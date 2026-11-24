// PingPong - table tennis for two players on TinkerPad
// Board: ESP32-S3 N16R8 (16 MB flash, 8 MB PSRAM)
// Setup guide: see ../../01_HardwareTest/README.md
//
// Put TinkerPad on the table between you. Player 1 sits on the LEFT
// (teal), player 2 on the RIGHT (orange). Your "bat" is the column of 4 keys
// on your edge (dimly lit in your colour).
//
// How to play (the game starts as soon as the board is on)
//   1. "tick, tick, tick" - the server's ball blinks - then it is served.
//   2. The white ball flies across, one key at a time, with a soft trail.
//      You hear it bounce on the table when it reaches your half.
//   3. When it lands on YOUR edge column, press that key (same row) quickly
//      to hit it back. Wrong key or too late = a point for the other player.
//   4. Every return makes the ball a little faster. Sometimes it changes row
//      on the way - watch it! Hit very quickly and YOUR return always
//      changes row (a "smash" that is harder to read).
//
// After each point, your points light up in your half (one key per point).
// First to 5 wins. "Match point" plays a tense sound. The winner's half shows a
// rainbow - press any key for a new match. Serve takes turns.
// Each player's bat has its own pitch, so you can hear who hit.
//
// Arduino IDE settings (same as the other TinkerPad games):
//   Tools > USB Mode:        USB-OTG (TinyUSB)
//   Tools > USB CDC On Boot: Enabled
//   Tools > Upload Mode:     USB-OTG CDC (TinyUSB)
// If an upload fails: hold BOOT, tap RESET, release BOOT, upload again.
// Needs the "FastLED" library.
// Sounds: real table-tennis bat and table recordings, "Ping Pong Sounds" by
// Aj_ on OpenGameArt (CC0); other effects by Kenney (www.kenney.nl), CC0.
// To change them see tools/make_sounds.py.

#include <Wire.h>
#include <FastLED.h>
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

// ---- Tune the game here ----
#define WIN_SCORE           5       // first to this many points wins
#define START_STEP_MS       380     // ms per key the ball moves at the serve (bigger = slower)
#define SPEED_UP            0.92    // each return: step time x this (0.92 = 8% faster)
#define FASTEST_STEP_MS     110     // the ball never gets faster than this
#define HIT_WINDOW_MS       450     // how long the ball waits on your edge for you to hit it
#define EARLY_MS            100     // a press this much before the ball lands still counts
#define QUICK_HIT_MS        150     // hit within this time = a "smash" that changes row
#define ROW_CHANGE_PERCENT  35      // chance a normal return changes row on the way
#define TICK_MS             400     // count-in: time between ticks

// Each player's bat sound is the same recording at a different speed (pitch).
// Keep these the same as P1_PITCH / P2_PITCH in tools/make_sounds.py.
#define P1_PITCH      1.15
#define P2_PITCH      0.87

CRGB leds[NUM_PADS];

// What the game is doing right now (declared here, before any function,
// so the Arduino IDE knows the type when it builds its function list)
enum State { COUNT_IN, FLYING, WAITING, MISSED, SCORE, WON };
State state;
unsigned long stateStart;           // when the current state began

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

int pad(int row, int col) { return row * 4 + col; }   // keys are numbered row by row

// =====================================================================
// The players and the ball
// =====================================================================

const int   BAT_COL[2] = {0, 3};                        // player 1 left, player 2 right
const float PITCH[2]   = {P1_PITCH, P2_PITCH};
const CRGB  COLOUR[2]  = {CRGB(0, 190, 170), CRGB(255, 90, 0)};   // teal, orange

// The keys of each half, in the order the points light up (edge column first)
const int HALF[2][8] = {{0, 4, 8, 12, 1, 5, 9, 13},
                        {3, 7, 11, 15, 2, 6, 10, 14}};

int   score[2];
int   server;                       // who serves next (0 or 1)
int   ballRow, ballCol;
int   trailRow = -1, trailCol;      // where the ball just was (drawn dim)
int   dir;                          // +1 = flying right (to player 2), -1 = left
int   drift;                        // row change on this shot: -1, 0 or +1
float stepMs;                       // time for one key - gets smaller = faster
unsigned long nextStep, lastMove;
int   loser, ticks;
bool  matchPointPlayed;

int receiver() { return dir > 0 ? 1 : 0; }              // who the ball is flying to

// =====================================================================
// The game
// =====================================================================

void setState(State s) { state = s; stateStart = millis(); }

void startServe() {
    ballCol = BAT_COL[server];
    ballRow = random(4);
    trailRow = -1;
    ticks = 0;
    stepMs = START_STEP_MS;                             // every rally starts slow
    setState(COUNT_IN);
}

void startMatch() {
    score[0] = score[1] = 0;
    server = random(2);
    play(START);
    startServe();
}

// The ball leaves player `from`'s bat
void launch(int from, bool smash) {
    dir = from == 0 ? 1 : -1;
    play(BAT, PITCH[from]);
    drift = 0;
    if (smash || random(100) < ROW_CHANGE_PERCENT) {
        drift = random(2) ? 1 : -1;
        if (ballRow + drift < 0 || ballRow + drift > 3) drift = -drift;   // stay on the board
    }
    nextStep = millis() + stepMs;
    setState(FLYING);
}

void moveBall() {
    trailRow = ballRow;
    trailCol = ballCol;
    lastMove = millis();
    ballCol += dir;
    // Halfway (the column next to the receiver) the ball bounces on the
    // table, and that is where it may change row - so the receiver always
    // sees its final row one step before it arrives.
    if (ballCol == BAT_COL[receiver()] - dir) {
        ballRow += drift;
        play(TABLE, 1, 0.6);
    }
    if (ballCol == BAT_COL[receiver()]) setState(WAITING);   // hit it now!
    else nextStep += stepMs;
}

void hit(bool smash) {
    stepMs = fmaxf(FASTEST_STEP_MS, stepMs * SPEED_UP);   // a bit faster every return
    launch(receiver(), smash);
}

void miss(int who) {
    loser = who;
    score[1 - who]++;
    play(MISS);
    setState(MISSED);
}

void handlePresses(uint16_t pressed) {
    int r = receiver();
    int bat = BAT_COL[r];
    for (int row = 0; row < 4; row++) {
        if (!(pressed & (1 << pad(row, bat)))) continue;   // only the receiver's bat keys count
        if (state == WAITING) {
            if (row == ballRow) hit(millis() - stateStart < QUICK_HIT_MS);
            else miss(r);                                  // wrong key
            return;
        }
        // A tiny bit early (ball one key away) still counts, so a fast ball is fair
        if (ballCol == bat - dir && row == ballRow && (long)(nextStep - millis()) <= EARLY_MS) {
            trailRow = ballRow; trailCol = ballCol; lastMove = millis();
            ballCol = bat;
            hit(true);
            return;
        }
    }
}

void update(uint16_t pressed) {
    unsigned long t = millis() - stateStart;
    switch (state) {
        case COUNT_IN:                                      // tick, tick, tick, serve
            if (t >= (unsigned long)(ticks + 1) * TICK_MS) {
                if (ticks < 3) { play(TICK); ticks++; }
                else launch(server, false);
            }
            break;
        case FLYING:
            handlePresses(pressed);
            if (state == FLYING && (long)(millis() - nextStep) >= 0) moveBall();
            break;
        case WAITING:
            handlePresses(pressed);
            if (state == WAITING && t > HIT_WINDOW_MS) miss(receiver());   // too late
            break;
        case MISSED:                                        // red flash, then the score
            if (t > 700) {
                if (score[1 - loser] >= WIN_SCORE) { play(WIN); setState(WON); }
                else { play(POINT, PITCH[1 - loser]); matchPointPlayed = false; setState(SCORE); }
            }
            break;
        case SCORE:
            if (t > 600 && !matchPointPlayed &&
                (score[0] == WIN_SCORE - 1 || score[1] == WIN_SCORE - 1)) {
                play(MATCH_POINT);
                matchPointPlayed = true;
            }
            if (t > 1800) { server = 1 - server; startServe(); }   // serve takes turns
            break;
        case WON:
            if (pressed && t > 2000) startMatch();
            break;
    }
}

// =====================================================================
// Drawing
// =====================================================================

void drawBats() {                   // each player's bat column, dimly in their colour
    for (int p = 0; p < 2; p++)
        for (int row = 0; row < 4; row++) leds[pad(row, BAT_COL[p])] = CRGB(COLOUR[p]).nscale8(25);
}

void drawBall() {
    // The trail fades out over one step
    if (trailRow >= 0) {
        long age = millis() - lastMove;
        int glow = 70 - 70 * age / max(1L, (long)stepMs);
        if (glow > 0) leds[pad(trailRow, trailCol)] = CRGB(glow, glow, glow);
    }
    leds[pad(ballRow, ballCol)] = CRGB::White;
}

void drawScores(int blinkPlayer) {
    for (int p = 0; p < 2; p++)
        for (int i = 0; i < score[p]; i++) {
            bool newest = p == blinkPlayer && i == score[p] - 1;
            if (newest && (millis() / 150) % 2) continue;  // the new point blinks
            leds[HALF[p][i]] = COLOUR[p];
        }
}

void draw() {
    fill_solid(leds, NUM_PADS, CRGB::Black);
    unsigned long t = millis() - stateStart;
    switch (state) {
        case COUNT_IN:
            drawBats();
            if ((t / 100) % 2 == 0) leds[pad(ballRow, ballCol)] = CRGB::White;   // ball blinks
            break;
        case FLYING:
        case WAITING:
            drawBats();
            drawBall();
            break;
        case MISSED:                                        // red flash on the loser's half
            if ((t / 120) % 2 == 0)
                for (int i = 0; i < 8; i++) leds[HALF[loser][i]] = CRGB::Red;
            break;
        case SCORE:
            drawScores(1 - loser);
            break;
        case WON: {
            int w = 1 - loser;
            for (int i = 0; i < 8; i++) leds[HALF[w][i]] = CHSV(i * 32 + millis() / 4, 255, 255);
            for (int i = 0; i < score[loser]; i++) leds[HALF[loser][i]] = CRGB(COLOUR[loser]).nscale8(60);
            break;
        }
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
    randomSeed(esp_random());
    startSpeaker();
    delay(300);
    startMatch();
}

void loop() {
    update(newPresses());

    static unsigned long lastLed = 0;
    if (millis() - lastLed >= 16) {
        lastLed = millis();
        draw();
        FastLED.show();
    }
}
