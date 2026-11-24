<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="docs/images/lockup-dark.svg">
    <img src="docs/images/lockup-light.svg" alt="Lonely Binary" height="48">
  </picture>
</p>

<h1 align="center">TinkerPad</h1>

<p align="center">
  An ESP32-S3 board with 16 lit keys, a microphone and a speaker.<br>
  One board, many projects: games, music, a voice message board, a USB keyboard.
</p>

> [!CAUTION]
> **Check the ESP32-S3 board before you power it on. Plugged in wrong, it can burn.**
>
> - **Direction:** the two USB-C ports on the ESP32-S3 board must face the **left edge** of
>   TinkerPad, as printed on the back: *"ALIGN USB TO THE LEFT SIDE"*.
> - **Alignment:** every pin must go into its own socket hole. Make sure the board is not
>   shifted by one pin, and that no pin is left hanging outside the socket.
> - Only plug in USB or the battery **after** you have checked both.


<p align="center">
  <img src="docs/images/tinkerpad-insert-guide.png" alt="How to plug the ESP32-S3 board into TinkerPad: USB-C ports face left, line it up with the outline, every pin in its own hole" width="620">
</p>

## Examples

**No Arduino IDE?** Install ready-made programs from Chrome or Edge with the
[TinkerPad Web Installer](https://lonely-binary.github.io/TinkerPad/): plug in the board,
click Install, pick the port.

To build them yourself, open any folder in the Arduino IDE and upload it. Each one is
independent. Everything except the hardware tests is also in the Web Installer.

| Folder | What is inside |
|---|---|
| [`01_HardwareTest`](examples/01_HardwareTest) | One small sketch per part: keys, LEDs, speaker, microphone, PSRAM, I²C. **Start here**: run them first to check that every part of your board works |
| **Games** | |
| [`MemoryLights`](examples/02_Games/MemoryLights) | Watch the lights, then play them back. The classic memory game |
| [`WhackAMole`](examples/02_Games/WhackAMole) | Hit the moles before they hide again |
| [`PingPong`](examples/02_Games/PingPong) | Table tennis for two players, one column of keys each |
| [`SoundMatch`](examples/02_Games/SoundMatch) | A memory game for your ears: 16 keys hide 8 pairs of sounds |
| [`BeatGrid`](examples/02_Games/BeatGrid) | A rhythm game on the keys, with the score shown on your phone or computer |
| **Music** | |
| [`MidiPad`](examples/03_Music/MidiPad) | USB MIDI drum pads for GarageBand and other music apps, plus a follow-the-lights game |
| [`UkuleleTeacher`](examples/03_Music/UkuleleTeacher) | Learn a song on the ukulele, one phrase at a time |
| **Apps** | |
| [`VoiceBoard`](examples/04_Apps/VoiceBoard) | A family voice message board. Twelve people, one key each. Several boards keep each other in sync over ESP-NOW or Wi-Fi |
| [`MacroPad`](examples/04_Apps/MacroPad) | A 16-key Bluetooth shortcut keyboard, set up from a web page |
| [`ComboPad`](examples/04_Apps/ComboPad) | A fighting-game combo keyboard: every key plays a whole combo over USB |

Most games, MidiPad, UkuleleTeacher and ComboPad need **USB Mode: USB-OTG (TinyUSB)** in
the Arduino IDE. Each sketch lists its settings at the top of the `.ino` file.

## What is on the board

- **ESP32-S3 N16R8**: 16 MB flash, 8 MB PSRAM, Wi-Fi and Bluetooth LE
- **16 mechanical keys**, each with its own **RGB LED** under it
- **Microphone** (PDM) and **speaker output** (I²S amplifier)
- **Battery connector** with a charger, and a power USB-C that charges it
- **Two expansion ports** and a **display connector**
- **Two USB-C ports** on the ESP32-S3 board for uploading: UART and native USB

<p align="center">
  <img src="docs/images/tinkerpad-top-annotated.png" alt="TinkerPad, top view with numbered parts" width="560">
</p>

| # | Part | Details |
|---|---|---|
| 1 | Battery | PH2.0 connector, 1-cell Li-ion |
| 2 | Speaker | 1.25 mm connector, I²S amplifier |
| 3 | USB-C (power) | 5 V in, charges the battery. No data |
| 4 | Display connector | FPC 15-pin, 1.0 mm |
| 5 | USB-C (UART) | Upload and serial monitor |
| 6 | BOOT button | Hold it while plugging in if an upload will not start |
| 7 | USB-C (native USB) | Upload, serial, or act as a USB keyboard / MIDI device |
| 8 | RESET button | Restarts the program |
| 9 | Status RGB LED | One WS2812 on IO48 |
| 10 | ESP32-S3 module | |
| 11 | Port A | GND · 3V3 · IO1 · IO15 |
| 12 | Port B | GND · 3V3 · IO14 · IO48 · IO47 · IO21 |
| 13 | Microphone | PDM |
| 14 | Keys and LEDs | 16 keys, 16 WS2812B LEDs |

## Pins

| Part | Pins |
|---|---|
| Keys (PCF8574, I²C address `0x20`) | SDA **9**, SCL **8** |
| Key LEDs (16 × WS2812B) | **38** |
| Speaker (I²S) | DIN **18**, BCLK **17**, LRCLK **16** |
| Microphone (PDM) | DATA **39**, CLK **40** |
| Status LED (WS2812) | **48** |
| Battery voltage | **3**, through a 100k/100k divider. Short the solder pads on the back to connect it |
| Battery charger (IP5306, I²C `0x75`) | shares SDA 9 / SCL 8 |
| Display (FPC 15-pin) | BL 41, RESET 42, DC 2, CS 10, MOSI 11, MISO 13, SCK 12, D2 1, D3 15, D4 7, D5 4, D6 6, D7 5 |

Some pins are shared. Port A uses IO1 and IO15, which are also display D2 and D3.
Port B's IO48 is also the status LED.

**Key numbering.** Keys, key LEDs and positions all use the same number: 0 is the
top-left key, 3 the top-right, 15 the bottom-right. The LED under key *n* is LED *n*.

## Getting started

1. Install the **Arduino IDE 2.x** and **esp32 by Espressif Systems** from the Boards Manager.
   Step-by-step instructions are in
   [`examples/01_HardwareTest/README.md`](examples/01_HardwareTest/README.md).
2. Plug a USB cable into **one** of the two USB-C ports on the ESP32-S3 board (5 or 7 above).
3. In the **Tools** menu:

   | Setting | Value |
   |---|---|
   | Board | ESP32S3 Dev Module |
   | Flash Size | 16MB (128Mb) |
   | PSRAM | OPI PSRAM |
   | Partition Scheme | 16M Flash (3MB APP/9.9MB FATFS) |
   | USB CDC On Boot | **Disabled** on the UART port, **Enabled** on the USB port |

4. Install **FastLED** from the Library Manager. Most examples use it for the LEDs.
5. Open `examples/01_HardwareTest/KeyMatrix`, upload, open the Serial Monitor at
   115200 baud and press some keys.

If the Serial Monitor stays empty, the **USB CDC On Boot** setting does not match the
port you plugged into.

## Not tested yet

The battery voltage reading, the IP5306 charger status and the display connector are on
the board but do not have working examples yet.

## License

[MIT](LICENSE) © 2026 Lonely Binary

---

<p align="center"><i>From Zeros to Heroes, One Bit at a Time.</i></p>
