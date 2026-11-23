# 01_HardwareTest - one simple test sketch per part

Board: **ESP32-S3 N16R8** (16 MB flash, 8 MB PSRAM) on the TinkerPad board.

Each folder is one small sketch that tests one part of the board. They are
independent: open one, upload it, see it work.

| Folder | Part | What it does |
|---|---|---|
| `I2CScan` | I2C bus | Prints the address of every chip that answers |
| `KeyMatrix` | 16 keys | Prints which keys (0-15) are pressed |
| `LedStrip` | 16 RGB LEDs | Red, green, blue (1 s each), then a white sweep |
| `Speaker` | Speaker | Plays a 440 Hz beep |
| `Microphone` | Microphone | Prints a loudness bar |
| `AudioLoopback` | Mic + speaker | Hold any key to record (red LED), release to play back (blue LED) |
| `PsramTest` | 8 MB PSRAM | Writes and reads back 6 MB, prints how far it got |
| `Piano` | Speaker + LEDs | Plays a melody; the lit LED follows the pitch |

Open the **Serial Monitor** (115200 baud) to see the text output of
`I2CScan`, `KeyMatrix`, `Microphone` and `PsramTest`.

---

## 1. Install ESP32-S3 support in Arduino IDE

1. Install Arduino IDE 2.x from https://www.arduino.cc/en/software
2. Open **Settings** (Windows/Linux: File > Preferences, Mac: Arduino IDE > Settings).
3. In **Additional boards manager URLs**, paste:
   ```
   https://espressif.github.io/arduino-esp32/package_esp32_index.json
   ```
4. Open **Tools > Board > Boards Manager**, search for `esp32`, and install
   **esp32 by Espressif Systems**. (It is a big download. Wait for it to finish.)

## 2. Choose the board and the USB port

The ESP32-S3 board has **two Type-C sockets**. Pick one, then use the matching
settings. Only the **USB CDC On Boot** setting differs.

| | Socket marked **UART** (or COM) | Socket marked **USB** |
|---|---|---|
| How it works | A USB-to-serial chip on the board | The ESP32-S3's own USB |
| Port name looks like | `wchusbserial...` / `COM3` | `usbmodem...` / `COM5` |
| **USB CDC On Boot** | **Disabled** | **Enabled** |

Settings in the **Tools** menu (same for both sockets, except the row above):

| Setting | Value |
|---|---|
| Board | **ESP32S3 Dev Module** |
| Flash Size | **16MB (128Mb)** |
| PSRAM | **OPI PSRAM** |
| Partition Scheme | **16M Flash (3MB APP/9.9MB FATFS)** |
| USB CDC On Boot | **Disabled** (UART socket) or **Enabled** (USB socket) |
| Port | the port that appears when you plug the board in |

Then press **Upload** (the arrow button).

**If the Serial Monitor is empty**, the *USB CDC On Boot* setting does not match
the socket you used. Change it and upload again.

**If upload fails**, hold the **BOOT** button, tap **RESET**, release BOOT, and
try again.

## 3. Libraries

Most sketches need **no extra library**: `Wire` and the I2S driver come with
the ESP32 support you installed in step 1.

Only two sketches need a library:

| Sketch | Library to install |
|---|---|
| `LedStrip`, `AudioLoopback`, `Piano` | **FastLED** |

To install a library:

1. Open **Tools > Manage Libraries...** (or click the books icon on the left).
2. Type the library name in the search box.
3. Click **Install**.

## Tips

- **LEDs and USB power:** with no battery, the LEDs draw power from USB. Sixteen
  LEDs at full white is too much for a laptop port, so `LedStrip` uses a low
  brightness (`BRIGHTNESS 40`). If the USB port keeps disconnecting, lower it.
- **AudioLoopback:** records into PSRAM (max 10 s), so set **PSRAM** to **OPI PSRAM**.
