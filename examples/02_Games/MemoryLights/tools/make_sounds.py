#!/usr/bin/env python3
"""Turn sound files into sounds.h for MemoryLights.

Usage:
    python3 make_sounds.py <folder>

<folder> must contain two sub-folders:
    kenney/      the Kenney packs, unzipped (CC0 - https://kenney.nl), as for WhackAMole:
                 kenney/ui (Interface Sounds), kenney/digital (Digital SFX)
    xylophone/   the "samples" folder of the FreePats xylophone (CC0):
                 https://freepats.zenvoid.org/ChromaticPercussion/xylophone.html
                 (Xylophone-MediumMallets-SFZ+FLAC)

Every sound becomes 22050 Hz, mono, 16-bit, trimmed and normalised.
"""

import os
import subprocess
import sys

RATE = 22050

# (name in the code, file, loudness 0-1, what it is for)
# The xylophone was recorded on every C and G; the game plays the notes in
# between by playing these a little faster or slower.
SOUNDS = [
    ("XYLO_C5",   "xylophone/C5_ff.flac",                 1.0, "xylophone C5"),
    ("XYLO_G5",   "xylophone/G5_ff.flac",                 1.0, "xylophone G5"),
    ("XYLO_C6",   "xylophone/C6_ff.flac",                 1.0, "xylophone C6"),
    ("XYLO_G6",   "xylophone/G6_ff.flac",                 1.0, "xylophone G6"),
    ("XYLO_C7",   "xylophone/C7_ff.flac",                 1.0, "xylophone C7"),
    ("TICK",      "kenney/ui/Audio/tick_002.ogg",         0.9, "count-in and score counting"),
    ("GO",        "kenney/ui/Audio/confirmation_001.ogg", 0.8, "start"),
    ("ROUND_OK",  "kenney/ui/Audio/confirmation_002.ogg", 0.7, "you repeated the whole sequence"),
    ("WRONG",     "kenney/ui/Audio/error_006.ogg",        1.0, "wrong pad"),
    ("GAME_OVER", "kenney/digital/zapThreeToneDown.mp3",  0.8, "game over"),
    ("NEW_BEST",  "kenney/digital/zapThreeToneUp.mp3",    0.8, "new high score"),
]


def load(path):
    raw = subprocess.run(["ffmpeg", "-v", "error", "-i", path, "-ac", "1", "-ar", str(RATE),
                          "-f", "s16le", "-"], check=True, capture_output=True).stdout
    return [int.from_bytes(raw[i:i + 2], "little", signed=True) for i in range(0, len(raw), 2)]


def tidy(s, loudness):
    peak = max(1, max(abs(x) for x in s))
    start = next((i for i, x in enumerate(s) if abs(x) > peak * 0.02), 0)
    end = max((i for i, x in enumerate(s) if abs(x) > peak * 0.003), default=len(s) - 1)
    s = s[start:end + 1]
    fade = min(len(s), RATE // 50)                      # 20 ms fade-out
    for i in range(fade):
        s[-fade + i] = int(s[-fade + i] * (1 - i / fade))
    gain = 29000 / peak * loudness
    return [max(-32767, min(32767, int(x * gain))) for x in s]


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    folder = sys.argv[1]
    out = ["// sounds.h - sounds for MemoryLights (made by tools/make_sounds.py)",
           "//",
           "// Effects by Kenney (www.kenney.nl), CC0. Xylophone: FreePats / Versilian",
           "// Community Sample Library, CC0 (freepats.zenvoid.org).",
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
