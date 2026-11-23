#!/usr/bin/env python3
"""Motor activity from a phone recording (wheels-up tests).

Usage: research/motor_audio.py VIDEO [--threshold 150] [--bars]

Extracts the audio with ffmpeg, computes a 10 ms RMS envelope and prints the
segments where the motor runs (start, end, duration) and the gaps between
them. With numpy available, also prints the dominant frequency of each
segment, which follows motor speed — used for the speed-byte test.

Used on 2026-11-22 to measure the module watchdog (~0.38-0.40 s per frame).
"""
import argparse
import math
import struct
import subprocess
import sys

RATE = 8000
WIN = RATE // 100  # 10 ms


def load(path: str) -> list[int]:
    raw = subprocess.run(
        ["ffmpeg", "-v", "error", "-i", path, "-ac", "1", "-ar", str(RATE), "-f", "s16le", "-"],
        check=True,
        capture_output=True,
    ).stdout
    n = len(raw) // 2
    return list(struct.unpack(f"<{n}h", raw[: n * 2]))


def envelope(samples: list[int]) -> list[float]:
    return [
        math.sqrt(sum(x * x for x in samples[i : i + WIN]) / WIN)
        for i in range(0, len(samples) - WIN, WIN)
    ]


def segments(env: list[float], threshold: float, min_len: int = 3) -> list[tuple[int, int]]:
    out, start = [], None
    for i, v in enumerate(env + [0.0]):
        if v > threshold and start is None:
            start = i
        elif v <= threshold and start is not None:
            if i - start >= min_len:
                out.append((start, i))
            start = None
    return out


def dominant_hz(samples: list[int], a: int, b: int) -> float | None:
    try:
        import numpy as np
    except ImportError:
        return None
    # Skip spin-up / spin-down edges.
    lo, hi = (a + 20) * WIN, (b - 10) * WIN
    if hi - lo < RATE // 4:
        return None
    x = np.asarray(samples[lo:hi], dtype=float)
    x -= x.mean()
    spec = np.abs(np.fft.rfft(x * np.hanning(len(x))))
    freqs = np.fft.rfftfreq(len(x), 1 / RATE)
    band = (freqs > 60) & (freqs < 3000)
    return float(freqs[band][np.argmax(spec[band])])


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("video")
    ap.add_argument("--threshold", type=float, default=150)
    ap.add_argument("--bars", action="store_true", help="print a 100 ms bar chart")
    args = ap.parse_args()

    samples = load(args.video)
    env = envelope(samples)
    if args.bars:
        for i in range(0, len(env), 10):
            v = max(env[i : i + 10])
            print(f"{i / 100:6.1f}s {int(v):6d} " + "#" * int(v / 150))
    segs = segments(env, args.threshold)
    prev_end = None
    for a, b in segs:
        gap = f"  (przerwa {(a - prev_end) / 100:.2f}s)" if prev_end is not None else ""
        hz = dominant_hz(samples, a, b)
        rms = sum(env[a:b]) / (b - a)
        hz_s = f"  ~{hz:.0f} Hz" if hz else ""
        print(f"{a / 100:7.2f}s - {b / 100:7.2f}s  {(b - a) / 100:5.2f}s  rms {rms:5.0f}{hz_s}{gap}")
        prev_end = b
    return 0


if __name__ == "__main__":
    sys.exit(main())
