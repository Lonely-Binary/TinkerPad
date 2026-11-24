#!/usr/bin/env python3
"""Turn sound files into sounds.h for SoundMatch.

Usage:
    python3 make_sounds.py <folder>

<folder> must contain two sub-folders:
    commons/   real recordings from Wikimedia Commons (all public domain or CC0):
               Meow_of_a_Siamese_cat_-_freemaster2.wav   (CC0, freemaster2)
               George_vuf_1996.ogg                        (public domain, Broadbeer)
               Small_rooster_crowing.ogg                  (public domain, alys / PDSounds)
               Sheep_bleating.ogg                         (public domain, earthcalling / PDSounds)
               Wiehern.ogg                                (public domain, Hu.)
               Elephant_voice_-_trumpeting.ogg            (CC0)
               Doorbell-classic-dingdong.ogg              (public domain)
               Download each from https://commons.wikimedia.org/wiki/File:<name>
    kenney/    the Kenney packs, unzipped (CC0 - https://kenney.nl):
               kenney/ui (Interface Sounds), kenney/digital (Digital SFX), kenney/rpg (RPG Sounds)

The first 8 sounds are the "cards" (each one is on two keys). To use other
sounds, change the list below and run this again.
Every sound becomes 22050 Hz, mono, 16-bit, trimmed and normalised.
"""

import os
import subprocess
import sys

RATE = 22050

# (name in the code, file, loudness 0-1, what it is for, start second, seconds, times)
# "start" and "seconds" cut the best part out of a longer recording;
# "times" repeats it (one dog bark -> "woof woof").
SOUNDS = [
    # --- the 8 cards ---
    ("CAT",      "commons/Meow_of_a_Siamese_cat_-_freemaster2.wav", 0.9, "card: cat meowing",     0.20, 1.0, 1),
    ("DOG",      "commons/George_vuf_1996.ogg",                     0.9, "card: dog barking",     0.00, 0.34, 2),
    ("ROOSTER",  "commons/Small_rooster_crowing.ogg",               0.9, "card: rooster crowing", 2.45, 1.7, 1),
    ("SHEEP",    "commons/Sheep_bleating.ogg",                      0.9, "card: sheep baaing",    0.50, 1.25, 1),
    ("HORSE",    "commons/Wiehern.ogg",                             0.9, "card: horse neighing",  0.05, 1.9, 1),
    ("ELEPHANT", "commons/Elephant_voice_-_trumpeting.ogg",         0.9, "card: elephant",        0.05, 1.1, 1),
    ("DOORBELL", "commons/Doorbell-classic-dingdong.ogg",           0.8, "card: doorbell ding-dong", 0.05, 1.5, 1),
    ("COINS",    "kenney/rpg/OGG/handleCoins.ogg",                  0.9, "card: coins",           0, 1.2, 1),
    # --- game sounds ---
    ("GO",       "kenney/ui/Audio/confirmation_001.ogg", 0.7, "start",              0, 1.2, 1),
    ("MATCH",    "kenney/ui/Audio/confirmation_002.ogg", 0.7, "a pair found",       0, 1.2, 1),
    ("NO_MATCH", "kenney/ui/Audio/back_002.ogg",         0.6, "not a pair",         0, 1.2, 1),
    ("CHEER",    "kenney/digital/zapThreeToneUp.mp3",    0.7, "all pairs found",    0, 1.2, 1),
    ("TICK",     "kenney/ui/Audio/tick_002.ogg",         0.9, "counting the stars", 0, 1.2, 1),
]


def load(path, start, seconds):
    raw = subprocess.run(["ffmpeg", "-v", "error", "-ss", str(start), "-i", path, "-ac", "1", "-ar", str(RATE),
                          "-t", str(seconds), "-f", "s16le", "-"], check=True, capture_output=True).stdout
    return [int.from_bytes(raw[i:i + 2], "little", signed=True) for i in range(0, len(raw), 2)]


def tidy(s, loudness):
    peak = max(1, max(abs(x) for x in s))
    start = next((i for i, x in enumerate(s) if abs(x) > peak * 0.02), 0)
    end = max((i for i, x in enumerate(s) if abs(x) > peak * 0.003), default=len(s) - 1)
    s = s[start:end + 1]
    fade = min(len(s), RATE // 12)                      # 80 ms fade-out
    for i in range(fade):
        s[-fade + i] = int(s[-fade + i] * (1 - i / fade))
    gain = 29000 / peak * loudness
    return [max(-32767, min(32767, int(x * gain))) for x in s]


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    folder = sys.argv[1]
    out = ["// sounds.h - sounds for SoundMatch (made by tools/make_sounds.py)",
           "//",
           "// Animals and doorbell: Wikimedia Commons, public domain / CC0 (see tools/make_sounds.py).",
           "// Effects by Kenney (www.kenney.nl), CC0.",
           f"// {RATE} Hz, mono, 16-bit. Stored in flash, not RAM.",
           "", "#pragma once", "#include <stdint.h>", "", f"#define SOUND_RATE {RATE}", ""]
    total = 0
    for name, file, loud, what, start, seconds, times in SOUNDS:
        one = tidy(load(os.path.join(folder, file), start, seconds), loud)
        gap = [0] * (RATE // 12)                        # 80 ms between repeats
        s = one
        for _ in range(times - 1):
            s = s + gap + one
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
