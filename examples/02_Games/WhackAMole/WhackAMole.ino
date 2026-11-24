// WhackAMole - hit the moles before they hide again!
// Board: ESP32-S3 N16R8 (16 MB flash, 8 MB PSRAM)
// Setup guide: see ../../01_HardwareTest/README.md
//
// How to play
//   Press any key to start. After "tick, tick, tick, GO!" moles pop up.
//   Hit them before they hide. A round lasts 30 seconds.
//
//     orange          a mole           +1 (more hits in a row = higher "ding")
//     gold, sparkling a golden mole    +3
//     red, flashing   a BOMB - leave it alone!   -5
//
//   Every 10 seconds the moles get faster (you hear a power-up sound).
//   The last 5 seconds tick down.
//
// Your score
//   At the end the pads light up one by one, one per point:
//     first 16 points green, next 16 blue, next 16 purple, then gold.
//   A new high score plays a fanfare and a rainbow. Press any key to play again.
//
// Arduino IDE settings (same as MidiPad and ComboPad):
//   Tools > USB Mode:        USB-OTG (TinyUSB)
//   Tools > USB CDC On Boot: Enabled
//   Tools > Upload Mode:     USB-OTG CDC (TinyUSB)
// If an upload fails: hold BOOT, tap RESET, release BOOT, upload again.
// Needs the "FastLED" library.
// Sounds by Kenney (www.kenney.nl), CC0. To change them see tools/make_sounds.py.

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

#define GAME_SECONDS  30
#define VOLUME        0.7           // 0.0 - 1.0

CRGB        leds[NUM_PADS];
Preferences prefs;

// =====================================================================
// Sound: plays the effects in sounds.h, several at once
// =====================================================================

#define VOICES 8
#define CHUNK  64                   // samples made at a time (3 ms)

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

// Play a sound. speed 1.5 = higher and shorter, 0.7 = lower and longer.
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
    chan.dma_desc_num  = 3;                         // small buffers = no delay
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

// Returns all 16 keys at once: bit N is 1 when key N is down
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
// LED flashes (hits and mistakes) drawn on top of everything
// =====================================================================

CRGB    flashColour[NUM_PADS];
uint8_t flashLevel[NUM_PADS];

void flash(int pad, CRGB colour) { flashColour[pad] = colour; flashLevel[pad] = 255; }
void flashAll(CRGB colour)       { for (int i = 0; i < NUM_PADS; i++) flash(i, colour); }

void showLeds() {
    for (int i = 0; i < NUM_PADS; i++) {
        if (!flashLevel[i]) continue;
        CRGB f = flashColour[i];
        f.nscale8(flashLevel[i]);
        leds[i] += f;
        flashLevel[i] = flashLevel[i] > 15 ? flashLevel[i] - 15 : 0;
    }
    FastLED.show();
}

// =====================================================================
// The game
// =====================================================================

enum State { WAITING, COUNTDOWN, PLAYING, SCORE };
State state = WAITING;

enum Kind { EMPTY_HOLE, MOLE, GOLDEN, BOMB };
struct Hole {
    Kind kind = EMPTY_HOLE;
    unsigned long upAt, downAt;     // when it came up, when it goes back down
};
Hole holes[NUM_PADS];

unsigned long stateStart;           // when the current state began
unsigned long nextMole;             // when the next mole may pop up
int  score, combo, best;
int  upTime;                        // how long a mole stays up, ms
int  maxMoles;                      // moles up at the same time
int  lastSecond, lastStage, lastHole;
int  shownPoints;                   // score screen: pads lit so far
bool newBest;

// Points 1-16 green, 17-32 blue, 33-48 purple, then gold
const CRGB SCORE_COLOURS[4] = {CRGB(0, 220, 0), CRGB(0, 80, 255), CRGB(170, 0, 255), CRGB(255, 190, 0)};
// Pads from the bottom-left upwards, so the score "fills up" the keypad
const int FILL_ORDER[16] = {12, 13, 14, 15, 8, 9, 10, 11, 4, 5, 6, 7, 0, 1, 2, 3};

void startCountdown() {
    state = COUNTDOWN;
    stateStart = millis();
    lastSecond = -1;
}

void startGame() {
    state = PLAYING;
    stateStart = millis();
    for (Hole &h : holes) h.kind = EMPTY_HOLE;
    score = combo = 0;
    upTime = 1200;
    maxMoles = 1;
    nextMole = millis() + 400;
    lastSecond = lastStage = 0;
    lastHole = -1;
    play(GO);
    flashAll(CRGB(0, 120, 0));
}

void endGame() {
    for (Hole &h : holes) h.kind = EMPTY_HOLE;
    play(GAME_OVER);
    newBest = score > best;
    if (newBest) {
        best = score;
        prefs.putInt("best", best);
    }
    state = SCORE;
    stateStart = millis();
    shownPoints = 0;
}

// Make a mole (or golden mole, or bomb) pop up in a random empty hole
void popUp(unsigned long now, float secondsIn) {
    int up = 0;
    for (Hole &h : holes) if (h.kind != EMPTY_HOLE) up++;
    if (up >= maxMoles) return;

    int pad;
    do { pad = random(NUM_PADS); } while (holes[pad].kind != EMPTY_HOLE || pad == lastHole);
    lastHole = pad;

    int dice = random(100);
    Kind kind = MOLE;
    if (secondsIn > 8 && dice < 12) kind = BOMB;            // no bombs at the very start
    else if (dice >= 90) kind = GOLDEN;

    holes[pad].kind = kind;
    holes[pad].upAt = now;
    holes[pad].downAt = now + (kind == GOLDEN ? upTime * 3 / 4 : upTime);
    if (kind != BOMB) play(POP, 0.9 + random(30) / 100.0, 0.8);
}

void hitHole(int pad) {
    Hole &h = holes[pad];
    switch (h.kind) {
        case MOLE:
            score += 1;
            combo++;
            play(WHACK);
            play(POINT, 1 + 0.06 * min(combo, 12));         // rises with the combo
            flash(pad, CRGB::White);
            break;
        case GOLDEN:
            score += 3;
            combo++;
            play(WHACK);
            play(COINS);
            flash(pad, CRGB(255, 220, 80));
            break;
        case BOMB:
            score = max(0, score - 5);
            combo = 0;
            play(BOOM);
            flashAll(CRGB::Red);
            break;
        case EMPTY_HOLE:
            combo = 0;
            play(EMPTY, 0.7, 0.6);
            flash(pad, CRGB(60, 0, 0));
            break;
    }
    h.kind = EMPTY_HOLE;
}

void playingUpdate(uint16_t pressed) {
    unsigned long now = millis();
    float secondsIn = (now - stateStart) / 1000.0f;

    // Every 10 seconds: faster, and one more mole at a time
    int stage = secondsIn / 10;
    if (stage > lastStage && secondsIn < GAME_SECONDS) {
        lastStage = stage;
        upTime = upTime * 3 / 4;
        maxMoles = min(maxMoles + 1, 3);
        play(SPEED_UP);
        for (int i = 0; i < NUM_PADS; i++) flash(i, CHSV(i * 16, 255, 120));
    }

    // The last 5 seconds tick
    int second = secondsIn;
    if (second != lastSecond) {
        lastSecond = second;
        if (second >= GAME_SECONDS - 5 && second < GAME_SECONDS) play(TICK, 1.3);
    }

    // New moles
    if (now >= nextMole) {
        popUp(now, secondsIn);
        nextMole = now + upTime / 2 + random(upTime / 2);
    }

    // Moles that were too fast for you
    for (int i = 0; i < NUM_PADS; i++) {
        Hole &h = holes[i];
        if (h.kind != EMPTY_HOLE && now >= h.downAt) {
            if (h.kind != BOMB) {
                combo = 0;
                play(ESCAPE, 1, 0.6);
            }
            h.kind = EMPTY_HOLE;
        }
    }

    for (int p = 0; p < NUM_PADS; p++) {
        if (pressed & (1 << p)) hitHole(p);
    }

    if (secondsIn >= GAME_SECONDS) endGame();
}

// =====================================================================
// LEDs for each state
// =====================================================================

void drawWaiting() {
    // Soft "breathing" green: press any key
    uint8_t b = 20 + 40 * (1 + sinf(millis() / 600.0f)) / 2;
    fill_solid(leds, NUM_PADS, CRGB(0, b, b / 3));
}

void drawCountdown() {
    // 3, 2, 1: three rows lit, then two, then one (from the bottom)
    int second = (millis() - stateStart) / 800;
    if (second != lastSecond) {
        lastSecond = second;
        if (second < 3) play(TICK);
    }
    fill_solid(leds, NUM_PADS, CRGB::Black);
    int rows = 3 - second;
    for (int i = 0; i < rows * 4 && i < NUM_PADS; i++) leds[FILL_ORDER[i]] = CRGB(40, 40, 40);
    if (second >= 3) startGame();
}

void drawPlaying() {
    unsigned long now = millis();
    fill_solid(leds, NUM_PADS, CRGB(0, 6, 0));      // the "grass" between holes
    for (int i = 0; i < NUM_PADS; i++) {
        Hole &h = holes[i];
        if (h.kind == EMPTY_HOLE) continue;
        float age = now - h.upAt, life = h.downAt - h.upAt;
        uint8_t level = age < 80 ? age * 255 / 80 : 255;           // pop up quickly
        if (age > life * 0.75f && (now / 80) % 2) level /= 4;      // about to hide: blink
        CRGB c;
        if (h.kind == MOLE)   c = CRGB(255, 90, 0);
        if (h.kind == GOLDEN) c = CRGB(255, 200, 0).nscale8(160 + random(96));   // sparkle
        if (h.kind == BOMB)   c = CRGB(255, 0, 0).nscale8(120 + 135 * ((now / 100) % 2));
        c.nscale8(level);
        leds[i] = c;
    }
}

// When the score has finished counting up (ms after the score screen began)
unsigned long scoreDoneAt() { return 1300 + score * 70UL; }

void drawScore() {
    unsigned long t = millis() - stateStart;
    // Count the points up, one pad (and one tick) at a time
    if (t > 1300 && shownPoints < score && t > 1300 + shownPoints * 70UL) {
        shownPoints++;
        play(TICK, 0.8 + 0.02 * min(shownPoints, 40));
        if (shownPoints == score && newBest) play(NEW_BEST);
    }
    fill_solid(leds, NUM_PADS, CRGB::Black);
    for (int i = 0; i < shownPoints; i++) {
        int layer = min(i / 16, 3);
        leds[FILL_ORDER[i % 16]] = SCORE_COLOURS[layer];
    }
    bool done = shownPoints >= score;
    if (done && newBest && t < scoreDoneAt() + 3000) {             // rainbow for a new best
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
    prefs.begin("whack", false);
    best = prefs.getInt("best", 0);
    randomSeed(esp_random());
    startSpeaker();
}

void loop() {
    uint16_t pressed = newPresses();

    switch (state) {
        case WAITING:
            if (pressed) startCountdown();
            break;
        case PLAYING:
            playingUpdate(pressed);
            break;
        case SCORE:                                 // any key: play again
            if (pressed && millis() - stateStart > scoreDoneAt() + 800) startCountdown();
            break;
        default:
            break;
    }

    static unsigned long lastLed = 0;
    if (millis() - lastLed >= 16) {                 // about 60 times a second
        lastLed = millis();
        if (state == WAITING)        drawWaiting();
        else if (state == COUNTDOWN) drawCountdown();
        else if (state == PLAYING)   drawPlaying();
        else                         drawScore();
        showLeds();
    }
}
