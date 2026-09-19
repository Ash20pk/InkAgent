#!/usr/bin/env python3
"""Walk the UI over serial and save a screenshot of each screen.

Needs firmware built from [env:screenshot], which adds the CMD:KEY:<name>
command (see INKAGENT_SERIAL_INPUT). A tour is a list of steps; each step names
the keys to press and the screen they should land on:

    python capture_tour.py --tour settings -o shots/
    python capture_tour.py --list
    python capture_tour.py --keys DOWN,DOWN,CONFIRM -o shots/ --label apps
    python capture_tour.py --selftest

Screens that need outside state (a crash, a network, a Calibre server, the USB
host) are not in any tour: they cannot be reached by pressing buttons alone.
"""

from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from grab_screenshot import (  # noqa: E402
    autodetect_port,
    framebuffer_to_png,
    geometry_for,
    grab,
    open_port,
)

KEYS = {"BACK", "CONFIRM", "LEFT", "RIGHT", "UP", "DOWN", "POWER"}

# Each step: (label, keys pressed to get there from the previous step).
#
# Navigation, established by tracing the real device rather than assumed:
#   * a reset boots to home with NOTHING highlighted -- the only reliable way
#     back to a known screen, since this board has no home key and no touch, so
#     no button leaves the reader;
#   * from home, DOWN x1/x2/x3 highlights Recent Books / Settings / Apps, and
#     CONFIRM opens the highlight. CONFIRM with nothing highlighted resumes the
#     book instead;
#   * BACK unwinds inside menus, but does nothing in the reader.
# Every tour therefore expects --reset before it runs.

TOURS: dict[str, list[tuple[str, list[str]]]] = {
    "home": [
        ("home", ["DOWN"]),
        ("home-recent-books-selected", ["DOWN"]),
        ("home-settings-selected", ["DOWN"]),
        ("home-apps-selected", ["DOWN"]),
    ],
    "recent-books": [
        ("recent-books", ["DOWN", "CONFIRM"]),
        ("recent-books-sel-2", ["DOWN"]),
        ("recent-books-sel-3", ["DOWN"]),
    ],
    # CONFIRM here switches tab, because no row is selected. Do NOT press DOWN
    # first: that selects a row and turns CONFIRM into Toggle, which edits the
    # user's settings rather than photographing them.
    "settings": [
        ("settings-display", ["DOWN", "DOWN", "CONFIRM"]),
        ("settings-reader", ["CONFIRM"]),
        ("settings-controls", ["CONFIRM"]),
        ("settings-system", ["CONFIRM"]),
    ],
    # Scrolling the list is safe; CONFIRM on a row is not, so it is left out.
    "settings-display-rows": [
        ("settings-display-sel-1", ["DOWN", "DOWN", "CONFIRM", "DOWN"]),
        ("settings-display-sel-5", ["DOWN", "DOWN", "DOWN", "DOWN"]),
        ("settings-display-sel-9", ["DOWN", "DOWN", "DOWN", "DOWN"]),
    ],
    # Only read-only apps are opened. File Transfer and Send to reader start
    # network/USB work and are deliberately not toured.
    "apps": [
        ("apps-drawer", ["DOWN", "DOWN", "DOWN", "CONFIRM"]),
        ("apps-recent-books", ["CONFIRM"]),
        ("apps-drawer-back", ["BACK"]),
        ("apps-sel-2", ["DOWN"]),
        ("apps-open-2", ["CONFIRM"]),
        ("apps-back-2", ["BACK"]),
        ("apps-sel-3", ["DOWN"]),
        ("apps-open-3", ["CONFIRM"]),
        ("apps-back-3", ["BACK"]),
        ("apps-sel-4", ["DOWN"]),
        ("apps-open-4", ["CONFIRM"]),
        ("apps-back-4", ["BACK"]),
    ],
    # CONFIRM opens the reader menu; activating a menu row could change a
    # setting, so the tour only scrolls it.
    "reader": [
        ("reader", ["CONFIRM"]),
        ("reader-page-next", ["RIGHT"]),
        ("reader-page-prev", ["LEFT"]),
        ("reader-menu", ["CONFIRM"]),
        ("reader-menu-sel-1", ["DOWN"]),
        ("reader-menu-sel-2", ["DOWN"]),
        ("reader-menu-sel-3", ["DOWN"]),
    ],
}


def reset_device(port: str, wait: float) -> bool:
    """Reboot the device so a tour starts from the home screen.

    There is no button that leaves the reader on a board with no home key and
    no touch, so a reset is the only reliable way back to a known screen.
    esptool's own hard reset is reused rather than reimplementing the
    USB-Serial/JTAG line dance.
    """
    pio = shutil.which("pio") or str(Path.home() / ".platformio/penv/bin/pio")
    cmd = [pio, "pkg", "exec", "-p", "tool-esptoolpy", "--",
           "esptool.py", "--chip", "esp32c3", "-p", port, "chip_id"]
    try:
        subprocess.run(cmd, capture_output=True, timeout=90, check=False)
    except Exception as exc:
        print(f"  ! reset failed: {exc}")
        return False
    print(f"  (reset; waiting {wait:g}s for boot)")
    time.sleep(wait)
    return True


def send_key(ser, key: str, settle: float) -> str:
    """Press one button and return the firmware's acknowledgement."""
    ser.reset_input_buffer()
    ser.write(f"CMD:KEY:{key}\n".encode())
    ser.flush()
    ack = ""
    deadline = time.time() + 2.0
    while time.time() < deadline:
        line = ser.readline().decode("utf-8", "replace").strip()
        if not line:
            break
        if line.startswith(("KEY_OK:", "KEY_ERR:")):
            ack = line
            break
    time.sleep(settle)
    return ack


def capture_to(ser, out_dir: Path, label: str, width: int | None, rotate: bool) -> Path:
    data = grab(ser)
    w, h = geometry_for(len(data), width)
    path = out_dir / f"{label}.png"
    path.write_bytes(framebuffer_to_png(data, w, h, rotate=rotate))
    return path


def run_tour(steps, port, baud, timeout, out_dir, settle, width, rotate) -> int:
    out_dir.mkdir(parents=True, exist_ok=True)
    saved = 0
    with open_port(port, baud, timeout) as ser:
        for label, keys in steps:
            for key in keys:
                ack = send_key(ser, key, settle)
                if ack.startswith("KEY_ERR"):
                    print(f"  ! firmware rejected key {key} ({ack})")
                elif not ack:
                    # No acknowledgement means the build almost certainly lacks
                    # the command, so say that once rather than at every step.
                    print(f"  ! no KEY_OK for {key}; is this an [env:screenshot] build?")
                    return 1
            try:
                path = capture_to(ser, out_dir, label, width, rotate)
            except SystemExit as exc:
                print(f"  ! {label}: {exc}")
                continue
            saved += 1
            print(f"  [{saved}] {path.name}")
    print(f"Saved {saved} screen(s) to {out_dir}")
    return 0


def selftest() -> int:
    """Check the tours are well formed, without a device."""
    for name, steps in TOURS.items():
        assert steps, f"tour {name} is empty"
        labels = [label for label, _ in steps]
        assert len(labels) == len(set(labels)), f"tour {name} has duplicate labels"
        for label, keys in steps:
            assert keys, f"{name}/{label} presses nothing"
            for key in keys:
                assert key in KEYS, f"{name}/{label} uses unknown key {key}"
        print(f"ok  {name}: {len(steps)} steps, {sum(len(k) for _, k in steps)} presses")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("port", nargs="?", help="Serial port (autodetected if omitted)")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--tour", help=f"Tour to run: {', '.join(TOURS)}, or 'all'")
    parser.add_argument("--keys", help="Comma-separated keys for a one-off capture, e.g. DOWN,CONFIRM")
    parser.add_argument("--label", default="capture", help="Filename stem when using --keys")
    parser.add_argument("-o", "--out", default="screens", help="Output directory (default: screens)")
    parser.add_argument("--settle", type=float, default=1.2, help="Seconds to wait after each press (default: 1.2)")
    parser.add_argument("--timeout", type=float, default=10.0)
    parser.add_argument("--width", type=int, help="Panel width override")
    parser.add_argument("--no-rotate", action="store_true")
    parser.add_argument(
        "--trace",
        help="Comma-separated keys; capture after EVERY press, numbered. For working out "
             "what the UI actually does before writing a tour.",
    )
    parser.add_argument("--reset", action="store_true",
                        help="Reboot the device first so the tour starts from the home screen")
    parser.add_argument("--boot-wait", type=float, default=11.0,
                        help="Seconds to wait for boot after --reset (default: 11)")
    parser.add_argument("--list", action="store_true", help="List the tours and exit")
    parser.add_argument("--selftest", action="store_true", help="Validate the tours without a device")
    args = parser.parse_args()

    if args.selftest:
        return selftest()
    if args.list:
        for name, steps in TOURS.items():
            print(f"{name:10s} {len(steps):2d} screens: {', '.join(l for l, _ in steps)}")
        return 0

    if args.trace:
        keys = [k.strip().upper() for k in args.trace.split(",") if k.strip()]
        steps = [(f"{args.label}-{i:02d}-{k.lower()}", [k]) for i, k in enumerate(keys, 1)]
    elif args.keys:
        steps = [(args.label, [k.strip().upper() for k in args.keys.split(",") if k.strip()])]
    elif args.tour == "all":
        steps = [s for t in TOURS.values() for s in t]
    elif args.tour in TOURS:
        steps = TOURS[args.tour]
    else:
        parser.error(f"--tour must be one of {', '.join(TOURS)}, or 'all'; or pass --keys")

    port = args.port or autodetect_port()
    print(f"Driving {port} at {args.baud} baud...")
    if args.reset:
        reset_device(port, args.boot_wait)
    return run_tour(steps, port, args.baud, args.timeout, Path(args.out),
                    args.settle, args.width, not args.no_rotate)


if __name__ == "__main__":
    sys.exit(main())
