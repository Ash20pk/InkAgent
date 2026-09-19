#!/usr/bin/env python3
"""The soundtrack, generated rather than licensed — so it is the project's own,
and so it can be written against the edit instead of cut to fit it.

120 bpm, which is the edit's own grid (theme.ts: BEAT = 15 frames at 30 fps), so
every pluck lands on a cut. Aeolian on D; the chord changes sit on the scene
boundaries. Nothing here is louder than the voice: the mix ducks off the
voiceover's own envelope."""
import os
import numpy as np
import wave

HERE = os.path.dirname(os.path.abspath(__file__))
SR = 48000
DUR = 64.2
BEAT = 0.5  # 120 bpm
N = int(DUR * SR)
T = np.arange(N) / SR

def hz(semitones_from_a4):
    return 440.0 * 2 ** (semitones_from_a4 / 12)

# Note names -> semitones from A4, octave 4 = the octave A440 lives in.
STEP = {'C': -9, 'D': -7, 'E': -5, 'F': -4, 'G': -2, 'A': 0, 'Bb': 1, 'B': 2}
def note(name, octave):
    return hz(STEP[name] + (octave - 4) * 12)

# Scene boundaries in seconds (Root.tsx durations less the dissolve overlap).
SCENES = [0.0, 4.533, 14.067, 26.6, 41.467, 52.0, 59.2, DUR]
# One chord per scene. Dm is home; the lift under Apps is the only major triad,
# and the end falls back to Dm.
CHORDS = [
    [('D', 3), ('F', 3), ('A', 3)],
    [('Bb', 2), ('D', 3), ('F', 3)],
    [('G', 2), ('Bb', 2), ('D', 3)],
    [('F', 2), ('A', 2), ('C', 3)],
    [('Bb', 2), ('D', 3), ('F', 3)],
    [('C', 3), ('E', 3), ('G', 3)],
    [('D', 3), ('A', 3), ('D', 4)],
]
# How present the pad and the plucks are in each scene. The piece opens thin,
# fills through the middle, and thins again for the close.
PAD_GAIN = [0.30, 0.46, 0.60, 0.62, 0.50, 0.66, 0.44]
PLUCK_DENSITY = [0, 2, 4, 4, 2, 8, 2]  # a pluck every N beats; 0 = none

def seg(a, b):
    return slice(max(0, int(a * SR)), min(N, int(b * SR)))

def ramp(n, attack, release):
    """Attack/release envelope in samples, cosine-shaped so nothing edges."""
    e = np.ones(n)
    a, r = min(attack, n // 2), min(release, n // 2)
    if a: e[:a] = (1 - np.cos(np.linspace(0, np.pi, a))) / 2
    if r: e[-r:] = (1 + np.cos(np.linspace(0, np.pi, r))) / 2
    return e

out = np.zeros(N)

# --- Drone: D1/D2, the floor the whole thing sits on. -----------------------
d1, d2 = note('D', 1), note('D', 2)
drift = 1 + 0.0009 * np.sin(2 * np.pi * 0.06 * T)   # a slow detune, so it breathes
swell = 0.55 + 0.45 * (1 - np.cos(np.linspace(0, 2 * np.pi * 1.5, N))) / 2
out += 0.16 * swell * np.sin(2 * np.pi * d1 * T)
out += 0.10 * swell * np.sin(2 * np.pi * d2 * T * drift)

# --- Pad: the chord of the scene, three sines plus a fifth-ish shimmer. -----
for i, (a, b) in enumerate(zip(SCENES, SCENES[1:])):
    s = seg(max(0, a - 1.2), b + 1.2)
    n = s.stop - s.start
    t = np.arange(n) / SR
    env = ramp(n, int(1.1 * SR), int(1.1 * SR)) * PAD_GAIN[i]
    # A slow tremolo at half the beat rate keeps the pad from sitting dead still.
    env = env * (0.88 + 0.12 * np.sin(2 * np.pi * (1 / (BEAT * 8)) * t))
    v = np.zeros(n)
    for j, (nm, oc) in enumerate(CHORDS[i]):
        f = note(nm, oc)
        v += np.sin(2 * np.pi * f * t + j) / (j + 1.6)
        v += 0.18 * np.sin(2 * np.pi * f * 2 * t + j)  # one octave of air
    out[s] += 0.075 * env * v

# --- Plucks: a struck sine with a fast decay, on the beat grid. -------------
rng = np.random.default_rng(7)
for i, (a, b) in enumerate(zip(SCENES, SCENES[1:])):
    every = PLUCK_DENSITY[i]
    if not every:
        continue
    degrees = [note(nm, oc) for nm, oc in CHORDS[i]]
    degrees += [d * 2 for d in degrees]  # an octave up, for the top voice
    k = 0
    t0 = np.ceil(a / BEAT) * BEAT
    while t0 < b - 0.4:
        f = degrees[(k * 3 + i) % len(degrees)]
        if f < 200:
            f *= 2  # keep the plucks above the pad
        dur = 1.6
        n = int(dur * SR)
        if int(t0 * SR) + n > N:
            n = N - int(t0 * SR)
        t = np.arange(n) / SR
        decay = np.exp(-t * 3.4)
        v = np.sin(2 * np.pi * f * t) + 0.35 * np.sin(2 * np.pi * f * 2 * t) * np.exp(-t * 9)
        v *= decay * ramp(n, 60, 200)
        g = 0.085 * (0.75 + 0.25 * rng.random())
        out[int(t0 * SR):int(t0 * SR) + n] += g * v
        k += 1
        t0 += every * BEAT

# --- Scene changes: a short filtered-noise swell into each cut. -------------
# The film's own transition is a panel refresh, so the sound is a sweep that
# arrives and stops, not a crash that rings.
for a in SCENES[1:-1]:
    n = int(0.9 * SR)
    start = int((a - 0.7) * SR)
    if start < 0:
        continue
    t = np.arange(n) / SR
    noise = rng.normal(0, 1, n)
    # One-pole low pass, opening as it rises.
    y = np.zeros(n)
    acc = 0.0
    for idx in range(n):
        acc += (noise[idx] - acc) * (0.004 + 0.02 * idx / n)
        y[idx] = acc
    y *= np.linspace(0, 1, n) ** 2 * ramp(n, 10, int(0.25 * SR))
    out[start:start + n] += 2.4 * 0.05 * y

# --- Duck under the voice, using the voiceover's own speech envelope. -------
cues_path = os.path.join(HERE, 'cues.npy')
duck = np.ones(N)
if os.path.exists(cues_path):
    cues = np.load(cues_path)[:N]
    if len(cues) < N:
        cues = np.pad(cues, (0, N - len(cues)))
    # Widen the speech mask a little either side, then smooth it into a gain
    # curve: -8 dB while talking, with ~250 ms of glide in and out.
    w = int(0.12 * SR)
    mask = np.convolve(cues, np.ones(w), mode='same') > 0
    k = int(0.25 * SR)
    smooth = np.convolve(mask.astype(float), np.hanning(k) / np.hanning(k).sum(), mode='same')
    duck = 1 - 0.60 * np.clip(smooth, 0, 1)
out *= duck

# Top and tail: up over the first bar, out over the last two.
out[:int(1.5 * SR)] *= np.linspace(0, 1, int(1.5 * SR)) ** 1.5
out[-int(2.4 * SR):] *= np.linspace(1, 0, int(2.4 * SR)) ** 1.4

# Soft-clip rather than hard-limit, then normalise with headroom for the voice.
out = np.tanh(out * 1.25) / 1.25
out *= 0.34 / np.abs(out).max()

dest = os.path.join(HERE, '..', 'public', 'score.wav')
with wave.open(dest, 'w') as w:
    w.setnchannels(1); w.setsampwidth(2); w.setframerate(SR)
    w.writeframes((out * 32767).astype(np.int16).tobytes())
print('wrote', os.path.normpath(dest), f'{DUR}s, peak {np.abs(out).max():.3f}')
