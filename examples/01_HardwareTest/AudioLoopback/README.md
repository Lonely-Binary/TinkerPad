# AudioLoopback

Hold any key to record, let go to hear it back. Tests the microphone and the speaker together.

## What you should see

While you hold a key its LED is **red** (recording). When you let go it turns **blue** (playing back), then off. Up to 10 seconds, stored in PSRAM, so set **PSRAM** to **OPI PSRAM**.

**Pins:** Microphone: CLK 40, DATA 39 · Speaker: BCLK 17, LRCLK 16, DIN 18 · Keys: PCF8574 at 0x20 (SDA 9, SCL 8) · LEDs: 38.

## Run it

Open `AudioLoopback.ino` in the Arduino IDE and upload it. The board settings and the full setup steps
are in the [hardware test README](../README.md). Library needed: **FastLED**.
