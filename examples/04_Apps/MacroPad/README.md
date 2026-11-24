# MacroPad

A 16-key Bluetooth shortcut keyboard, set up from a web page. Give every key a shortcut, pair it
with your computer, and press.

## How to use

1. **Power on.** The board makes its own Wi-Fi network:
   - name: `TinkerPad-XXXX` (XXXX is different for every board)
   - password: `12345678`
2. **Join it** with a phone or computer. The settings page usually opens by itself; if not,
   open `http://192.168.4.1`.
3. **Tap a key** on the page and give it a shortcut:
   - on a computer, just press the shortcut on your keyboard;
   - on a phone, pick the modifier keys and the key from the lists.
4. **Pair "TinkerPad"** over Bluetooth on your Mac or PC. Each key now sends its shortcut.

## Using your home Wi-Fi

On the settings page, under Wi-Fi, enter your router's name and password. The board then joins
your router, and the page is at `http://tinkerpad.local`. If it cannot reach the router, it
goes back to its own network by itself. Hold **key 1** (top-left) while powering on to force
its own network.

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
| USB CDC On Boot | **Disabled** on the UART port, **Enabled** on the USB port |

## Files

| File | What it is |
|---|---|
| `MacroPad.ino` | Keys, Bluetooth keyboard, Wi-Fi and the web server |
| `web_page.h` | The settings page |
