# Piano

Every key plays a song, and the LEDs dance with the notes.

## What you should see

Press a key: its song starts. Press it again: it stops. Press another key to switch songs. The lit LED follows the pitch (low notes on LED 0, high notes on LED 15) and old notes fade away.

Songs, keys 0–15 row by row: Super Mario Bros, Tetris, Star Wars, Imperial March, Harry Potter, Game of Thrones, Pac-Man, Pink Panther, Never Gonna Give You Up, Happy Birthday, Ode to Joy, Für Elise, Merry Christmas, Silent Night, Nokia Tune, Take On Me. Melodies from https://github.com/robsoncouto/arduino-songs.

**Pins:** Speaker: BCLK 17, LRCLK 16, DIN 18 · Keys: SDA 9, SCL 8 · LEDs: 38.

## Run it

Open `Piano.ino` in the Arduino IDE and upload it. The board settings and the full setup steps
are in the [hardware test README](../README.md). Library needed: **FastLED**.
