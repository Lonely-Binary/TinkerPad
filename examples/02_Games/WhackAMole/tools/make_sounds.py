#!/usr/bin/env python3
"""Turn sound-effect files into sounds.h for WhackAMole.

Usage:
    python3 make_sounds.py <folder that holds the Kenney packs>

The folder must contain the three Kenney packs, unzipped (all CC0, free for
any use, credit not required - https://kenney.nl):
    ui/       Interface Sounds  (kenney_interfaceSounds.zip)
    digital/  Digital SFX       (Digital_SFX_Set.zip)
    rpg/      RPG Sounds        (RPGsounds_Kenney.zip)
All three are on opengameart.org. To change a sound, change the list below
and run this again. Any format ffmpeg reads works (OGG, MP3, WAV...).

Every sound becomes 22050 Hz, mono, 16-bit, trimmed and normalised.
"""

import os
import subprocess
import sys

RATE = 22050

# (name in the code, file, loudness 0-1, what it is for)
SOUNDS = [
    ("POP",       "ui/Audio/pluck_001.ogg",        0.6, "a mole pops up"),
    ("WHACK",     "rpg/OGG/chop.ogg",              1.0, "hitting a mole"),
    ("POINT",     "digital/pepSound1.mp3",         0.5, "+1 point (played faster as the combo grows)"),
    ("COINS",     "rpg/OGG/handleCoins.ogg",       0.9, "hitting a golden mole"),
    ("BOOM",      "digital/lowRandom.mp3",         1.0, "hitting a bomb"),
    ("ESCAPE",    "ui/Audio/minimize_001.ogg",     0.5, "a mole got away"),
    ("EMPTY",     "ui/Audio/tick_002.ogg",         0.5, "hitting an empty hole"),
    ("TICK",      "ui/Audio/tick_002.ogg",         0.9, "count-in and the last seconds"),
    ("GO",        "ui/Audio/confirmation_001.ogg", 0.9, "start"),
    ("SPEED_UP",  "digital/powerUp1.mp3",          0.6, "the game gets faster"),
    ("GAME_OVER", "digital/zapThreeToneDown.mp3",  0.8, "time is up"),
    ("NEW_BEST",  "digital/zapThreeToneUp.mp3",    0.8, "new high score"),
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
    out = ["// sounds.h - sound effects for WhackAMole (made by tools/make_sounds.py)",
           "//",
           "// Sounds by Kenney (www.kenney.nl), Creative Commons Zero (CC0).",
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
