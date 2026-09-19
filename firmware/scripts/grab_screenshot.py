#!/usr/bin/env python3
"""Pull a screenshot off the device over USB serial and save it as a PNG.

The firmware answers CMD:SCREENSHOT with a length header, the raw 1bpp
framebuffer, and a trailing marker:

    SCREENSHOT_START:<bytes>
    <bytes of packed framebuffer>
    SCREENSHOT_END

Usage:
    python grab_screenshot.py                    # autodetect port, write screenshot-<ts>.png
    python grab_screenshot.py /dev/cu.usbmodem1  # explicit port
    python grab_screenshot.py -o shot.png --raw  # also keep the unpacked bytes
    python grab_screenshot.py --selftest         # decode/encode check, no hardware
"""

from __future__ import annotations

import argparse
import glob
import struct
import sys
import zlib
from datetime import datetime

DEFAULT_BAUDRATE = 115200

# The panel geometry is not sent with the framebuffer, so it is recovered from
# the byte count. Both boards pack 8 pixels per byte, MSB first.
GEOMETRY_BY_SIZE = {
    800 // 8 * 480: (800, 480),  # 48000 - default panel
    792 // 8 * 528: (792, 528),  # 52272 - X3
}


def geometry_for(size: int, width: int | None) -> tuple[int, int]:
    """Panel dimensions for a framebuffer of `size` bytes."""
    if width is not None:
        if width % 8:
            raise ValueError(f"width {width} is not a multiple of 8")
        stride = width // 8
        if size % stride:
            raise ValueError(f"{size} bytes is not a whole number of {width}px rows")
        return width, size // stride
    if size not in GEOMETRY_BY_SIZE:
        known = ", ".join(f"{n} ({w}x{h})" for n, (w, h) in sorted(GEOMETRY_BY_SIZE.items()))
        raise ValueError(f"unrecognised framebuffer size {size}; known: {known}. Pass --width to override.")
    return GEOMETRY_BY_SIZE[size]


def unpack(data: bytes, width: int, height: int) -> bytearray:
    """Packed 1bpp framebuffer -> one byte per pixel, 1 = white."""
    stride = width // 8
    px = bytearray(width * height)
    for y in range(height):
        row = y * width
        for xb in range(stride):
            byte = data[y * stride + xb]
            base = row + xb * 8
            for b in range(8):
                px[base + b] = (byte >> (7 - b)) & 1
    return px


def rotate_cw(px: bytearray, width: int, height: int) -> tuple[bytearray, int, int]:
    """Rotate a pixel array 90 degrees clockwise."""
    out = bytearray(width * height)
    for y in range(height):
        row = y * width
        # column (height - 1 - y) of the destination, which is `height` wide
        dst = height - 1 - y
        for x in range(width):
            out[x * height + dst] = px[row + x]
    return out, height, width


def pack(px: bytearray, width: int, height: int) -> bytes:
    """One byte per pixel -> packed 1bpp rows, MSB first."""
    stride = width // 8
    out = bytearray(stride * height)
    for y in range(height):
        row = y * width
        for xb in range(stride):
            base = row + xb * 8
            v = 0
            for b in range(8):
                if px[base + b]:
                    v |= 1 << (7 - b)
            out[y * stride + xb] = v
    return bytes(out)


def framebuffer_to_png(data: bytes, width: int, height: int, rotate: bool = False) -> bytes:
    """Encode a packed 1bpp framebuffer as a greyscale PNG.

    A set bit is white on this panel (buffers clear to 0xFF), which is also
    PNG's convention for 1-bit greyscale, so with no rotation the rows pass
    through untouched apart from the per-row filter byte.
    """
    if rotate:
        if height % 8:
            raise ValueError(f"cannot rotate: height {height} is not a multiple of 8")
        px, width, height = rotate_cw(unpack(data, width, height), width, height)
        data = pack(px, width, height)

    stride = width // 8
    raw = bytearray()
    for y in range(height):
        raw.append(0)  # filter type 0 (None)
        raw += data[y * stride : (y + 1) * stride]

    def chunk(tag: bytes, payload: bytes) -> bytes:
        return (
            struct.pack(">I", len(payload))
            + tag
            + payload
            + struct.pack(">I", zlib.crc32(tag + payload) & 0xFFFFFFFF)
        )

    # bit depth 1, colour type 0 (greyscale), no interlace
    ihdr = struct.pack(">IIBBBBB", width, height, 1, 0, 0, 0, 0)
    return (
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", ihdr)
        + chunk(b"IDAT", zlib.compress(bytes(raw), 9))
        + chunk(b"IEND", b"")
    )


def autodetect_port() -> str:
    """The first plausible USB serial device, preferring a real USB modem."""
    candidates = sorted(
        glob.glob("/dev/cu.usbmodem*")
        + glob.glob("/dev/cu.usbserial*")
        + glob.glob("/dev/cu.wchusbserial*")
        + glob.glob("/dev/ttyUSB*")
        + glob.glob("/dev/ttyACM*")
    )
    if not candidates:
        raise SystemExit(
            "No USB serial device found. Plug the device in, or pass the port explicitly.\n"
            "Bluetooth and debug-console ports are deliberately ignored."
        )
    return candidates[0]


def open_port(port: str, baud: int, timeout: float):
    try:
        import serial  # type: ignore
    except ImportError:
        raise SystemExit("pyserial is required: pip install pyserial")
    return serial.Serial(port, baud, timeout=timeout)


def grab(ser) -> bytes:
    """One CMD:SCREENSHOT round-trip on an already-open port."""
    if True:
        ser.reset_input_buffer()
        ser.write(b"CMD:SCREENSHOT\n")
        ser.flush()

        # Log lines from other tasks can arrive before the header, so skip
        # anything that is not the marker rather than assuming it comes first.
        size = None
        while size is None:
            line = ser.readline()
            if not line:
                raise SystemExit("Timed out waiting for SCREENSHOT_START.")
            text = line.decode("utf-8", "replace").strip()
            if text.startswith("SCREENSHOT_START:"):
                size = int(text.split(":", 1)[1])

        data = ser.read(size)
        if len(data) != size:
            raise SystemExit(f"Short read: expected {size} bytes, got {len(data)}.")

        # Drain the trailer so the port is left clean for the next command.
        for _ in range(4):
            if b"SCREENSHOT_END" in ser.readline():
                break
        return data


def capture(port: str, baud: int, timeout: float) -> bytes:
    with open_port(port, baud, timeout) as ser:
        return grab(ser)


def selftest() -> int:
    """Round-trip a synthetic buffer for each known panel, without hardware."""
    for size, (width, height) in sorted(GEOMETRY_BY_SIZE.items()):
        assert geometry_for(size, None) == (width, height)
        assert geometry_for(size, width) == (width, height)
        # A vertical split: left half black, right half white.
        stride = width // 8
        row = bytes([0x00] * (stride // 2) + [0xFF] * (stride - stride // 2))
        png = framebuffer_to_png(row * height, width, height)
        assert png.startswith(b"\x89PNG\r\n\x1a\n")
        w, h, depth, colour = struct.unpack(">IIBB", png[16:26])
        assert (w, h, depth, colour) == (width, height, 1, 0), (w, h, depth, colour)

        rot = framebuffer_to_png(row * height, width, height, rotate=True)
        rw, rh = struct.unpack(">II", rot[16:24])
        assert (rw, rh) == (height, width), (rw, rh)
        print(f"ok  {width}x{height}  {size} bytes -> {len(png)} byte PNG, rotated {rw}x{rh}")

    # A known pattern must land where clockwise rotation puts it: the top-left
    # pixel of the result is the bottom-left pixel of the source.
    px = bytearray([1] * 64)
    px[56] = 0  # bottom-left of an 8x8 block
    out, ow, oh = rotate_cw(px, 8, 8)
    assert (ow, oh) == (8, 8)
    assert out[0] == 0, "clockwise rotation put the corner in the wrong place"
    print("ok  clockwise rotation orientation")

    for bad in (12345, 0):
        try:
            geometry_for(bad, None)
        except ValueError:
            pass
        else:
            raise AssertionError(f"{bad} should have been rejected")
    print("ok  unknown sizes rejected")
    return 0


def watch(port: str, baud: int, timeout: float, interval: float, prefix: str,
          width: int | None, rotate: bool, limit: int) -> int:
    """Poll the panel and save a file whenever the screen changes.

    Lets you walk through the UI by hand while every distinct screen is
    collected, instead of one round-trip per capture.
    """
    import hashlib
    import os
    import time

    seen: set[str] = set()
    saved = 0
    where = port or "any USB serial port"
    print(f"Watching {where} every {interval:g}s. Navigate the device; Ctrl-C to stop.")
    ser = None
    try:
        while saved < limit:
            # The port disappears whenever the device sleeps, resets or is
            # unplugged, so treat losing it as a pause rather than the end of
            # the session and pick the screens back up when it returns.
            if ser is None:
                if not port or not os.path.exists(port):
                    # The port can come back under a different number after a
                    # reset, so re-run detection rather than waiting forever on
                    # the name we started with.
                    try:
                        found = autodetect_port()
                    except SystemExit:
                        time.sleep(interval)
                        continue
                    if found != port:
                        print(f"  (port moved: {port} -> {found})")
                        port = found
                try:
                    ser = open_port(port, baud, timeout)
                    print(f"  (connected to {port})")
                except Exception as exc:
                    print(f"  (waiting for {port}: {exc})")
                    time.sleep(interval)
                    continue

            try:
                data = grab(ser)
            except SystemExit as exc:  # a dropped frame should not end the session
                print(f"  (skipped: {exc})")
                time.sleep(interval)
                continue
            except Exception as exc:  # port went away underneath us
                print(f"  (lost {port}: {exc}; waiting for it to come back)")
                try:
                    ser.close()
                except Exception:
                    pass
                ser = None
                time.sleep(interval)
                continue

            digest = hashlib.sha1(data).hexdigest()
            if digest not in seen:
                seen.add(digest)
                w, h = geometry_for(len(data), width)
                saved += 1
                out = f"{prefix}-{saved:02d}.png"
                with open(out, "wb") as f:
                    f.write(framebuffer_to_png(data, w, h, rotate=rotate))
                print(f"  [{saved}] {out}", flush=True)
            time.sleep(interval)
    except KeyboardInterrupt:
        print()
    finally:
        if ser is not None:
            try:
                ser.close()
            except Exception:
                pass
    print(f"Captured {saved} distinct screen(s).")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("port", nargs="?", help="Serial port (autodetected if omitted)")
    parser.add_argument("--baud", type=int, default=DEFAULT_BAUDRATE, help=f"Baud rate (default: {DEFAULT_BAUDRATE})")
    parser.add_argument("-o", "--output", help="Output PNG path (default: screenshot-<timestamp>.png)")
    parser.add_argument("--width", type=int, help="Panel width in px, if the framebuffer size is unrecognised")
    parser.add_argument("--raw", action="store_true", help="Also write the unpacked framebuffer alongside the PNG")
    parser.add_argument(
        "--no-rotate",
        action="store_true",
        help="Keep the panel's native landscape scan order instead of rotating the UI upright",
    )
    parser.add_argument("--timeout", type=float, default=10.0, help="Serial read timeout in seconds (default: 10)")
    parser.add_argument("--selftest", action="store_true", help="Check encoding without a device attached")
    parser.add_argument(
        "--watch",
        action="store_true",
        help="Poll continuously and save a numbered file each time the screen changes",
    )
    parser.add_argument("--interval", type=float, default=2.0, help="Seconds between polls in --watch (default: 2)")
    parser.add_argument("--limit", type=int, default=50, help="Stop --watch after this many screens (default: 50)")
    args = parser.parse_args()

    if args.selftest:
        return selftest()

    if args.watch:
        # Watch mode waits for the device, so an absent port is not fatal here.
        try:
            port = args.port or autodetect_port()
        except SystemExit:
            port = args.port or ""
        prefix = (args.output or "screen").rsplit(".", 1)[0]
        return watch(port, args.baud, args.timeout, args.interval, prefix,
                     args.width, not args.no_rotate, args.limit)

    port = args.port or autodetect_port()

    print(f"Capturing from {port} at {args.baud} baud...")
    data = capture(port, args.baud, args.timeout)

    width, height = geometry_for(len(data), args.width)
    # The framebuffer is scanned out in landscape but the UI is drawn portrait,
    # so an unrotated capture is on its side. Rotating clockwise puts the status
    # bar back at the top.
    rotate = not args.no_rotate
    out = args.output or f"screenshot-{datetime.now():%Y%m%d-%H%M%S}.png"
    with open(out, "wb") as f:
        f.write(framebuffer_to_png(data, width, height, rotate=rotate))
    shown = (height, width) if rotate else (width, height)
    print(f"Saved {shown[0]}x{shown[1]} screenshot to {out}")

    if args.raw:
        raw_path = out.rsplit(".", 1)[0] + ".raw"
        with open(raw_path, "wb") as f:
            f.write(data)
        print(f"Saved raw framebuffer to {raw_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
