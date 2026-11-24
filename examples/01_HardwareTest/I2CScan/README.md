# I2CScan

Finds every chip on the I²C bus and prints its address.

## What you should see

Open the Serial Monitor at 115200 baud. You should see **0x20** (PCF8574, reads the keys) and **0x75** (IP5306, the battery chip; it may be asleep and not answer without a battery).

**Pins:** SDA 9, SCL 8.

## Run it

Open `I2CScan.ino` in the Arduino IDE and upload it. The board settings and the full setup steps
are in the [hardware test README](../README.md). Library needed: none.
