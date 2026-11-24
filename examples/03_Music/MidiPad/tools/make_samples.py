#!/usr/bin/env python3
"""Turn drum recordings into drum_samples.h for MidiPad.

Usage:
    python3 make_samples.py <folder with the recordings>

Every pad gets one recording. The list below says which file goes on which
pad (same order as NOTES in MidiPad.ino), how long to keep it and how loud. Any audio
format ffmpeg can read works (WAV, FLAC, MP3...), so you can swap in your own
sounds: put them in a folder, change the file names below, run this again.

Each sound is converted to 32 kHz, mono, 16-bit, trimmed, faded out at the
end, and made as loud as possible without clipping.

Default sounds: MuldjordKit by Lars Muldjord, FreePats version,
Creative Commons Attribution 4.0 - https://freepats.zenvoid.org/Percussion/acoustic-drum-kit.html
"""

import os
import subprocess
import sys

RATE = 32000

# (file, seconds to keep, loudness 0-1, name) - one line per pad, top-left first.
# The numbers in the MuldjordKit file names are recordings from soft to hard;
# drumkit.xml in the kit says which is which.
PADS = [
    ("8-CrashL.flac",       2.5, 0.85, "CRASH"),
    ("11-CrashR.flac",      2.5, 0.85, "CRASH_2"),
    ("9-RideL.flac",        2.0, 0.80, "RIDE"),
    ("7-RideRBell.flac",    1.5, 0.80, "RIDE_BELL"),

    ("19-Tom4.flac",        1.5, 1.00, "FLOOR_TOM"),
    ("14-Tom3.flac",        1.4, 1.00, "LOW_TOM"),
    ("12-Tom2.flac",        1.3, 1.00, "MID_TOM"),
    ("10-Tom1.flac",        1.2, 1.00, "HIGH_TOM"),

    ("20-Snare.flac",       0.8, 0.45, "SNARE_SOFT"),
    ("11-China.flac",       2.0, 0.80, "CHINA"),
    ("10-HihatClosed.flac", 0.3, 0.45, "PEDAL_HAT"),
    ("25-KdrumR.flac",      0.9, 1.00, "KICK_2"),

    ("23-KdrumL.flac",      0.9, 1.00, "KICK"),
    ("55-Snare.flac",       0.9, 1.00, "SNARE"),
    ("28-HihatClosed.flac", 0.4, 0.70, "CLOSED_HAT"),
    ("29-HihatOpen.flac",   1.2, 0.70, "OPEN_HAT"),
]

FADE_SECONDS = 0.08     # smooth fade at the end so nothing clicks


def load(path, seconds):
    """Decode with ffmpeg into a list of 16-bit mono samples."""
    raw = subprocess.run(
        ["ffmpeg", "-v", "error", "-i", path, "-ac", "1", "-ar", str(RATE),
         "-t", str(seconds), "-f", "s16le", "-"],
        check=True, capture_output=True).stdout
    return [int.from_bytes(raw[i:i + 2], "little", signed=True) for i in range(0, len(raw), 2)]


def tidy(samples, loudness):
    # Drop silence before the hit
    start = next((i for i, s in enumerate(samples) if abs(s) > 300), 0)
    samples = samples[max(0, start - 16):]
    # Drop the quiet tail (below about -50 dB)
    peak = max(1, max(abs(s) for s in samples))
    end = max((i for i, s in enumerate(samples) if abs(s) > peak * 0.003), default=len(samples) - 1)
    samples = samples[:end + 1]
    # Fade out
    fade = min(len(samples), int(FADE_SECONDS * RATE))
    for i in range(fade):
        samples[-fade + i] = int(samples[-fade + i] * (1 - i / fade))
    # As loud as possible (peak at about -1 dB), then scaled by `loudness`
    gain = 29000 / peak * loudness
    return [max(-32767, min(32767, int(s * gain))) for s in samples]


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    folder = sys.argv[1]
    out_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "drum_samples.h")

    lines = [
        "// drum_samples.h - real drum recordings for MidiPad (made by tools/make_samples.py)",
        "//",
        "// Sounds: MuldjordKit by Lars Muldjord, FreePats version,",
        "// Creative Commons Attribution 4.0 - https://freepats.zenvoid.org/Percussion/acoustic-drum-kit.html",
        f"// {RATE} Hz, mono, 16-bit. Stored in flash, not RAM.",
        "",
        "#pragma once",
        "#include <stdint.h>",
        "",
        f"#define SAMPLE_RATE {RATE}",
        "",
    ]
    total = 0
    for file, seconds, loudness, name in PADS:
        samples = tidy(load(os.path.join(folder, file), seconds), loudness)
        total += len(samples)
        lines.append(f"// {name}: {file}, {len(samples) / RATE:.2f} s")
        lines.append(f"const int16_t {name}[{len(samples)}] = {{")
        for i in range(0, len(samples), 20):
            lines.append("    " + ",".join(str(s) for s in samples[i:i + 20]) + ",")
        lines.append("};")
        lines.append("")
        print(f"{name:12} {file:22} {len(samples) / RATE:.2f} s")

    lines.append("// One recording per pad, top-left first")
    lines.append("const int16_t *const SAMPLES[16] = {" + ", ".join(p[3] for p in PADS) + "};")
    lines.append("const uint32_t LENGTHS[16] = {" + ", ".join(f"sizeof({p[3]}) / 2" for p in PADS) + "};")
    lines.append("")
    with open(out_path, "w") as f:
        f.write("\n".join(lines))
    print(f"total {total / RATE:.1f} s, {total * 2 / 1024:.0f} KB of flash -> {os.path.normpath(out_path)}")


if __name__ == "__main__":
    main()
