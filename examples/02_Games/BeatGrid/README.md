# BeatGrid

A rhythm game on the 4x4 pads. **The board is the game and the speaker; the phone, tablet or computer is the picture.**

```
 phone / tablet / computer (page only draws)
        ^  WebSocket :81   events: hit, miss, score
        |  http :80        the page itself
 ESP32-S3  Wi-Fi hotspot "BeatGrid-XXXX", no password
   core 1  audio task: synth + band, drives the speaker (master clock)
   core 1  loop: pad scan, judging, LEDs, web
   core 0  Wi-Fi
```

## Play

1. Flash the board (below). Power it from USB.
2. Join Wi-Fi **BeatGrid-XXXX** (open network). The page opens by itself; if it
   doesn't, go to `http://192.168.4.1` in Safari / Chrome.
3. Pick a song and a difficulty, press **START**.
4. A ring closes in on one of the 16 squares. Press that pad on the board
   when the ring lands. Hit it and your note plays; miss it and it doesn't.
5. 10 hits in a row raises the multiplier (up to x4). 20 in a row is FEVER.

No screen? The pads are a screen too: each pad glows brighter as its note comes due.
Idle: top row pads 1-3 start a song, bottom row pads 13-15 pick Easy / Normal / Hard.
**BOOT** button stops a song.

On a computer you can also tap the squares or use the keys
`1 2 3 4 / Q W E R / A S D F / Z X C V`. The sound still comes from the board.

## Flash

Libraries: `FastLED`, and `WebSockets` (by Markus Sattler, 2.7.x).

```bash
arduino-cli compile --fqbn "esp32:esp32:esp32s3:PSRAM=opi,CDCOnBoot=cdc,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB" BeatGrid
arduino-cli upload -p /dev/cu.usbmodemXXXX --fqbn "esp32:esp32:esp32s3:PSRAM=opi,CDCOnBoot=cdc,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB" BeatGrid
```

If the upload says "No serial data received" (it does when ComboPad is on the board):
hold **BOOT**, tap **RESET**, release **BOOT**, run the upload again.

## Files

| File | What |
|---|---|
| `BeatGrid.ino` | Game: chart building, judging, score, LEDs, Wi-Fi, WebSocket |
| `synth.h` | Sound: voices, drums, bass, chords, the sequencer, I2S, the master clock |
| `songs.h` | The three original songs, as patterns. Add a song here |
| `web/index.html` | The page (source). Open it with `?demo` to play without a board |
| `web_page.h` | Generated from the page: `python3 tools/make_page.py` after every page change |

## How the timing works

- The audio sample counter is the one clock. The band is sequenced inside the audio
  task against it, notes are due at fixed song times, and a pad press is judged at
  the song time it was read (minus ~1 ms for the scan).
- Windows (ms either side) Perfect / Good / OK: Easy 70/130/200, Normal 50/100/150, Hard 40/85/125.
- The page asks the board for its clock a few times a second (ping/pong, keeps the
  quickest round trip) and draws the rings from that. Wi-Fi delay can only make the
  picture late, never change a judgement. **Picture timing** in the menu nudges the
  rings earlier or later in 10 ms steps (kept on that device).
- Difficulty chooses which melody notes become targets: Easy the beats, Normal the
  eighth notes, Hard all of them. Higher notes sit on higher rows, so the tune has a
  shape under your hand.

## Not verified yet

Only built and tried in a browser (demo mode). Not yet run on the board, so these need an
ear and a finger:

- Does Wi-Fi traffic click the speaker? (audio is on core 1, Wi-Fi on core 0)
- Do the pad / LED numbers match the carrier? Fix `KEY_TO_PAD` / `PAD_TO_LED` at the top of `BeatGrid.ino`.
- Loudness: `MASTER_GAIN` in `synth.h`. Does the speaker reproduce the bass notes?
- `AUDIO_LATENCY_FRAMES` in `synth.h`: if hits feel early or late against the sound, tune it.
- iPhone: open in Safari, not the captive-portal pop-up. No screen-wake on plain http, so turn Auto-Lock off.
