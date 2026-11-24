// BeatGrid - a rhythm game on the 4x4 pads, shown on your phone, tablet or computer.
// Board: ESP32-S3 N16R8 (16 MB flash, 8 MB PSRAM) on the TinkerPad board.
// Setup guide: see README.md (and ../../01_HardwareTest/README.md for the Arduino settings).
//
// How it works
//   The BOARD runs the whole game: the music, the notes, the judging, the score.
//   The PAGE only draws it. So the sound always comes out of the board's speaker,
//   and a slow Wi-Fi link can only make the picture late, never the game.
//
//   1. Power the board. It makes a Wi-Fi network called "BeatGrid-XXXX" (no password).
//   2. Join it with a phone, tablet or computer. The page opens by itself,
//      or go to http://192.168.4.1
//   3. Pick a song, press START. A ring closes in on a square of the 4x4 grid on the
//      page - press that pad on the board when the ring lands. Hit it and your note
//      plays; miss it and the music has a gap.
//
//   No screen handy? The pads are a screen too: each pad lights up and brightens
//   as its note comes due. Pads 1-3 (top row) start a song, pads 13-15 (bottom row)
//   pick Easy / Normal / Hard.
//
// Needs the "FastLED" and "WebSockets" (by Markus Sattler) libraries.

#include <Wire.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <WebSocketsServer.h>
#include <Preferences.h>
#include <FastLED.h>

#include "songs.h"
#include "synth.h"
#include "web_page.h"

// ---------- Hardware ----------
#define PIN_SDA       9
#define PIN_SCL       8
#define PCF8574_ADDR  0x20
#define PIN_LED       38
#define PIN_BOOT      0
#define NUM_PADS      16
#define BRIGHTNESS    50

// Pad numbers on the page and in this file run left to right, top to bottom:
//
//      0  1  2  3
//      4  5  6  7
//      8  9 10 11
//     12 13 14 15
//
// If a pad lights up the wrong square on your carrier, fix it in these two tables.
// KEY_TO_PAD: key bit from the matrix scan -> pad. PAD_TO_LED: pad -> LED in the chain.
// (On the finished board key n has LED n and both count row by row, so these
// identity tables are right as they are.)
static const uint8_t KEY_TO_PAD[NUM_PADS] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
static const uint8_t PAD_TO_LED[NUM_PADS] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};

CRGB leds[NUM_PADS];
Preferences prefs;
WebServer server(80);
DNSServer dns;
WebSocketsServer ws(81);

// =====================================================================
// Keys
// =====================================================================

// All 16 keys at once: bit N is 1 while key N is down
static uint16_t scanKeys() {
    uint16_t keys = 0;
    for (int row = 0; row < 4; row++) {
        Wire.beginTransmission(PCF8574_ADDR);
        Wire.write(0xF0 | (0x0F & ~(1 << row)));
        Wire.endTransmission();
        Wire.requestFrom(PCF8574_ADDR, 1);
        uint8_t pins = Wire.read();
        for (int col = 0; col < 4; col++) {
            if ((pins & (1 << (col + 4))) == 0) keys |= 1 << (row * 4 + col);
        }
    }
    return keys;
}

// Pads newly pressed since last call (15 ms debounce), as a mask of PAD numbers
static uint16_t newPresses() {
    static uint16_t down = 0;
    static uint32_t lastChange[NUM_PADS];
    uint16_t raw = scanKeys(), pressed = 0;
    uint32_t now = millis();
    for (int k = 0; k < NUM_PADS; k++) {
        bool is = raw & (1 << k), was = down & (1 << k);
        if (is == was || now - lastChange[k] < 15) continue;
        lastChange[k] = now;
        if (is) { down |= 1 << k; pressed |= 1 << KEY_TO_PAD[k]; }
        else down &= ~(1 << k);
    }
    return pressed;
}

// =====================================================================
// The game
// =====================================================================

enum State { ST_IDLE, ST_PLAY, ST_END };
State state = ST_IDLE;

#define MAX_NOTES 340
struct Note {
    uint32_t t;        // when it is due, ms from the start of the song
    uint8_t pad;
    uint8_t midi;
    int8_t grade;      // -1 still to come, 0 perfect, 1 good, 2 ok, 3 missed
};
Note notes[MAX_NOTES];
int nNotes = 0;
int firstOpen = 0;     // every note before this one is settled
int ghostIdx = 0;      // next note whose quiet guide tone is still to be played
static const float GHOST_VOL[3] = {0.40f, 0.28f, 0.16f};   // guide tone loudness: Easy .. Hard

// Difficulty: how many notes, and how forgiving the timing is (ms either side)
static const char *DIFF_NAME[3] = {"Easy", "Normal", "Hard"};
static const uint16_t WIN_PERFECT[3] = {70, 50, 40};
static const uint16_t WIN_GOOD[3]    = {130, 100, 85};
static const uint16_t WIN_OK[3]      = {200, 150, 125};
static const uint16_t APPROACH_MS = 1600;     // how early a ring appears on the page
static const uint16_t LED_LEAD_MS = 600;      // how early a pad starts to glow

int selSong = 0, selDiff = 1;
const Song *song = &SONGS[0];
uint32_t startFrame = 0;                      // audio frame where the song's time 0 is
uint32_t endMs = 0;
uint32_t stateSince = 0;

int score, combo, maxCombo, counts[4];
int maxScore;
int bestScore;
bool newBest;
int lastBeat = -1;

// Pad flashes drawn over everything
CRGB flashColour[NUM_PADS];
uint8_t flashLevel[NUM_PADS];
static void flash(int pad, CRGB colour) { flashColour[pad] = colour; flashLevel[pad] = 255; }

// ---- chart building ----

static uint32_t rngState = 1;
static uint32_t rnd() {
    rngState ^= rngState << 13; rngState ^= rngState >> 17; rngState ^= rngState << 5;
    return rngState;
}

static int degreeOf(char c) { return c <= '9' ? c - '0' : c - 'a' + 10; }

// Turn a song's melody into targets. Higher notes go to higher rows, so the
// shape of the tune is the shape on the pads and you can learn it by hand.
static void buildChart(int songIdx, int diff) {
    const Song &sg = SONGS[songIdx];
    const uint8_t *scale = sg.minor ? SCALE_MINOR : SCALE_MAJOR;
    float stepMs = 60000.0f / sg.bpm / 4.0f;
    rngState = 2463534242u + songIdx * 7919;
    nNotes = 0;
    int bar = 1, lastCol = 1, lastPad = -1, lastPad2 = -1;

    for (const char *p = sg.order; *p; p++) {
        int ph = *p - 'A';
        for (int b = 0; b < 4; b++, bar++) {
            const char *pat = sg.phrase[ph][b];
            for (int st = 0; st < 16; st++) {
                char c = pat[st];
                if (c == '.') continue;
                if (diff == 0 && st % 4 != 0) continue;
                if (diff == 1 && st % 2 != 0) continue;
                if (nNotes >= MAX_NOTES) continue;

                int d = degreeOf(c);
                int row = d <= 4 ? 3 : d <= 8 ? 2 : d <= 11 ? 1 : 0;
                int col = (lastCol + 1 + (int)(rnd() % 3)) % 4;
                int pad = row * 4 + col;
                if (pad == lastPad || pad == lastPad2) { col = (col + 1) % 4; pad = row * 4 + col; }
                lastCol = col; lastPad2 = lastPad; lastPad = pad;

                Note &n = notes[nNotes++];
                n.t = (uint32_t)((bar * 16 + st) * stepMs);
                n.pad = pad;
                n.midi = sg.root + scale[d % 7] + 12 * (d / 7);
                n.grade = -1;
            }
        }
    }
    firstOpen = 0;
    ghostIdx = 0;
    int bars = 1 + (int)strlen(sg.order) * 4 + 1;
    endMs = (uint32_t)(bars * 16 * stepMs) + 1200;

    maxScore = 0;
    for (int i = 0; i < nNotes; i++) maxScore += 300 * (1 + min(i / 10, 3));
}

// ---- time ----

static int32_t songMs() {
    int32_t frames = (int32_t)(Synth::now() - startFrame) - AUDIO_LATENCY_FRAMES;
    return (int32_t)((int64_t)frames * 1000 / SR);
}

// ---- messages to the page ----

static char msg[5200];
static void send(const char *text) { ws.broadcastTXT(text); }

static void sendHello(int client) {
    char b[600];
    int n = snprintf(b, sizeof(b), "{\"e\":\"hi\",\"songs\":[");
    for (int i = 0; i < SONG_COUNT; i++)
        n += snprintf(b + n, sizeof(b) - n, "%s{\"n\":\"%s\",\"bpm\":%d,\"h\":%d}", i ? "," : "",
                      SONGS[i].name, SONGS[i].bpm, SONGS[i].hue);
    n += snprintf(b + n, sizeof(b) - n, "],\"song\":%d,\"diff\":%d,\"best\":[", selSong, selDiff);
    for (int s = 0; s < SONG_COUNT; s++)
        for (int d = 0; d < 3; d++) {
            char key[8];
            snprintf(key, sizeof(key), "b%d%d", s, d);
            n += snprintf(b + n, sizeof(b) - n, "%s%d", (s || d) ? "," : "", prefs.getInt(key, 0));
        }
    n += snprintf(b + n, sizeof(b) - n, "],\"st\":%d}", (int)state);
    ws.sendTXT(client, b);
}

static void sendSong(int client) {       // client < 0: everybody
    int32_t nowMs = songMs();
    uint32_t boardNow = millis();
    uint32_t startBoardMs = boardNow - nowMs;       // board time when song time was 0
    int n = snprintf(msg, sizeof(msg),
                     "{\"e\":\"song\",\"i\":%d,\"d\":%d,\"n\":\"%s\",\"bpm\":%d,\"h\":%d,\"start\":%u,\"end\":%u,\"max\":%d,\"notes\":[",
                     selSong, selDiff, song->name, song->bpm, song->hue, (unsigned)startBoardMs,
                     (unsigned)endMs, maxScore);
    for (int i = 0; i < nNotes && n < (int)sizeof(msg) - 24; i++)
        n += snprintf(msg + n, sizeof(msg) - n, "%s[%u,%u]", i ? "," : "", (unsigned)notes[i].t, notes[i].pad);
    snprintf(msg + n, sizeof(msg) - n, "]}");
    if (client < 0) ws.broadcastTXT(msg); else ws.sendTXT(client, msg);
}

static void sendIdle() {
    char b[80];
    snprintf(b, sizeof(b), "{\"e\":\"idle\",\"song\":%d,\"diff\":%d}", selSong, selDiff);
    send(b);
}

// ---- game flow ----

static const char *rankFor(int s) {
    float f = maxScore ? (float)s / maxScore : 0;
    return f >= 0.90f ? "S" : f >= 0.75f ? "A" : f >= 0.55f ? "B" : "C";
}

static void startGame(int songIdx, int diff) {
    if (state == ST_PLAY) return;
    selSong = constrain(songIdx, 0, SONG_COUNT - 1);
    selDiff = constrain(diff, 0, 2);
    prefs.putUChar("song", selSong);
    prefs.putUChar("diff", selDiff);
    song = &SONGS[selSong];
    buildChart(selSong, selDiff);
    score = combo = maxCombo = 0;
    for (int i = 0; i < 4; i++) counts[i] = 0;
    char key[8];
    snprintf(key, sizeof(key), "b%d%d", selSong, selDiff);
    bestScore = prefs.getInt(key, 0);
    newBest = false;
    memset(flashLevel, 0, sizeof(flashLevel));

    // The song starts half a second from now, so the page has time to get the notes
    int bars = 1 + (int)strlen(song->order) * 4 + 1;
    startFrame = Synth::now() + SR / 2;
    Synth::startSong(song, bars, startFrame);
    state = ST_PLAY;
    stateSince = millis();
    lastBeat = -1;
    sendSong(-1);
    Serial.printf("Start: %s (%s), %d notes\n", song->name, DIFF_NAME[selDiff], nNotes);
}

static void endGame() {
    state = ST_END;
    stateSince = millis();
    char key[8];
    snprintf(key, sizeof(key), "b%d%d", selSong, selDiff);
    if (score > bestScore) { newBest = true; bestScore = score; prefs.putInt(key, score); }
    char b[200];
    snprintf(b, sizeof(b),
             "{\"e\":\"end\",\"s\":%d,\"b\":%d,\"nb\":%d,\"j\":[%d,%d,%d,%d],\"mc\":%d,\"r\":\"%s\",\"max\":%d}",
             score, bestScore, newBest ? 1 : 0, counts[0], counts[1], counts[2], counts[3], maxCombo,
             rankFor(score), maxScore);
    send(b);
    Serial.printf("End: %d points, rank %s\n", score, rankFor(score));
}

static void stopGame() {
    Synth::stopAll();
    state = ST_IDLE;
    memset(flashLevel, 0, sizeof(flashLevel));
    sendIdle();
}

static CRGB gradeColour(int g) {
    switch (g) {
        case 0: return CRGB(255, 220, 120);
        case 1: return CRGB(0, 230, 120);
        case 2: return CRGB(40, 120, 255);
        default: return CRGB(255, 20, 20);
    }
}

static void settle(int i, int grade) {
    Note &n = notes[i];
    n.grade = grade;
    counts[grade]++;
    char b[100];
    if (grade < 3) {
        int mult = 1 + min(combo / 10, 3);
        score += (grade == 0 ? 300 : grade == 1 ? 200 : 100) * mult;
        combo++;
        if (combo > maxCombo) maxCombo = combo;
        flash(n.pad, gradeColour(grade));
        snprintf(b, sizeof(b), "{\"e\":\"hit\",\"i\":%d,\"j\":%d,\"s\":%d,\"c\":%d,\"x\":%d}", i, grade, score, combo,
                 1 + min(combo / 10, 3));
    } else {
        combo = 0;
        flash(n.pad, gradeColour(3));
        snprintf(b, sizeof(b), "{\"e\":\"miss\",\"i\":%d}", i);
    }
    send(b);
    while (firstOpen < nNotes && notes[firstOpen].grade >= 0) firstOpen++;
}

// A pad was pressed (on the board, or tapped on the page) at song time t
static void onPad(int pad, int32_t t) {
    int best = -1;
    int32_t bestDiff = 100000;
    for (int i = firstOpen; i < nNotes; i++) {
        if ((int32_t)notes[i].t > t + WIN_OK[selDiff]) break;
        if (notes[i].grade >= 0 || notes[i].pad != pad) continue;
        int32_t d = abs((int32_t)notes[i].t - t);
        if (d <= WIN_OK[selDiff] && d < bestDiff) { best = i; bestDiff = d; }
    }
    if (best < 0) {                               // nothing due here: a stray press, no penalty
        flash(pad, CRGB(40, 0, 0));
        return;
    }
    int grade = bestDiff <= WIN_PERFECT[selDiff] ? 0 : bestDiff <= WIN_GOOD[selDiff] ? 1 : 2;
    Synth::play(V_PLUCK, notes[best].midi, grade == 0 ? 1.0f : 0.9f);
    settle(best, grade);
}

static void updateGame() {
    if (state == ST_PLAY) {
        int32_t t = songMs();
        // the quiet guide melody: each note sounds softly the moment it is due,
        // so you can HEAR when to press. Your hit makes it loud.
        while (ghostIdx < nNotes && (int32_t)notes[ghostIdx].t <= t + 4) {
            Synth::play(V_PLUCK, notes[ghostIdx].midi, GHOST_VOL[selDiff]);
            ghostIdx++;
        }
        // notes that went past their last chance
        for (int i = firstOpen; i < nNotes; i++) {
            if ((int32_t)notes[i].t + WIN_OK[selDiff] >= t) break;
            if (notes[i].grade < 0) settle(i, 3);
        }
        if (t > (int32_t)endMs) endGame();
    }
}

// =====================================================================
// LEDs
// =====================================================================

static CRGB padColour(int pad, uint8_t value) {
    static const uint8_t HUE[4] = {210, 140, 70, 25};      // magenta, cyan, green, orange by column
    return CHSV(HUE[pad % 4], 255, value);
}

static void drawLeds() {
    CRGB frame[NUM_PADS];
    for (int i = 0; i < NUM_PADS; i++) frame[i] = CRGB::Black;
    uint32_t ms = millis();

    if (state == ST_IDLE) {
        // Row 0: the songs.  Row 3: Easy / Normal / Hard.  The rest breathes.
        uint8_t breathe = 8 + (sin8(ms / 8) >> 5);
        for (int i = 0; i < NUM_PADS; i++) frame[i] = CHSV(150, 200, breathe);
        for (int s = 0; s < SONG_COUNT; s++)
            frame[s] = CHSV(SONGS[s].hue * 255 / 360, 255, s == selSong ? 255 : 70);
        static const uint8_t DIFF_HUE[3] = {96, 55, 0};
        for (int d = 0; d < 3; d++)
            frame[13 + d] = CHSV(DIFF_HUE[d], 255, d == selDiff ? 255 : 50);
    } else if (state == ST_PLAY) {
        int32_t t = songMs();
        // a soft pulse on every beat
        float beatMs = 60000.0f / song->bpm;
        float phase = fmodf((t > 0 ? t : 0) / beatMs, 1.0f);
        uint8_t pulse = (uint8_t)(18.0f * expf(-phase * 5.0f));
        for (int i = 0; i < NUM_PADS; i++) frame[i] = CHSV(150, 180, 4 + pulse);
        // A countdown on the pad itself, in clear steps, ending in a snap to white:
        //   600-300 ms before  dim colour
        //   300-150 ms before  medium colour
        //   150-0   ms before  full colour, blinking fast
        //   due (white)        PRESS NOW  - stays white until the window closes
        for (int i = firstOpen; i < nNotes; i++) {
            int32_t dt = (int32_t)notes[i].t - t;
            if (dt > LED_LEAD_MS) break;
            if (notes[i].grade >= 0) continue;
            CRGB c;
            if (dt <= 0)        c = CRGB(255, 255, 255);
            else if (dt <= 150) c = padColour(notes[i].pad, ((ms / 40) & 1) ? 255 : 120);
            else if (dt <= 300) c = padColour(notes[i].pad, 110);
            else                c = padColour(notes[i].pad, 35);
            frame[notes[i].pad] = c;
        }
    } else {                                                        // END: score fills the pad from bottom
        static const int FILL[16] = {12, 13, 14, 15, 8, 9, 10, 11, 4, 5, 6, 7, 0, 1, 2, 3};
        float f = maxScore ? (float)score / maxScore : 0;
        int lit = (int)(f * 16 + 0.5f);
        uint32_t since = millis() - stateSince;
        int shown = min((int)(since / 120), lit);
        for (int i = 0; i < shown; i++)
            frame[FILL[i]] = f >= 0.9f ? CRGB(255, 190, 0) : f >= 0.75f ? CRGB(170, 0, 255) : f >= 0.55f ? CRGB(0, 120, 255) : CRGB(0, 200, 0);
    }

    for (int i = 0; i < NUM_PADS; i++) {
        if (flashLevel[i]) {
            CRGB f = flashColour[i];
            f.nscale8(flashLevel[i]);
            frame[i] += f;
            flashLevel[i] = flashLevel[i] > 14 ? flashLevel[i] - 14 : 0;
        }
        leds[PAD_TO_LED[i]] = frame[i];
    }
    FastLED.show();
}

// =====================================================================
// Pad presses in each state
// =====================================================================

static void handlePress(int pad, int32_t t) {
    if (state == ST_PLAY) {
        onPad(pad, t);
    } else if (state == ST_END) {
        if (millis() - stateSince > 1500) stopGame();
    } else {                                                       // idle
        if (pad < SONG_COUNT) {
            startGame(pad, selDiff);
        } else if (pad >= 13 && pad <= 15) {
            selDiff = pad - 13;
            prefs.putUChar("diff", selDiff);
            sendIdle();
        }
    }
}

// =====================================================================
// Wi-Fi, the page and the WebSocket
// =====================================================================

static int jsonInt(const char *text, const char *key, int fallback) {
    const char *p = strstr(text, key);
    if (!p) return fallback;
    p += strlen(key);
    while (*p == ' ' || *p == ':') p++;
    return atoi(p);
}

static void onWebSocket(uint8_t num, WStype_t type, uint8_t *payload, size_t length) {
    if (type == WStype_CONNECTED) {
        sendHello(num);
        if (state == ST_PLAY) sendSong(num);
        return;
    }
    if (type != WStype_TEXT) return;
    const char *text = (const char *)payload;
    if (strstr(text, "\"ping\"")) {
        char b[100];
        snprintf(b, sizeof(b), "{\"e\":\"pong\",\"c\":%d,\"b\":%u}", jsonInt(text, "\"t\"", 0), (unsigned)millis());
        ws.sendTXT(num, b);
    } else if (strstr(text, "\"start\"")) {
        startGame(jsonInt(text, "\"song\"", selSong), jsonInt(text, "\"diff\"", selDiff));
    } else if (strstr(text, "\"stop\"")) {
        if (state != ST_IDLE) stopGame();
    } else if (strstr(text, "\"sel\"")) {
        if (state == ST_IDLE) {
            selSong = constrain(jsonInt(text, "\"song\"", selSong), 0, SONG_COUNT - 1);
            selDiff = constrain(jsonInt(text, "\"diff\"", selDiff), 0, 2);
            prefs.putUChar("song", selSong);
            prefs.putUChar("diff", selDiff);
        }
    } else if (strstr(text, "\"tap\"")) {
        int pad = jsonInt(text, "\"p\"", -1);
        if (pad >= 0 && pad < NUM_PADS) handlePress(pad, songMs());
    }
}

static void startNetwork() {
    uint64_t mac = ESP.getEfuseMac();
    char name[24];
    snprintf(name, sizeof(name), "BeatGrid-%02X%02X", (uint8_t)(mac >> 32), (uint8_t)(mac >> 40));
    WiFi.mode(WIFI_AP);
    WiFi.softAP(name, nullptr, 6, 0, 4);                           // open network, up to 4 players watching
    WiFi.setSleep(false);
    dns.start(53, "*", WiFi.softAPIP());                           // the page opens by itself

    server.on("/", HTTP_GET, []() { server.send_P(200, "text/html", WEB_PAGE); });
    server.onNotFound([]() {
        server.sendHeader("Location", "http://192.168.4.1/");
        server.send(302, "text/plain", "");
    });
    server.begin();
    ws.begin();
    ws.onEvent(onWebSocket);
    ws.enableHeartbeat(15000, 3000, 2);
    Serial.printf("Wi-Fi \"%s\" is up. Join it (no password), then open http://192.168.4.1\n", name);
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
    FastLED.clear(true);
    pinMode(PIN_BOOT, INPUT_PULLUP);

    prefs.begin("beatgrid", false);
    selSong = constrain(prefs.getUChar("song", 0), 0, SONG_COUNT - 1);
    selDiff = constrain(prefs.getUChar("diff", 1), 0, 2);
    song = &SONGS[selSong];

    Synth::begin();
    startNetwork();
    Serial.println("BeatGrid ready.");
}

void loop() {
    uint16_t pressed = newPresses();
    int32_t t = songMs() - 1;                      // a pad is read ~1 ms after it was hit
    for (int p = 0; p < NUM_PADS; p++)
        if (pressed & (1 << p)) handlePress(p, t);

    updateGame();

    static bool bootWas = false;                   // BOOT button: stop the song
    bool boot = digitalRead(PIN_BOOT) == LOW;
    if (boot && !bootWas && state != ST_IDLE) stopGame();
    bootWas = boot;

    if (state == ST_END && millis() - stateSince > 30000) stopGame();

    static uint32_t lastLed = 0;
    if (millis() - lastLed >= 16) {
        lastLed = millis();
        drawLeds();
    }

    dns.processNextRequest();
    server.handleClient();
    ws.loop();
}
