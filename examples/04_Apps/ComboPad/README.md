# ComboPad

A 16-key fighting-game combo keyboard for games like King of Fighters and Street Fighter.
Every key plays a whole combo as a USB keyboard, frame by frame at 60 frames per second.

Plug the port marked **USB** into the computer that runs the game. Press a key: the combo plays.
Press it again: it stops.

> Meant for offline play, training and emulators. Most online modes do not allow macros.

## Mirror layout

On by default. The left two columns are for when your character faces **right**; the right two
columns play the same combos mirrored, for when your character faces **left**:

```
[1] [2] | [2'] [1']
[3] [4] | [4'] [3']
[5] [6] | [6'] [5']
[7] [8] | [8'] [7']
```

## Setting up the combos

1. Wi-Fi turns on for 10 minutes every time the board starts. Later, hold the **BOOT** button
   for 3 seconds to turn it on again. Key 1 blinks blue while Wi-Fi is on.
2. Join the Wi-Fi network `ComboPad-XXXX` (open, no password).
3. Open `http://192.168.4.1`, then pick a game, a character and a combo, or paste your own
   combo notation.

After 10 minutes Wi-Fi turns off by itself, which gives the best timing while you play.

## Install

**From the browser (no Arduino IDE):** open the [TinkerPad Web Installer](https://lonely-binary.github.io/TinkerPad/) in Chrome or
Edge, plug in the board, click **Install** next to this program and pick the port.

**From the Arduino IDE:** open the `.ino` file in this folder, choose these settings in the
**Tools** menu, and upload. Library needed: **FastLED**. Everything else comes with the ESP32 core.

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

## Files

| File | What it is |
|---|---|
| `ComboPad.ino` | Keys, the USB keyboard, combo playback, Wi-Fi |
| `web_page.h` | The combo set-up page |
