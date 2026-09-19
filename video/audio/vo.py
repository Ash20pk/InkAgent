#!/usr/bin/env python3
"""Render the voiceover: one `say` clip per line, laid onto a single 48 kHz bed
at the timestamp the line is cued to. Overruns are reported, not hidden — a line
that runs into the next cue is a script problem, not a mixing one."""
import json, os, subprocess, sys, wave
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
TMP = os.path.join(HERE, ".vo")
SR = 48000
DUR = 64.2  # LaunchVideo: 1926 frames at 30 fps

def read_wav(path):
    with wave.open(path) as w:
        n, ch, sw = w.getnframes(), w.getnchannels(), w.getsampwidth()
        raw = np.frombuffer(w.readframes(n), dtype=np.int16).astype(np.float32) / 32768
        if ch == 2:
            raw = raw.reshape(-1, 2).mean(axis=1)
        assert w.getframerate() == SR, w.getframerate()
    return raw

def main():
    spec = json.load(open(os.path.join(HERE, "script.json")))
    os.makedirs(TMP, exist_ok=True)
    bed = np.zeros(int(DUR * SR) + SR, dtype=np.float32)
    cues = np.zeros(len(bed), dtype=np.float32)  # 1 where speech is, for ducking
    lines = spec["lines"]
    for i, ln in enumerate(lines):
        aiff = os.path.join(TMP, f"{i:02d}.aiff")
        wav = os.path.join(TMP, f"{i:02d}.wav")
        subprocess.run(["say", "-v", spec["voice"], "-r", str(spec["rate"]),
                        "-o", aiff, ln["text"]], check=True)
        subprocess.run(["ffmpeg", "-y", "-loglevel", "error", "-i", aiff,
                        "-ar", str(SR), "-ac", "1", wav], check=True)
        a = read_wav(wav)
        # Trim the leading/trailing silence `say` pads every clip with.
        loud = np.where(np.abs(a) > 0.006)[0]
        if len(loud):
            a = a[max(0, loud[0] - int(0.02 * SR)): loud[-1] + int(0.06 * SR)]
        # Level each line to the same RMS. `say` varies a lot line to line, and
        # an even voice is also a louder one at the same peak.
        rms = np.sqrt(np.mean(a ** 2))
        if rms > 1e-5:
            a = a * (0.09 / rms)
        # 12 ms ramps so nothing clicks in.
        r = int(0.012 * SR)
        a[:r] *= np.linspace(0, 1, r)
        a[-r:] *= np.linspace(1, 0, r)
        at = int(ln["t"] * SR)
        bed[at:at + len(a)] += a
        cues[at:at + len(a)] = 1.0
        end = ln["t"] + len(a) / SR
        nxt = lines[i + 1]["t"] if i + 1 < len(lines) else DUR
        flag = "  OVERRUN" if end > nxt + 0.02 else ""
        print(f"{i:02d} {ln['t']:6.2f} -> {end:6.2f} (next {nxt:6.2f}){flag}  {ln['text'][:56]}")

    bed = bed[:int(DUR * SR)]
    cues = cues[:int(DUR * SR)]
    # Soft-knee limiting rather than a peak normalise: the loud consonants give
    # way instead of the whole track being pulled down to meet them.
    peak = np.abs(bed).max()
    bed = np.tanh(bed * 1.6) / 1.6
    bed *= 0.93 / np.abs(bed).max()
    np.save(os.path.join(HERE, "cues.npy"), cues)
    out = os.path.join(HERE, "..", "public", "vo.wav")
    with wave.open(out, "w") as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(SR)
        w.writeframes((bed * 32767).astype(np.int16).tobytes())
    print("wrote", os.path.normpath(out), f"peak {peak:.3f}")

main()
