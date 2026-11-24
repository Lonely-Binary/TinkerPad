# LedStrip

The simplest LED test: all 16 key LEDs.

## What you should see

All red for 1 s, all green, all blue, then a white light sweeps from LED 0 to LED 15, forever. Brightness is kept low on purpose: 16 LEDs at full white draw more than a laptop USB port gives.

**Pins:** WS2812B on 38.

## Run it

Open `LedStrip.ino` in the Arduino IDE and upload it. The board settings and the full setup steps
are in the [hardware test README](../README.md). Library needed: **FastLED**.
