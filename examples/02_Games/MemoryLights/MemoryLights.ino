// MemoryLights - watch the lights, then play them back (the classic "Simon" game)
// Board: ESP32-S3 N16R8 (16 MB flash, 8 MB PSRAM)
// Setup guide: see ../../01_HardwareTest/README.md
//
// How to play
//   1. Choose a level on the bottom row:
//        green  = easy    (the 4 middle pads)
//        yellow = normal  (the 8 pads of the middle two rows)
//        red    = hard    (all 16 pads)
//   2. Watch: some pads light up one after another, each with its own note.
//   3. Repeat them in the same order.
//   4. Every round adds one more step and plays a little faster.
//   A wrong pad (or waiting longer than 6 seconds) ends the game: the pad
//   you should have pressed blinks.
//
// Every pad has its own colour and its own xylophone note - low notes at the
// bottom, high notes at the top - so you can remember the tune, not only the
// lights.
//
// Your score = rounds completed. The pads fill up one by one, one per round:
// first 16 green, then blue, purple, gold. A new high score (one per level)
// plays a fanfare and a rainbow. Press any key to go back to the level choice.
//
// Arduino IDE settings (same as WhackAMole, MidiPad and ComboPad):
//   Tools > USB Mode:        USB-OTG (TinyUSB)
//   Tools > USB CDC On Boot: Enabled
//   Tools > Upload Mode:     USB-OTG CDC (TinyUSB)
// If an upload fails: hold BOOT, tap RESET, release BOOT, upload again.
// Needs the "FastLED" library.
// Sounds: effects by Kenney (www.kenney.nl) and a xylophone from FreePats /
// Versilian Community Sample Library, all CC0. See tools/make_sounds.py.

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

#define VOLUME        0.7           // 0.0 - 1.0
#define MAX_STEPS     100           // longest sequence
#define ANSWER_TIME   6000          // ms you may think before each press

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
    uint32_t step = 65536;          // 65536 = normal speed, more = faster and higher
    Sound    sound;
    float    volume = 1;
};
Voice voices[VOICES];

struct PlayCommand { Sound sound; float speed; float volume; };
QueueHandle_t     playQueue;
i2s_chan_handle_t speaker;

// Play a sound. speed 2 = one octave higher, 0.5 = one octave lower.
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
// The 16 notes: a C-major scale over two octaves, low at the bottom
// =====================================================================

// Pads from the bottom-left upwards (row by row)
const int LOW_TO_HIGH[16] = {12, 13, 14, 15, 8, 9, 10, 11, 4, 5, 6, 7, 0, 1, 2, 3};

// Semitones above C5 for each step of the scale: C D E F G A B C D E F G A B C D
const int SCALE[16] = {0, 2, 4, 5, 7, 9, 11, 12, 14, 16, 17, 19, 21, 23, 24, 26};

// The xylophone recordings we have, and how many semitones above C5 they are
const Sound XYLO[5]       = {XYLO_C5, XYLO_G5, XYLO_C6, XYLO_G6, XYLO_C7};
const int   XYLO_PITCH[5] = {0, 7, 12, 19, 24};

Sound padSound[NUM_PADS];           // which recording each pad uses
float padSpeed[NUM_PADS];           // ... and how fast to play it
CRGB  padColour[NUM_PADS];

void setupPads() {
    for (int i = 0; i < 16; i++) {
        int pad = LOW_TO_HIGH[i];
        int semitones = SCALE[i];
        int nearest = 0;                            // the closest recording
        for (int x = 1; x < 5; x++) {
            if (abs(XYLO_PITCH[x] - semitones) < abs(XYLO_PITCH[nearest] - semitones)) nearest = x;
        }
        padSound[pad] = XYLO[nearest];
        padSpeed[pad] = powf(2, (semitones - XYLO_PITCH[nearest]) / 12.0f);   // 1 semitone = x1.059
        padColour[pad] = CHSV(i * 15, 255, 255);    // red at the bottom ... purple at the top
    }
}

void playPad(int pad) {
    play(padSound[pad], padSpeed[pad]);
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
// LEDs: each pad glows in its own colour for a moment when it plays
// =====================================================================

uint8_t glow[NUM_PADS];             // 0-255, fades by itself

void light(int pad) { glow[pad] = 255; }

// =====================================================================
// The game
// =====================================================================

enum State { CHOOSE_LEVEL, SHOWING, ANSWERING, GAME_OVER_BLINK, SCORE };
State state = CHOOSE_LEVEL;

// The pads each level uses
const int EASY_PADS[4]   = {5, 6, 9, 10};
const int NORMAL_PADS[8] = {4, 5, 6, 7, 8, 9, 10, 11};
int level;                          // 0 easy, 1 normal, 2 hard

int  sequence[MAX_STEPS];
int  length;                        // steps in the sequence this round
int  position;                      // showing: next step to show; answering: next step to press
unsigned long timer;                // when the next thing should happen
bool padOn;                         // showing: is the current pad lit?
int  wrongPad, rightPad;
int  score, best, shown;
bool newBest;
unsigned long scoreStart;

int randomPad() {
    if (level == 0) return EASY_PADS[random(4)];
    if (level == 1) return NORMAL_PADS[random(8)];
    return random(16);
}

// How long each light stays on while showing - shorter as the game goes on
int stepTime() { return max(220, 520 - length * 20); }

void startShowing() {
    sequence[length++] = randomPad();
    position = 0;
    padOn = false;
    state = SHOWING;
    timer = millis() + 900;                         // a short pause first
}

void startGame(int chosenLevel) {
    level = chosenLevel;
    length = 0;
    best = prefs.getInt(("best" + String(level)).c_str(), 0);
    play(GO);
    startShowing();
}

void showingUpdate() {
    if (millis() < timer) return;
    if (!padOn) {                                   // light the next pad
        if (position >= length) {                   // all shown: your turn
            state = ANSWERING;
            position = 0;
            timer = millis() + ANSWER_TIME;
            return;
        }
        int pad = sequence[position];
        light(pad);
        playPad(pad);
        padOn = true;
        timer = millis() + stepTime();
    } else {                                        // short gap between steps
        padOn = false;
        position++;
        timer = millis() + stepTime() * 2 / 5;
    }
}

void gameOver(int pressed) {
    wrongPad = pressed;
    rightPad = sequence[position];
    play(WRONG);
    state = GAME_OVER_BLINK;
    timer = millis() + 1600;
}

void answeringUpdate(uint16_t pressed) {
    if (millis() > timer) { gameOver(-1); return; }  // took too long
    for (int p = 0; p < NUM_PADS; p++) {
        if (!(pressed & (1 << p))) continue;
        if (p != sequence[position]) { gameOver(p); return; }
        light(p);
        playPad(p);
        position++;
        timer = millis() + ANSWER_TIME;
        if (position >= length) {                   // the whole sequence, well done
            play(ROUND_OK, 1, 0.6);
            startShowing();
            return;
        }
    }
}

void showScore() {
    score = length - 1;                             // rounds completed
    newBest = score > best;
    if (newBest) prefs.putInt(("best" + String(level)).c_str(), score);
    play(GAME_OVER);
    state = SCORE;
    scoreStart = millis();
    shown = 0;
}

// =====================================================================
// Drawing
// =====================================================================

void drawChooseLevel() {
    // All pads softly in their colours; the three level keys pulse
    uint8_t pulse = 120 + 135 * (1 + sinf(millis() / 250.0f)) / 2;
    for (int i = 0; i < NUM_PADS; i++) {
        leds[i] = padColour[i];
        leds[i].nscale8(max((int)glow[i], 25));     // pressed pads light up (try the notes)
        glow[i] = glow[i] > 10 ? glow[i] - 10 : 0;
    }
    leds[12] = CRGB(0, 255, 0).nscale8(pulse);
    leds[13] = CRGB(255, 170, 0).nscale8(pulse);
    leds[14] = CRGB(255, 0, 0).nscale8(pulse);
    leds[15] = CRGB::Black;
}

void drawGame() {
    // The pads of this level glow faintly so you know which ones are in play
    for (int i = 0; i < NUM_PADS; i++) {
        bool inPlay = level == 2 || (level == 1 && i >= 4 && i <= 11)
                   || (level == 0 && (i == 5 || i == 6 || i == 9 || i == 10));
        leds[i] = padColour[i];
        leds[i].nscale8(max((int)glow[i], inPlay ? 12 : 0));
        glow[i] = glow[i] > 10 ? glow[i] - 10 : 0;
    }
}

void drawGameOverBlink() {
    fill_solid(leds, NUM_PADS, CRGB::Black);
    bool on = (millis() / 150) % 2;
    if (on) leds[rightPad] = padColour[rightPad];   // the pad you should have pressed
    if (wrongPad >= 0) leds[wrongPad] = CRGB(80, 0, 0);
    if (millis() > timer) showScore();
}

void drawScore() {
    unsigned long t = millis() - scoreStart;
    if (t > 1300 && shown < score && t > 1300 + shown * 90UL) {
        shown++;
        play(TICK, 0.8 + 0.03 * min(shown, 30));
        if (shown == score && newBest) play(NEW_BEST);
    }
    const CRGB LAYERS[4] = {CRGB(0, 220, 0), CRGB(0, 80, 255), CRGB(170, 0, 255), CRGB(255, 190, 0)};
    fill_solid(leds, NUM_PADS, CRGB::Black);
    for (int i = 0; i < shown; i++) leds[LOW_TO_HIGH[i % 16]] = LAYERS[min(i / 16, 3)];
    unsigned long doneAt = 1300 + score * 90UL;
    if (shown >= score && newBest && t > doneAt && t < doneAt + 3000) {
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
    prefs.begin("memory", false);
    randomSeed(esp_random());
    setupPads();
    startSpeaker();
}

void loop() {
    uint16_t pressed = newPresses();

    switch (state) {
        case CHOOSE_LEVEL:
            if (pressed & (1 << 12)) startGame(0);
            else if (pressed & (1 << 13)) startGame(1);
            else if (pressed & (1 << 14)) startGame(2);
            else for (int p = 0; p < 12; p++) {
                if (pressed & (1 << p)) { light(p); playPad(p); }   // try the notes
            }
            break;
        case SHOWING:
            showingUpdate();                        // presses are ignored while showing
            break;
        case ANSWERING:
            answeringUpdate(pressed);
            break;
        case SCORE:
            if (pressed && shown >= score && millis() - scoreStart > 1300 + score * 90UL + 800) {
                state = CHOOSE_LEVEL;
            }
            break;
        default:
            break;
    }

    static unsigned long lastLed = 0;
    if (millis() - lastLed >= 16) {
        lastLed = millis();
        if (state == CHOOSE_LEVEL)          drawChooseLevel();
        else if (state == GAME_OVER_BLINK)  drawGameOverBlink();
        else if (state == SCORE)            drawScore();
        else                                drawGame();
        FastLED.show();
    }
}
