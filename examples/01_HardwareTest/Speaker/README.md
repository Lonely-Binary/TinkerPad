# Speaker

Plays the notes of a C major scale up and down, forever.

## What you should see

You should hear Do Re Mi Fa Sol La Si Do, then back down. Each note is a sine wave calculated on the board and sent to the amplifier over I²S.

**Pins:** NS4168 amplifier: BCLK 17, LRCLK 16, DIN 18.

## Run it

Open `Speaker.ino` in the Arduino IDE and upload it. The board settings and the full setup steps
are in the [hardware test README](../README.md). Library needed: none.
