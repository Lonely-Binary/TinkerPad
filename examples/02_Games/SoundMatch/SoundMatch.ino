// SoundMatch - a memory game you play with your ears
// Board: ESP32-S3 N16R8 (16 MB flash, 8 MB PSRAM)
// Setup guide: see ../../01_HardwareTest/README.md
//
// 16 keys hide 8 pairs of sounds: a cat, a dog, a rooster, a sheep, a horse,
// an elephant, a doorbell and coins. Find the pairs!
//
// How to play (the game starts as soon as the board is on)
//   1. Press a key: you hear its sound and see its colour. Press a second key.
//      Same sound -> a "ding", and both keys stay lit.
//      Different  -> both go dark again. Remember where they were!
//   2. Find all 8 pairs in as few tries as you can.
//
// Stars at the end (rows lighting up gold from the bottom):
//   3 stars = 14 tries or fewer, 2 stars = 20 or fewer, otherwise 1 star.
// The fewest tries are saved; beating it shows a rainbow.
// Press any key to play again (the cards are shuffled every game).
//
// Arduino IDE settings (same as the other TinkerPad games):
//   Tools > USB Mode:        USB-OTG (TinyUSB)
//   Tools > USB CDC On Boot: Enabled
//   Tools > Upload Mode:     USB-OTG CDC (TinyUSB)
// If an upload fails: hold BOOT, tap RESET, release BOOT, upload again.
// Needs the "FastLED" library.
// Sounds: real animal and doorbell recordings from Wikimedia Commons (public
// domain / CC0) and Kenney (www.kenney.nl, CC0). See tools/make_sounds.py.
// To use your own sounds (animals, voices...) see tools/make_sounds.py.

#include <Wire.h>
#include <FastLED.h>
#include <Preferences.h>
#include <driver/i2s_std.h>
#include "sounds.h"

#define PIN_SDA       9
#define PIN_SCL       8
#define PCF8574_ADDR  0x20
#define PIN_LED       38
#define NUM_PADS      16
#define BRIGHTNESS    60

#define PIN_BCLK      17            // speaker (NS4168 on I2S)
#define PIN_LRCLK     16
#define PIN_DIN       18

#define VOLUME        0.8           // 0.0 - 1.0
#define SHOW_TIME     1300          // ms two different cards stay open

CRGB        leds[NUM_PADS];
Preferences prefs;

// =====================================================================
// Sound: plays the sounds in sounds.h, several at once
// =====================================================================

#define VOICES 8
#define CHUNK  64

struct Voice {
    bool     on = false;
    uint32_t pos = 0;               // position in the sound, x 65536
    uint32_t step = 65536;          // 65536 = normal speed
    Sound    sound;
    float    volume = 1;
};
Voice voices[VOICES];

struct PlayCommand { Sound sound; float speed; float volume; };
QueueHandle_t     playQueue;
i2s_chan_handle_t speaker;

void play(Sound sound, float speed = 1, float volume = 1) {
    PlayCommand c = {sound, speed, volume};
    xQueueSend(playQueue, &c, 0);
}

void soundTask(void *arg) {
    int16_t buffer[CHUNK * 2];
    while (true) {
        PlayCommand c;
        while (xQueueReceive(playQueue, &c, 0)) {
            int v = 0;                                  // a free voice, else the oldest
            for (int i = 0; i < VOICES; i++) {
                if (!voices[i].on) { v = i; break; }
                if (voices[i].pos > voices[v].pos) v = i;
            }
            voices[v] = {true, 0, (uint32_t)(c.speed * 65536), c.sound, c.volume};
        }
        for (int i = 0; i < CHUNK; i++) {
            float sum = 0;
            for (Voice &v : voices) {
                if (!v.on) continue;
                uint32_t index = v.pos >> 16;
                if (index >= SOUND_LENGTH[v.sound]) { v.on = false; continue; }
                sum += SOUND_DATA[v.sound][index] / 32768.0f * v.volume;
                v.pos += v.step;
            }
            int16_t s = tanhf(sum * VOLUME) * 32000;   // soft limit
            buffer[i * 2] = s;
            buffer[i * 2 + 1] = s;
        }
        size_t written;
        i2s_channel_write(speaker, buffer, sizeof(buffer), &written, portMAX_DELAY);
    }
}

void startSpeaker() {
    i2s_chan_config_t chan = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
    chan.dma_desc_num  = 3;
    chan.dma_frame_num = CHUNK;
    chan.auto_clear    = true;
    i2s_new_channel(&chan, &speaker, nullptr);
    i2s_std_config_t cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(SOUND_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = (gpio_num_t)PIN_BCLK,
            .ws   = (gpio_num_t)PIN_LRCLK,
            .dout = (gpio_num_t)PIN_DIN,
            .din  = I2S_GPIO_UNUSED,
            .invert_flags = {false, false, false},
        },
    };
    i2s_channel_init_std_mode(speaker, &cfg);
    i2s_channel_enable(speaker);
    playQueue = xQueueCreate(16, sizeof(PlayCommand));
    xTaskCreatePinnedToCore(soundTask, "sound", 4096, nullptr, 5, nullptr, 0);
}

// =====================================================================
// Keys
// =====================================================================

uint16_t scanKeys() {
    uint16_t keys = 0;
    for (int row = 0; row < 4; row++) {
        Wire.beginTransmission(PCF8574_ADDR);
        Wire.write(0xF0 | (0x0F & ~(1 << row)));    // pull this row low
        Wire.endTransmission();
        Wire.requestFrom(PCF8574_ADDR, 1);
        uint8_t pins = Wire.read();
        for (int col = 0; col < 4; col++) {
            if ((pins & (1 << (col + 4))) == 0) keys |= 1 << (row * 4 + col);
        }
    }
    return keys;
}

// The keys that were pressed since last time (with a 15 ms debounce)
uint16_t newPresses() {
    static uint16_t down = 0;
    static unsigned long lastChange[NUM_PADS];
    uint16_t raw = scanKeys(), pressed = 0;
    for (int i = 0; i < NUM_PADS; i++) {
        bool now = raw & (1 << i), was = down & (1 << i);
        if (now == was || millis() - lastChange[i] < 15) continue;
        lastChange[i] = millis();
        if (now) { down |= 1 << i; pressed |= 1 << i; }
        else down &= ~(1 << i);
    }
    return pressed;
}

// =====================================================================
// The cards
// =====================================================================

// The 8 card sounds (the first 8 in sounds.h) and a colour for each
const Sound CARD_SOUNDS[8] = {CAT, DOG, ROOSTER, SHEEP, HORSE, ELEPHANT, DOORBELL, COINS};
const CRGB  CARD_COLOURS[8] = {
    CRGB(180, 0, 255),   // cat: purple
    CRGB(140, 60, 10),   // dog: brown
    CRGB(255, 0, 30),    // rooster: red
    CRGB(220, 220, 220), // sheep: white
    CRGB(0, 200, 60),    // horse: green
    CRGB(30, 60, 255),   // elephant: blue
    CRGB(0, 200, 255),   // doorbell: light blue
    CRGB(255, 190, 0),   // coins: gold
};

int  card[NUM_PADS];                // which sound (0-7) is under each key
bool found[NUM_PADS];               // already matched
int  first = -1, second = -1;       // the cards open this turn
unsigned long closeAt;              // when to close two different cards
int  tries, pairs, best;

void shuffle() {
    for (int i = 0; i < NUM_PADS; i++) card[i] = i / 2;      // 0,0,1,1,2,2...
    for (int i = NUM_PADS - 1; i > 0; i--) {                 // mix them up
        int j = random(i + 1);
        int t = card[i]; card[i] = card[j]; card[j] = t;
    }
    for (int i = 0; i < NUM_PADS; i++) found[i] = false;
}

// =====================================================================
// The game
// =====================================================================

enum State { PLAYING, STARS };
State state = PLAYING;
unsigned long starsStart;
int  stars, starsShown;
bool newBest;

void startGame() {
    best = prefs.getInt("best", 0);
    shuffle();
    first = second = -1;
    tries = pairs = 0;
    play(GO);
    state = PLAYING;
}

void closeCards() {
    first = second = -1;
}

void openCard(int pad) {
    play(CARD_SOUNDS[card[pad]]);
    if (first < 0) { first = pad; return; }

    second = pad;
    tries++;
    if (card[first] == card[second]) {              // a pair!
        found[first] = found[second] = true;
        pairs++;
        play(MATCH, 1, 0.6);
        closeCards();
        if (pairs == NUM_PADS / 2) {                // all found
            stars = tries <= 14 ? 3 : tries <= 20 ? 2 : 1;
            newBest = best == 0 || tries < best;
            if (newBest) prefs.putInt("best", tries);
            play(CHEER);
            state = STARS;
            starsStart = millis();
            starsShown = 0;
        }
    } else {
        closeAt = millis() + SHOW_TIME;             // look (and listen) for a moment
    }
}

void playingUpdate(uint16_t pressed) {
    // Two different cards are open: close them after a moment
    if (second >= 0 && millis() >= closeAt) {
        play(NO_MATCH, 1, 0.5);
        closeCards();
    }
    for (int p = 0; p < NUM_PADS; p++) {
        if (!(pressed & (1 << p))) continue;
        if (found[p] || p == first) continue;       // already open
        if (second >= 0) closeCards();              // pressing on closes the last two early
        openCard(p);
    }
}

// =====================================================================
// Drawing
// =====================================================================

CRGB openColour(int pad) {
    return CARD_COLOURS[card[pad]];
}

void drawPlaying() {
    for (int i = 0; i < NUM_PADS; i++) {
        if (found[i]) {
            leds[i] = openColour(i);
            leds[i].nscale8(60);                    // found pairs: soft glow
        } else if (i == first || i == second) {
            leds[i] = openColour(i);                // open: bright
        } else {
            leds[i] = CRGB(6, 6, 10);               // closed: barely lit
        }
    }
}

void drawStars() {
    // 1, 2 or 3 rows of gold light up from the bottom, one row at a time
    unsigned long t = millis() - starsStart;
    int rows = t < 1200 ? 0 : min(stars, (int)((t - 1200) / 500) + 1);
    if (rows > starsShown) { starsShown = rows; play(TICK, 0.8 + 0.2 * rows); }
    fill_solid(leds, NUM_PADS, CRGB::Black);
    for (int r = 0; r < rows; r++)
        for (int c = 0; c < 4; c++) leds[(3 - r) * 4 + c] = CRGB(255, 180, 0);
    if (newBest && t > 1200 + stars * 500 + 300 && t < 1200 + stars * 500 + 3500) {
        for (int i = 0; i < NUM_PADS; i++) leds[i] = CHSV(i * 16 + millis() / 4, 255, 255);
    }
}

// =====================================================================
// Main
// =====================================================================

void setup() {
    Serial.begin(115200);
    Wire.begin(PIN_SDA, PIN_SCL);
    Wire.setTimeOut(5);
    FastLED.addLeds<WS2812B, PIN_LED, GRB>(leds, NUM_PADS);
    FastLED.setBrightness(BRIGHTNESS);
    prefs.begin("soundmatch", false);
    randomSeed(esp_random());
    startSpeaker();
    delay(300);
    startGame();
}

void loop() {
    uint16_t pressed = newPresses();

    switch (state) {
        case PLAYING:
            playingUpdate(pressed);
            break;
        case STARS:
            if (pressed && millis() - starsStart > 1200 + stars * 500 + 800) startGame();
            break;
    }

    static unsigned long lastLed = 0;
    if (millis() - lastLed >= 16) {
        lastLed = millis();
        if (state == PLAYING)       drawPlaying();
        else                        drawStars();
        FastLED.show();
    }
}
