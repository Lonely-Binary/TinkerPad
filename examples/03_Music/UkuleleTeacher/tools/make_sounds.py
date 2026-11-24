#!/usr/bin/env python3
"""Turn sound files into sounds.h for UkuleleTeacher.

Usage:
    python3 make_sounds.py <folder>

<folder> must contain two sub-folders:
    ukulele/   the "samples" folder of the FreePats ukulele (CC0):
               https://freepats.zenvoid.org/GuitarFamily/ukulele.html (Ukulele-SFZ+FLAC)
    kenney/    the Kenney packs, unzipped (CC0 - https://kenney.nl), as for WhackAMole:
               kenney/ui (Interface Sounds), kenney/digital (Digital SFX)

Every sound becomes 22050 Hz, mono, 16-bit, trimmed and normalised.
"""

import os
import subprocess
import sys

RATE = 22050

# (name in the code, file, loudness 0-1, what it is for)
# The ukulele was recorded on 13 notes; the notes in between are played by
# playing a neighbour a little faster or slower. Long notes are cut at MAX_SECONDS.
MAX_SECONDS = 1.6
SOUNDS = [
    ("UKE_C4",   "ukulele/C4.flac",  1.0, "ukulele C4 (midi 60)"),
    ("UKE_D4",   "ukulele/D4.flac",  1.0, "ukulele D4 (62)"),
    ("UKE_E4",   "ukulele/E4.flac",  1.0, "ukulele E4 (64)"),
    ("UKE_FS4",  "ukulele/F#4.flac", 1.0, "ukulele F#4 (66)"),
    ("UKE_G4",   "ukulele/G4.flac",  1.0, "ukulele G4 (67)"),
    ("UKE_A4",   "ukulele/A4.flac",  1.0, "ukulele A4 (69)"),
    ("UKE_B4",   "ukulele/B4.flac",  1.0, "ukulele B4 (71)"),
    ("UKE_CS5",  "ukulele/C#5.flac", 1.0, "ukulele C#5 (73)"),
    ("TAP",      "ukulele/body-tap.flac", 0.8, "knock on the ukulele body - the count-in"),
    ("YOUR_TURN","kenney/ui/Audio/drop_002.ogg",         0.5, "now you play"),
    ("GO",       "kenney/ui/Audio/confirmation_001.ogg", 0.7, "start"),
    ("WELL_DONE","kenney/ui/Audio/confirmation_002.ogg", 0.6, "a phrase learned"),
    ("WRONG",    "kenney/ui/Audio/error_002.ogg",        0.6, "wrong note"),
    ("CHEER",    "kenney/digital/zapThreeToneUp.mp3",    0.7, "the whole song learned"),
]


def load(path):
    raw = subprocess.run(["ffmpeg", "-v", "error", "-i", path, "-ac", "1", "-ar", str(RATE),
                          "-t", str(MAX_SECONDS), "-f", "s16le", "-"], check=True, capture_output=True).stdout
    return [int.from_bytes(raw[i:i + 2], "little", signed=True) for i in range(0, len(raw), 2)]


def tidy(s, loudness):
    peak = max(1, max(abs(x) for x in s))
    start = next((i for i, x in enumerate(s) if abs(x) > peak * 0.02), 0)
    end = max((i for i, x in enumerate(s) if abs(x) > peak * 0.003), default=len(s) - 1)
    s = s[start:end + 1]
    fade = min(len(s), RATE // 8)                       # 125 ms fade-out
    for i in range(fade):
        s[-fade + i] = int(s[-fade + i] * (1 - i / fade))
    gain = 29000 / peak * loudness
    return [max(-32767, min(32767, int(x * gain))) for x in s]


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    folder = sys.argv[1]
    out = ["// sounds.h - sounds for UkuleleTeacher (made by tools/make_sounds.py)",
           "//",
           "// Ukulele: recorded by Mateusz Dabrowski for FreePats, CC0 (freepats.zenvoid.org).",
           "// Effects by Kenney (www.kenney.nl), CC0.",
           f"// {RATE} Hz, mono, 16-bit. Stored in flash, not RAM.",
           "", "#pragma once", "#include <stdint.h>", "", f"#define SOUND_RATE {RATE}", ""]
    total = 0
    for name, file, loud, what in SOUNDS:
        s = tidy(load(os.path.join(folder, file)), loud)
        total += len(s)
        out.append(f"// {name}: {what} ({file}, {len(s) / RATE:.2f} s)")
        out.append(f"const int16_t SND_{name}[{len(s)}] = {{")
        for i in range(0, len(s), 20):
            out.append("    " + ",".join(map(str, s[i:i + 20])) + ",")
        out.append("};\n")
        print(f"{name:10} {len(s) / RATE:.2f} s  {file}")
    out.append("// Sound numbers, to use in the game: play(POP) ...")
    out.append("enum Sound { " + ", ".join(n for n, *_ in SOUNDS) + ", SOUND_COUNT };")
    out.append("const int16_t *const SOUND_DATA[SOUND_COUNT] = {" + ", ".join(f"SND_{n}" for n, *_ in SOUNDS) + "};")
    out.append("const uint32_t SOUND_LENGTH[SOUND_COUNT] = {" + ", ".join(f"sizeof(SND_{n}) / 2" for n, *_ in SOUNDS) + "};")
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "sounds.h")
    with open(path, "w") as f:
        f.write("\n".join(out) + "\n")
    print(f"total {total / RATE:.1f} s, {total * 2 / 1024:.0f} KB of flash")


if __name__ == "__main__":
    main()
