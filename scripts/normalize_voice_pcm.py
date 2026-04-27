#!/usr/bin/env python3
"""Normalize voice PCM headers to peak amplitude.

Each file in software/OnSpeed-Gen3-ESP32/Audio/PCM_*.h holds a header-less
int16 LE PCM stream wrapped in a `const unsigned char xxx_pcm[]` array.
This script rescales each file so its peak sample sits at PEAK_TARGET
(0.99 * int16 full-scale, leaving ~0.09 dB of safety headroom), in place,
preserving the original 16-bytes-per-line array layout.

Used as a one-shot when retiring VOICE_BOOST: after running this, every
PCM file is mastered at the same loudness, and the runtime gain chain
(fVolume * pan * envelope) can use a unity boost without clipping.
"""

import glob
import os
import re
import struct
import sys

PEAK_TARGET = int(0.99 * 32767)   # 32439

AUDIO_DIR = os.path.join(
    os.path.dirname(os.path.abspath(__file__)),
    "..",
    "software",
    "OnSpeed-Gen3-ESP32",
    "Audio",
)


def parse(path):
    """Return (header, samples list[int], footer) where header ends at the
    open brace and footer starts at the close brace."""
    with open(path) as f:
        text = f.read()

    m = re.search(r"\{\s*", text)
    if m is None:
        raise RuntimeError(f"no opening brace in {path}")
    open_idx = m.start()
    header = text[: open_idx + 1] + "\n"

    m2 = re.search(r"\};", text[open_idx:])
    if m2 is None:
        raise RuntimeError(f"no closing brace in {path}")
    close_idx = open_idx + m2.start()
    footer = text[close_idx:]

    body = text[open_idx + 1 : close_idx]
    hex_bytes = re.findall(r"0x([0-9a-fA-F]{2})", body)
    raw = bytes(int(b, 16) for b in hex_bytes)
    if len(raw) % 2 != 0:
        raise RuntimeError(f"odd byte count in {path}")
    samples = list(struct.unpack(f"<{len(raw)//2}h", raw))
    return header, samples, footer


def emit_array(samples):
    """Render samples as 16 bytes per line, two-space indent, lowercase
    hex, trailing comma on every line including the last (matches the
    incoming convention).
    """
    raw = struct.pack(f"<{len(samples)}h", *samples)
    out_lines = []
    per_line = 16
    for i in range(0, len(raw), per_line):
        chunk = raw[i : i + per_line]
        out_lines.append("  " + ", ".join(f"0x{b:02x}" for b in chunk) + ",")
    return "\n".join(out_lines) + "\n"


def normalize(samples):
    peak = max(abs(s) for s in samples)
    if peak == 0:
        return samples, 1.0
    scale = PEAK_TARGET / peak
    out = []
    for s in samples:
        v = int(round(s * scale))
        if v > 32767:
            v = 32767
        elif v < -32768:
            v = -32768
        out.append(v)
    return out, scale


def main():
    files = sorted(glob.glob(os.path.join(AUDIO_DIR, "PCM_*.h")))
    if not files:
        print(f"no PCM_*.h files found in {AUDIO_DIR}", file=sys.stderr)
        return 1

    print(f"{'file':40s}  {'old_peak':>8s}  {'scale':>6s}  {'new_peak':>8s}")
    for path in files:
        header, samples, footer = parse(path)
        old_peak = max(abs(s) for s in samples)
        new_samples, scale = normalize(samples)
        new_peak = max(abs(s) for s in new_samples)
        out = header + emit_array(new_samples) + footer
        with open(path, "w") as f:
            f.write(out)
        print(f"{os.path.basename(path):40s}  {old_peak:8d}  {scale:6.2f}  {new_peak:8d}")

    return 0


if __name__ == "__main__":
    sys.exit(main())
