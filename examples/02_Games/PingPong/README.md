# PingPong

Table tennis for two players. Put TinkerPad on the table between you: player 1 sits on the
**left** (teal), player 2 on the **right** (orange). Your bat is the column of 4 keys on your
edge, dimly lit in your colour.

## How to play

The game starts as soon as the board is on.

1. "tick, tick, tick": the server's ball blinks, then it is served.
2. The white ball flies across, one key at a time, with a soft trail. You hear it bounce on the
   table when it reaches your half.
3. When it lands on **your** edge column, press that key (same row) quickly to hit it back.
   Wrong key or too late: a point for the other player.
4. Every return makes the ball a little faster. Sometimes it changes row on the way, so watch
   it. Hit very quickly and your return always changes row: a smash that is harder to read.

After each point, your points light up in your half, one key per point. **First to 5 wins.**
Match point plays a tense sound; the winner's half shows a rainbow. Press any key for a new
match. The serve takes turns, and each player's bat has its own pitch, so you can hear who hit.

## Install

**From the browser (no Arduino IDE):** open the [TinkerPad Web Installer](https://lonely-binary.github.io/TinkerPad/) in Chrome or
Edge, plug in the board, click **Install** next to this program and pick the port.

**From the Arduino IDE:** open the `.ino` file in this folder, choose these settings in the
**Tools** menu, and upload. Library needed: **FastLED**.

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

## Sounds

Real table-tennis bat and table recordings, "Ping Pong Sounds" by Aj_ on OpenGameArt (CC0);
other effects by Kenney (www.kenney.nl), CC0. To change them, see `tools/make_sounds.py`.

## Files

| File | What it is |
|---|---|
| `PingPong.ino` | The game |
| `sounds.h` | The sounds, as sample data (generated) |
| `tools/make_sounds.py` | Builds sounds.h from the sound files |
| `tools/preview.wav` | All the sounds in one file, to listen to |
