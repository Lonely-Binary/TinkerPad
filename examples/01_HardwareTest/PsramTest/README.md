# PsramTest

Writes and reads back 6 MB of the external PSRAM, 256 KB at a time.

## What you should see

Open the Serial Monitor at 115200 baud. Each 256 KB step prints `ok`, and the end prints **PSRAM test PASSED**. A faulty board shows exactly how far it got instead of just restarting. Set **PSRAM** to **OPI PSRAM**.

**Pins:** the 8 MB PSRAM built into the ESP32-S3 N16R8.

## Run it

Open `PsramTest.ino` in the Arduino IDE and upload it. The board settings and the full setup steps
are in the [hardware test README](../README.md). Library needed: none.
