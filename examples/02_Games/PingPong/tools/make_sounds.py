#!/usr/bin/env python3
"""Turn sound files into sounds.h for PingPong (and render preview.wav).

Usage:
    python3 make_sounds.py <folder>

<folder> must contain:
    kenney/    the Kenney packs, unzipped (CC0 - https://kenney.nl):
               kenney/ui (Interface Sounds), kenney/digital (Digital SFX)
    pingpong/  "Ping Pong Sounds" by Aj_ (CC0) from OpenGameArt:
               https://opengameart.org/content/ping-pong-sounds
               (pingpongbat.ogg and pingpongboard_0.ogg)

Every sound becomes 22050 Hz, mono, 16-bit, trimmed and normalised.
tools/preview.wav plays them all in a row so you can listen on a computer.
"""

import os
import subprocess
import sys
import wave

RATE = 22050

# Each player's bat is the same recording played at a different speed
# (higher = faster = higher pitch). Must match P1_PITCH / P2_PITCH in the sketch.
P1_PITCH = 1.15
P2_PITCH = 0.87

# (name in the code, file, loudness 0-1, longest seconds, ffmpeg filter, what it is for)
SOUNDS = [
    ("BAT",         "pingpong/pingpongbat.ogg",           1.0, 0.30, "highpass=f=180", "a bat hits the ball (real recording)"),
    ("TABLE",       "pingpong/pingpongboard_0.ogg",       0.7, 0.25, "highpass=f=180", "the ball bounces on the table (real recording)"),
    ("TICK",        "kenney/ui/Audio/tick_002.ogg",       0.9, 1.0,  "",               "count-in before a serve"),
    ("POINT",       "kenney/ui/Audio/confirmation_002.ogg", 0.7, 1.0, "",              "someone scores"),
    ("MISS",        "kenney/ui/Audio/error_004.ogg",      0.6, 1.0,  "",               "the ball got past a player"),
    ("MATCH_POINT", "kenney/digital/lowThreeTone.mp3",    0.7, 1.2,  "",               "one more point wins (tension)"),
    ("WIN",         "kenney/digital/zapThreeToneUp.mp3",  0.7, 1.2,  "",               "a player wins the match"),
    ("START",       "kenney/ui/Audio/confirmation_001.ogg", 0.7, 1.0, "",              "a new match starts"),
]


def load(path, seconds, filt):
    cmd = ["ffmpeg", "-v", "error", "-i", path]
    if filt:
        cmd += ["-af", filt]
    cmd += ["-ac", "1", "-ar", str(RATE), "-t", str(seconds), "-f", "s16le", "-"]
    raw = subprocess.run(cmd, check=True, capture_output=True).stdout
    return [int.from_bytes(raw[i:i + 2], "little", signed=True) for i in range(0, len(raw), 2)]


def tidy(s, loudness):
    peak = max(1, max(abs(x) for x in s))
    start = next((i for i, x in enumerate(s) if abs(x) > peak * 0.02), 0)
    end = max((i for i, x in enumerate(s) if abs(x) > peak * 0.003), default=len(s) - 1)
    s = s[start:end + 1]
    fade = min(len(s) // 2, RATE // 12)                 # up to 80 ms fade-out
    for i in range(fade):
        s[-fade + i] = int(s[-fade + i] * (1 - i / fade))
    gain = 29000 / peak * loudness
    return [max(-32767, min(32767, int(x * gain))) for x in s]


def resample(s, speed):
    """Play a sound faster/slower, like the sketch does (nearest sample)."""
    return [s[int(i * speed)] for i in range(int(len(s) / speed))]


def write_preview(path, sounds):
    gap = [0] * (RATE * 2 // 5)
    order = [resample(sounds["BAT"], P1_PITCH), resample(sounds["TABLE"], P1_PITCH),
             resample(sounds["BAT"], P2_PITCH), resample(sounds["TABLE"], P2_PITCH)]
    order += [sounds[n] for n, *_ in SOUNDS if n not in ("BAT", "TABLE")]
    out = []
    for s in order:
        out += s + gap
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(b"".join(int(x).to_bytes(2, "little", signed=True) for x in out))


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    folder = sys.argv[1]
    here = os.path.dirname(os.path.abspath(__file__))
    out = ["// sounds.h - sounds for PingPong (made by tools/make_sounds.py)",
           "//",
           "// Bat and table: \"Ping Pong Sounds\" by Aj_, CC0 (opengameart.org/content/ping-pong-sounds).",
           "// Other effects by Kenney (www.kenney.nl), CC0.",
           f"// {RATE} Hz, mono, 16-bit. Stored in flash, not RAM.",
           "", "#pragma once", "#include <stdint.h>", "", f"#define SOUND_RATE {RATE}", ""]
    total = 0
    made = {}
    for name, file, loud, seconds, filt, what in SOUNDS:
        s = tidy(load(os.path.join(folder, file), seconds, filt), loud)
        made[name] = s
        total += len(s)
        out.append(f"// {name}: {what} ({file}, {len(s) / RATE:.2f} s)")
        out.append(f"const int16_t SND_{name}[{len(s)}] = {{")
        for i in range(0, len(s), 20):
            out.append("    " + ",".join(map(str, s[i:i + 20])) + ",")
        out.append("};\n")
        print(f"{name:12} {len(s) / RATE:.2f} s  {file}")
    out.append("// Sound numbers, to use in the game: play(BAT) ...")
    out.append("enum Sound { " + ", ".join(n for n, *_ in SOUNDS) + ", SOUND_COUNT };")
    out.append("const int16_t *const SOUND_DATA[SOUND_COUNT] = {" + ", ".join(f"SND_{n}" for n, *_ in SOUNDS) + "};")
    out.append("const uint32_t SOUND_LENGTH[SOUND_COUNT] = {" + ", ".join(f"sizeof(SND_{n}) / 2" for n, *_ in SOUNDS) + "};")
    with open(os.path.join(here, "..", "sounds.h"), "w") as f:
        f.write("\n".join(out) + "\n")
    write_preview(os.path.join(here, "preview.wav"), made)
    print(f"total {total / RATE:.1f} s, {total * 2 / 1024:.0f} KB of flash")
    print("preview.wav: bat+table (player 1), bat+table (player 2), then " +
          ", ".join(n for n, *_ in SOUNDS if n not in ("BAT", "TABLE")))


if __name__ == "__main__":
    main()
