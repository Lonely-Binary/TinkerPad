// Piano - every key plays a song, and the LEDs dance with the notes
// Board: ESP32-S3 N16R8 (16 MB flash, 8 MB PSRAM)
// Setup guide: see ../README.md
//
// Press a key        -> its song starts
// Press the same key -> the song stops
// Press another key  -> switch to that song
//
// The 16 LEDs show the music: the LED position follows the pitch (low note =
// LED 0, high note = LED 15) and the colour changes with the pitch and over
// time. Old notes fade away and leave a trail.
//
// Keys 0-15, row by row, play the songs listed in songs.h:
//    0 Super Mario Bros      1 Tetris             2 Star Wars        3 Imperial March
//    4 Harry Potter          5 Game of Thrones    6 Pac-Man          7 Pink Panther
//    8 Never Gonna Give...   9 Happy Birthday    10 Ode to Joy      11 Fur Elise
//   12 Merry Christmas      13 Silent Night      14 Nokia Tune      15 Take On Me
//
// Needs the "FastLED" library. Songs: github.com/robsoncouto/arduino-songs

#include <Wire.h>
#include <FastLED.h>
#include <math.h>
#include <driver/i2s_std.h>
#include "songs.h"

// Keys (PCF8574 on I2C)
#define PIN_SDA       9
#define PIN_SCL       8
#define PCF8574_ADDR  0x20

// Speaker (I2S)
#define PIN_BCLK      17
#define PIN_LRCLK     16
#define PIN_DIN       18
#define SAMPLE_RATE   16000
#define CHUNK         256       // samples made at a time (16 ms)
#define VOLUME        0.3       // 0.0 - 1.0

// The small speaker sounds bad (harsh, distorted) when the pitch is too high
// or too low. Notes outside this range are moved up or down by whole octaves.
#define MAX_FREQ      1100      // Hz
#define MIN_FREQ      220       // Hz

// LEDs
#define PIN_LED       38
#define NUM_LEDS      16
#define BRIGHTNESS    40

i2s_chan_handle_t tx;           // speaker channel
CRGB leds[NUM_LEDS];            // the colour of every LED
int16_t buffer[CHUNK * 2];      // sound waiting to be sent (left + right)

// What is playing right now
int   current = -1;             // song number, or -1 when stopped
int   position = 0;             // where we are in the melody array
float frequency = 0;            // pitch of the current note (0 = rest)
long  noteLength = 0;           // length of the current note, in samples
long  noteTime = 0;             // how many samples of this note are done
float angle = 0;                // position inside the sine wave
int   noteCount = 0;            // notes played so far (used for colour)

// Returns the number (0-15) of a pressed key, or -1 if no key is pressed
int findKey() {
    for (int row = 0; row < 4; row++) {
        // Pull this row low, leave everything else high
        Wire.beginTransmission(PCF8574_ADDR);
        Wire.write(0xF0 | (0x0F & ~(1 << row)));
        Wire.endTransmission();

        Wire.requestFrom(PCF8574_ADDR, 1);
        uint8_t pins = Wire.read();
        for (int col = 0; col < 4; col++) {
            if ((pins & (1 << (col + 4))) == 0) {
                return row * 4 + col;
            }
        }
    }
    return -1;
}

// Light the LED that matches a note, in a colour that matches its pitch
void showNote(float freq) {
    if (freq == 0) return;                                  // a rest: no light

    float semitone = 12 * log2f(freq / 440.0) + 69;         // piano key number
    int led = (semitone - 48) * 15 / 40;                    // 0 (low) - 15 (high)
    led = constrain(led, 0, NUM_LEDS - 1);

    uint8_t hue = led * 12 + noteCount * 4;                 // colour changes
    leds[led] = CHSV(hue, 255, 255);
    if (led > 0)            leds[led - 1] += CHSV(hue, 255, 80);   // soft glow
    if (led < NUM_LEDS - 1) leds[led + 1] += CHSV(hue, 255, 80);
}

// Get the next note of the song. Returns false when the song is over.
bool nextNote() {
    const Song &song = songs[current];
    if (position >= song.length) return false;

    frequency  = song.melody[position] * pow(2, song.octave);   // song-wide shift

    // Keep the note inside the speaker's comfortable range (rests stay 0)
    while (frequency > MAX_FREQ) frequency /= 2;
    while (frequency > 0 && frequency < MIN_FREQ) frequency *= 2;
    int divider = song.melody[position + 1];
    position += 2;

    // A whole note lasts (60000 * 4 / tempo) milliseconds.
    long ms = 60000L * 4 / song.tempo / abs(divider);
    if (divider < 0) ms = ms * 3 / 2;                       // dotted note

    noteLength = ms * SAMPLE_RATE / 1000;
    noteTime = 0;
    noteCount++;
    showNote(frequency);
    return true;
}

void stopSong() {
    current = -1;                                           // speaker goes quiet by itself
    FastLED.clear(true);                                    // LEDs off
}

void startSong(int number) {
    current = number;
    position = 0;
    noteCount = 0;
    noteLength = 0;
    noteTime = 0;
    FastLED.clear();
    Serial.printf("Key %d: %s\n", number, songs[number].name);
}

// Make one small piece of sound, send it, and update the LEDs
void playChunk() {
    bool finished = false;

    for (int i = 0; i < CHUNK; i++) {
        if (noteTime >= noteLength && !finished) {
            if (!nextNote()) finished = true;
        }

        int16_t sample = 0;
        if (!finished) {
            // Play the first 90% of the note, then a short gap
            long soundLength = noteLength * 9 / 10;
            if (frequency > 0 && noteTime < soundLength) {
                // Fade in and out over 80 samples so there are no clicks
                float fade = min(min(noteTime, soundLength - noteTime) / 80.0, 1.0);
                sample = sinf(angle) * VOLUME * fade * 30000;
                angle += 2 * M_PI * frequency / SAMPLE_RATE;
                if (angle > 2 * M_PI) angle -= 2 * M_PI;
            }
            noteTime++;
        }
        buffer[i * 2]     = sample;                         // left
        buffer[i * 2 + 1] = sample;                         // right
    }

    size_t written;
    i2s_channel_write(tx, buffer, sizeof(buffer), &written, portMAX_DELAY);

    fadeToBlackBy(leds, NUM_LEDS, 25);                      // old notes fade
    FastLED.show();

    if (finished) stopSong();
}

void setup() {
    Serial.begin(115200);
    delay(600);

    Wire.begin(PIN_SDA, PIN_SCL);
    Wire.setTimeOut(5);         // never wait more than 5 ms for the key chip

    FastLED.addLeds<WS2812B, PIN_LED, GRB>(leds, NUM_LEDS);
    FastLED.setBrightness(BRIGHTNESS);
    FastLED.clear(true);

    // Speaker: I2S, 16 kHz, 16 bit, stereo
    i2s_chan_config_t chan = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
    // auto_clear: when we have no sound to send, the speaker plays silence.
    // Without it, the hardware keeps repeating old sound ("beep beep beep").
    chan.auto_clear = true;
    i2s_new_channel(&chan, &tx, nullptr);
    i2s_std_config_t cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                        I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = (gpio_num_t)PIN_BCLK,
            .ws   = (gpio_num_t)PIN_LRCLK,
            .dout = (gpio_num_t)PIN_DIN,
            .din  = I2S_GPIO_UNUSED,
            .invert_flags = {false, false, false},
        },
    };
    i2s_channel_init_std_mode(tx, &cfg);
    i2s_channel_enable(tx);

    Serial.println("Press a key to play a song. Press it again to stop.");
}

void loop() {
    static int lastKey = -1;
    static unsigned long releaseTime = 0;

    // React only at the moment a key goes down (not while it is held).
    // A mechanical key "bounces" (flickers on/off for a few ms), so a press
    // only counts if the keys have been released for at least 100 ms.
    int key = findKey();
    if (key < 0 && lastKey >= 0) {
        releaseTime = millis();
    }
    if (key >= 0 && lastKey < 0 && millis() - releaseTime > 100) {
        if (key == current) {
            stopSong();                 // same key again: stop
            Serial.println("Stopped");
        } else {
            startSong(key);             // new key: start (or switch) song
        }
    }
    lastKey = key;

    if (current >= 0) {
        playChunk();
    } else {
        delay(10);
    }
}
