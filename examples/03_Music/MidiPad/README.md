# MidiPad

Two things in one: **USB MIDI drum pads** when it is plugged into a computer, and a
**follow-the-lights music game** when it runs on its own.

## Drum pads (plugged into a computer)

Plug the port marked **USB** into a Mac or PC. It shows up as a MIDI device (named
"ESP32S3_DEV" on a Mac). Open GarageBand, Logic, Ableton or any music app, make a drum track
and play. General MIDI drums, channel 10:

```
Crash        Crash 2     Ride        Ride bell     <- top row
Floor tom    Low tom     Mid tom     High tom
Soft snare   China       Pedal hat   Kick 2
KICK         SNARE       Closed hat  Open hat      <- bottom row
```

## Music game (battery or phone charger)

The keypad is the menu:

```
[song 1]  [song 2]   [song 3]   [song 4]     green  = everyday songs
[song 5]  [song 6]   [song 7]   [song 8]     blue   = classics
[song 9]  [song 10]  [song 11]  [song 12]    orange = games and films
[FREE]    [MODE]     [LEVEL]    [VOLUME]
```

| Key | What it does |
|---|---|
| A song, once | Plays the start of the song (the key blinks) |
| The same song again | Starts it: a count-in, then follow the lights |
| FREE | Free drumming |
| MODE | Blue = learn (the song waits for you), red = challenge |
| LEVEL | Green = easy (4 pads), yellow = normal (8), red = hard (16) |
| VOLUME | Three levels |

While playing, the pad to press lights up and gets brighter until it is time. Each press plays
the next note of the melody, and the drums play along. Flash colours: green = perfect,
yellow = good, red = wrong or missed. At the end the pads fill up from the bottom: the more,
the better. Hold the **two top corner keys** for 1.5 s to go back to the menu at any time.

## Install

**From the browser (no Arduino IDE):** open the [TinkerPad Web Installer](https://lonely-binary.github.io/TinkerPad/) in Chrome or
Edge, plug in the board, click **Install** next to this program and pick the port.

**From the Arduino IDE:** open the `.ino` file in this folder, choose these settings in the
**Tools** menu, and upload. Library needed: **FastLED**. USB MIDI comes with the ESP32 core.

| Setting | Value |
|---|---|
| Board | ESP32S3 Dev Module |
| Flash Size | 16MB (128Mb) |
| PSRAM | OPI PSRAM |
| Partition Scheme | 16M Flash (3MB APP/9.9MB FATFS) |
| **USB Mode** | **USB-OTG (TinyUSB)** |
| USB CDC On Boot | Enabled |
| **Upload Mode** | **USB-OTG CDC (TinyUSB)** |

This sketch takes over the port marked **USB**. If an upload fails, hold **BOOT**, tap
**RESET**, let go of BOOT and upload again, or upload through the **UART** port.

## Sounds and songs

Drum sounds: MuldjordKit by Lars Muldjord, FreePats version, Creative Commons Attribution 4.0
(https://freepats.zenvoid.org/Percussion/acoustic-drum-kit.html). Melodies:
https://github.com/robsoncouto/arduino-songs.

## Files

| File | What it is |
|---|---|
| `MidiPad.ino` | Drum pads, menu and MIDI |
| `game.h` | Turns a song into notes to play, and the pad for each one |
| `songs.h` | The 12 songs |
| `sound.h` | Everything the speaker plays: the drum recordings and a marimba-like melody voice |
| `drum_samples.h` | The drum kit, as sample data (generated) |
| `tools/make_samples.py` | Builds drum_samples.h from the sound files |
| `tools/preview.wav` | All the drums in one file, to listen to |
