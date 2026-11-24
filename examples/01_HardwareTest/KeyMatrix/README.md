# KeyMatrix

Shows which of the 16 keys are pressed.

## What you should see

Open the Serial Monitor at 115200 baud and press some keys. Keys are numbered 0 to 15, row by row: 0 is top-left, 15 is bottom-right.

**Pins:** PCF8574 at 0x20 (SDA 9, SCL 8): pins 0–3 are the rows, 4–7 the columns.

## Run it

Open `KeyMatrix.ino` in the Arduino IDE and upload it. The board settings and the full setup steps
are in the [hardware test README](../README.md). Library needed: none.
