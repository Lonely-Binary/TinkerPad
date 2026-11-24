// MidiPad - a drum pad and a "follow the lights" music game
// Board: ESP32-S3 N16R8 (16 MB flash, 8 MB PSRAM)
// Setup guide: see ../../01_HardwareTest/README.md
//
// ===== Plugged into a computer: USB MIDI drum pads =====
// It shows up as a MIDI device (named "ESP32S3_DEV" on a Mac). Open GarageBand,
// Logic, Ableton... make a drum track and play. General MIDI drums, channel 10:
//
//      Crash       Crash 2    Ride        Ride bell     <- top row
//      Floor tom   Low tom    Mid tom     High tom
//      Soft snare  China      Pedal hat   Kick 2
//      KICK        SNARE      Closed hat  Open hat      <- bottom row
//
// ===== No computer (battery or phone charger): the music game =====
// The keypad itself is the menu:
//
//      [song 1]  [song 2]  [song 3]  [song 4]     green  = everyday songs
//      [song 5]  [song 6]  [song 7]  [song 8]     blue   = classics
//      [song 9]  [song 10] [song 11] [song 12]    orange = games and films
//      [FREE]    [MODE]    [LEVEL]   [VOLUME]
//
//   Song key once   -> hear the start of the song (the key blinks)
//   Same key again  -> play it: count-in, then follow the lights
//   FREE            -> free drumming
//   MODE            -> blue = learn (the song waits for you), red = challenge
//   LEVEL           -> green = easy (4 pads), yellow = normal (8), red = hard (16)
//   VOLUME          -> three levels
//
// Playing: the pad to press lights up and gets brighter until it is time.
// Each press plays the next note of the melody, the drums play along.
// Flash colours: green = perfect, yellow = good, red = wrong or missed.
// At the end the pads fill up from the bottom: the more, the better.
// Hold the two TOP CORNER keys for 1.5 s to go back to the menu at any time.
//
// Arduino IDE settings (same as ComboPad):
//   Tools > USB Mode:        USB-OTG (TinyUSB)
//   Tools > USB CDC On Boot: Enabled
//   Tools > Upload Mode:     USB-OTG CDC (TinyUSB)
// If an upload fails: hold BOOT, tap RESET, release BOOT, upload again.
//
// Needs the "FastLED" library. USB MIDI comes with the ESP32 core.
//
// Drum sounds: MuldjordKit by Lars Muldjord, FreePats version, Creative Commons
// Attribution 4.0 (https://freepats.zenvoid.org/Percussion/acoustic-drum-kit.html).
// Melodies: https://github.com/robsoncouto/arduino-songs

#include <Wire.h>
#include <FastLED.h>
#include <Preferences.h>
#include "USB.h"
#include "USBMIDI.h"
#include <driver/i2s_std.h>
#include "sound.h"
#include "game.h"

// ---------- Pins and settings ----------
#define PIN_SDA       9
#define PIN_SCL       8
#define PCF8574_ADDR  0x20
#define PIN_LED       38
#define NUM_PADS      16
#define BRIGHTNESS    60

#define PIN_BCLK      17            // speaker (NS4168 on I2S)
#define PIN_LRCLK     16
#define PIN_DIN       18
#define CHUNK         64            // samples made at a time (2 ms)

#define DRUM_CHANNEL  10            // General MIDI drums are always on channel 10
#define VELOCITY      100
#define DEBOUNCE_MS   15

#define PERFECT_MS    70            // press this close to the beat for "perfect"
#define GOOD_MS       150           // ... or this close for "good"

// Function keys on the bottom row (menu only)
#define KEY_FREE      12
#define KEY_MODE      13
#define KEY_LEVEL     14
#define KEY_VOLUME    15

USBMIDI     MIDI("TinkerPad Drums");
CRGB        leds[NUM_PADS];
Preferences prefs;

const uint8_t NOTES[NUM_PADS] = {
    49, 57, 51, 53,     // Crash, Crash 2, Ride, Ride bell
    41, 45, 48, 50,     // Floor tom, Low tom, Mid tom, High tom
    40, 52, 44, 35,     // Soft snare, China, Pedal hat, Kick 2
    36, 38, 42, 46,     // Kick, Snare, Closed hat, Open hat
};

// Drum-group colours (MIDI and free-drum mode)
const CRGB CYMBAL_COLOUR = CRGB(0, 160, 255);
const CRGB TOM_COLOUR    = CRGB(40, 60, 255);
const CRGB HAT_COLOUR    = CRGB(255, 170, 0);
const CRGB DRUM_COLOUR   = CRGB(255, 20, 0);
const CRGB DRUM_COLOURS[NUM_PADS] = {
    CYMBAL_COLOUR, CYMBAL_COLOUR, CYMBAL_COLOUR, CYMBAL_COLOUR,
    TOM_COLOUR,    TOM_COLOUR,    TOM_COLOUR,    TOM_COLOUR,
    DRUM_COLOUR,   CYMBAL_COLOUR, HAT_COLOUR,    DRUM_COLOUR,
    DRUM_COLOUR,   DRUM_COLOUR,   HAT_COLOUR,    HAT_COLOUR,
};

// Menu colours of the 12 songs (one colour per row)
const CRGB ROW_COLOURS[3] = {CRGB(0, 220, 60), CRGB(30, 90, 255), CRGB(255, 100, 0)};

// ---------- Saved settings ----------
bool  learnMode = true;             // true = the song waits for you
int   level = 0;                    // 0 easy, 1 normal, 2 hard
int   volumeLevel = 1;              // 0, 1, 2
const float VOLUMES[3] = {0.3, 0.6, 1.0};

// =====================================================================
// Sound: made in its own task so the music never stutters
// =====================================================================

enum SoundKind { DRUM, NOTE, SILENCE };
struct SoundCommand {
    SoundKind kind;
    int       pad;          // DRUM
    float     hz;           // NOTE
    int       ms;           // NOTE
    float     volume;
};

i2s_chan_handle_t speaker;
QueueHandle_t     soundQueue;

void drum(int pad, float volume)        { SoundCommand c = {DRUM, pad, 0, 0, volume}; xQueueSend(soundQueue, &c, 0); }
void note(float hz, int ms)             { SoundCommand c = {NOTE, 0, hz, ms, 1};      xQueueSend(soundQueue, &c, 0); }
void silence()                          { SoundCommand c = {SILENCE, 0, 0, 0, 0};     xQueueSend(soundQueue, &c, 0); }

void soundTask(void *arg) {
    int16_t buffer[CHUNK * 2];                      // left + right
    while (true) {
        SoundCommand c;
        while (xQueueReceive(soundQueue, &c, 0)) {
            if (c.kind == DRUM)    playDrum(c.pad, c.volume);
            if (c.kind == NOTE)    playNote(c.hz, c.ms, c.volume);
            if (c.kind == SILENCE) stopAllSound();
        }
        for (int i = 0; i < CHUNK; i++) {
            int16_t s = mixSample() * 32000;
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
    i2s_channel_init_std_mode(speaker, &cfg);
    i2s_channel_enable(speaker);
    soundQueue = xQueueCreate(32, sizeof(SoundCommand));
    xTaskCreatePinnedToCore(soundTask, "sound", 4096, nullptr, 5, nullptr, 0);
}

// =====================================================================
// Keys
// =====================================================================

uint16_t keysDown = 0;              // keys held right now (debounced)

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

// Updates keysDown and returns the keys that were just pressed / released
void readKeys(uint16_t &pressed, uint16_t &released) {
    static unsigned long lastChange[NUM_PADS];
    uint16_t raw = scanKeys();
    pressed = released = 0;
    for (int i = 0; i < NUM_PADS; i++) {
        bool now = raw & (1 << i), was = keysDown & (1 << i);
        if (now == was || millis() - lastChange[i] < DEBOUNCE_MS) continue;
        lastChange[i] = millis();
        if (now) { keysDown |= 1 << i;    pressed  |= 1 << i; }
        else     { keysDown &= ~(1 << i); released |= 1 << i; }
    }
}

// True once the two top corner keys have been held for 1.5 s
bool exitHeld() {
    static unsigned long since = 0;
    if ((keysDown & 0b1001) != 0b1001) { since = 0; return false; }
    if (since == 0) since = millis();
    return millis() - since > 1500;
}

// =====================================================================
// Is a computer connected?
// =====================================================================

volatile bool computer = false;

void usbEvent(void *arg, esp_event_base_t base, int32_t id, void *data) {
    if (id == ARDUINO_USB_STARTED_EVENT || id == ARDUINO_USB_RESUME_EVENT) computer = true;
    if (id == ARDUINO_USB_STOPPED_EVENT || id == ARDUINO_USB_SUSPEND_EVENT) computer = false;
}

// =====================================================================
// LEDs
// =====================================================================

// Short coloured flashes on top of everything else (hits, feedback)
CRGB    flashColour[NUM_PADS];
uint8_t flashLevel[NUM_PADS];

void flash(int pad, CRGB colour) {
    flashColour[pad] = colour;
    flashLevel[pad] = 255;
}

void showLeds() {
    for (int i = 0; i < NUM_PADS; i++) {
        if (flashLevel[i]) {
            CRGB f = flashColour[i];
            f.nscale8(flashLevel[i]);
            leds[i] += f;
            flashLevel[i] = flashLevel[i] > 12 ? flashLevel[i] - 12 : 0;
        }
    }
    FastLED.show();
}

// =====================================================================
// Screens
// =====================================================================

enum Screen { MENU, PLAYING, RESULT, FREE_DRUMS, MIDI_MODE };
Screen screen = MENU;

int   currentSong = -1;
float songClock = 0;                // ms into the song (negative = count-in)
unsigned long lastTick = 0;
int   nextNote = 0;                 // first note not played yet
bool  waiting = false;              // learn mode: stopped, waiting for a press
long  nextEighth = 0;               // next eighth-note of the drum beat
int   lastCountBeat = 0;

// Menu: song preview
int   previewSong = -1;
unsigned long previewStart = 0;
int   previewNext = 0;

// Results
int   resultPercent = 0;
bool  newBest = false;
unsigned long resultStart = 0;

void saveSettings() {
    prefs.putBool("learn", learnMode);
    prefs.putInt("level", level);
    prefs.putInt("volume", volumeLevel);
}

String bestKey(int song) {          // e.g. "b3_1_c" = song 3, normal, challenge
    return "b" + String(song) + "_" + String(level) + (learnMode ? "_l" : "_c");
}

void goMenu() {
    silence();
    screen = MENU;
    previewSong = -1;
}

// ---------- Menu ----------

void startPreview(int song) {
    previewSong = song;
    loadSong(song, level);
    previewStart = millis();
    previewNext = 0;
    silence();
}

void startGame(int song) {
    currentSong = song;
    loadSong(song, level);
    previewSong = -1;
    silence();
    songClock = -beatsPerBar * beatMs;              // one bar of count-in
    lastCountBeat = -beatsPerBar - 1;
    lastTick = millis();
    nextNote = 0;
    nextEighth = 0;
    waiting = false;
    screen = PLAYING;
}

void menuKeys(uint16_t pressed) {
    for (int k = 0; k < NUM_PADS; k++) {
        if (!(pressed & (1 << k))) continue;
        if (k < 12) {
            if (k == previewSong) startGame(k);     // second press: play
            else startPreview(k);                   // first press: listen
        } else if (k == KEY_FREE) {
            silence();
            screen = FREE_DRUMS;
        } else if (k == KEY_MODE) {
            learnMode = !learnMode;
            saveSettings();
            drum(14, 0.6);
        } else if (k == KEY_LEVEL) {
            level = (level + 1) % 3;
            saveSettings();
            drum(14, 0.6);
        } else if (k == KEY_VOLUME) {
            volumeLevel = (volumeLevel + 1) % 3;
            masterVolume = VOLUMES[volumeLevel];
            saveSettings();
            drum(13, 0.8);                          // hear the new volume
        }
    }
}

void updatePreview() {
    if (previewSong < 0) return;
    unsigned long t = millis() - previewStart;
    while (previewNext < noteCount && gameNotes[previewNext].start <= t && t < 6000) {
        note(gameNotes[previewNext].hz, gameNotes[previewNext].length * 0.9);
        previewNext++;
    }
}

void menuLeds() {
    for (int i = 0; i < 12; i++) {
        leds[i] = ROW_COLOURS[i / 4];
        leds[i].nscale8(70);
        if (i == previewSong && (millis() / 250) % 2) leds[i] = CRGB::White;
    }
    leds[KEY_FREE]   = CRGB(60, 60, 60);
    leds[KEY_MODE]   = learnMode ? CRGB(0, 60, 255) : CRGB(255, 0, 0);
    leds[KEY_LEVEL]  = level == 0 ? CRGB(0, 220, 0) : level == 1 ? CRGB(255, 160, 0) : CRGB(255, 0, 0);
    uint8_t v = volumeLevel == 0 ? 30 : volumeLevel == 1 ? 110 : 255;
    leds[KEY_VOLUME] = CRGB(v, v, v);
}

// ---------- Playing ----------

// The drum beat, one eighth note at a time
void playBeat(long eighth) {
    int perBar = beatsPerBar * 2;
    int pos = eighth % perBar;
    bool onBeat = pos % 2 == 0;
    int beat = pos / 2;

    if (eighth == 0) drum(0, 0.5);                  // crash on the very first beat
    if (onBeat && beat == 0) drum(12, 0.8);         // kick on beat 1
    if (beatsPerBar == 4) {
        if (onBeat && beat == 2) drum(12, 0.7);     // kick on beat 3
        if (onBeat && (beat == 1 || beat == 3)) drum(13, 0.6);   // snare on 2 and 4
        drum(14, onBeat ? 0.35 : 0.2);              // hi-hat on every eighth
    } else {                                        // waltz: ONE two three
        if (onBeat && beat > 0) drum(14, 0.35);
    }
}

void finishGame() {
    int points = 0;
    for (int i = 0; i < noteCount; i++) {
        if (gameNotes[i].result == PERFECT) points += 2;
        if (gameNotes[i].result == GOOD) points += 1;
    }
    resultPercent = noteCount ? points * 100 / (noteCount * 2) : 0;
    int best = prefs.getInt(bestKey(currentSong).c_str(), -1);
    newBest = resultPercent > best;
    if (newBest) prefs.putInt(bestKey(currentSong).c_str(), resultPercent);

    // A little tune: happy rising notes, or two calm ones
    if (resultPercent >= 60) { note(523, 150); note(659, 150); }
    else note(392, 300);
    resultStart = millis();
    screen = RESULT;
}

void playingUpdate(uint16_t pressed) {
    unsigned long now = millis();
    if (!waiting) songClock += now - lastTick;
    lastTick = now;

    // Count-in: one click and a white flash per beat
    if (songClock < 0) {
        int beat = (int)floorf(songClock / beatMs);
        if (beat != lastCountBeat) {
            lastCountBeat = beat;
            drum(14, 0.6);
            for (int i = 0; i < NUM_PADS; i++) flash(i, CRGB(60, 60, 60));
        }
    }

    // Learn mode: stop at the next note until it is pressed
    if (learnMode && nextNote < noteCount && songClock >= gameNotes[nextNote].start) {
        songClock = gameNotes[nextNote].start;
        waiting = true;
    }

    // Challenge mode: notes that went by unplayed are missed
    if (!learnMode) {
        while (nextNote < noteCount && songClock > gameNotes[nextNote].start + GOOD_MS) {
            gameNotes[nextNote].result = MISSED;
            flash(gameNotes[nextNote].pad, CRGB::Red);
            nextNote++;
        }
    }

    // Drums follow the song clock
    float eighthMs = beatMs / 2;
    while (songClock >= 0 && nextEighth * eighthMs <= songClock && nextEighth * eighthMs < songEnd - 600) {
        playBeat(nextEighth++);
    }

    // Presses
    for (int p = 0; p < NUM_PADS; p++) {
        if (!(pressed & (1 << p))) continue;
        if (nextNote >= noteCount) continue;
        GameNote &n = gameNotes[nextNote];
        float early = n.start - songClock;          // ms until the note is due
        bool inTime = learnMode ? (waiting || early <= GOOD_MS) : fabsf(early) <= GOOD_MS;

        if (p == n.pad && inTime) {
            bool perfect = learnMode ? !n.wrongFirst : fabsf(early) <= PERFECT_MS;
            n.result = perfect ? PERFECT : GOOD;
            flash(p, perfect ? CRGB::Green : CRGB(255, 160, 0));
            note(n.hz, n.length * 0.9);
            if (learnMode && songClock < n.start) songClock = n.start;
            nextNote++;
            waiting = false;
        } else {
            flash(p, CRGB::Red);                    // wrong pad (or far too early)
            if (learnMode) n.wrongFirst = true;
        }
    }

    if (nextNote >= noteCount && songClock >= songEnd) finishGame();
}

void playingLeds() {
    fill_solid(leds, NUM_PADS, CRGB::Black);
    float lead = max(beatMs, 450.0f);               // lights come on this early
    for (int k = 0; k < 2 && nextNote + k < noteCount; k++) {
        GameNote &n = gameNotes[nextNote + k];
        float level01 = 1 - (n.start - songClock) / lead;
        if (level01 <= 0) continue;
        if (level01 > 1) level01 = 1;
        if (k == 0 && waiting) level01 = 0.6f + 0.4f * sinf(millis() / 120.0f);   // pulse
        CRGB c = k == 0 ? CRGB(255, 255, 255) : CRGB(0, 120, 140);
        c.nscale8(level01 * (k == 0 ? 255 : 120));
        leds[n.pad] += c;
    }
}

// ---------- Results ----------

void resultLeds() {
    // Fill pads from the bottom: 16 lit = 100 %
    unsigned long t = millis() - resultStart;
    int target = (resultPercent * 16 + 50) / 100;
    int lit = min((long)target, (long)(t / 60));
    CRGB colour = resultPercent >= 90 ? CRGB(255, 200, 0) : resultPercent >= 70 ? CRGB::Green
                : resultPercent >= 40 ? CRGB(255, 160, 0) : CRGB::Red;
    fill_solid(leds, NUM_PADS, CRGB::Black);
    for (int i = 0; i < lit; i++) leds[PADS_LOW_TO_HIGH[i]] = colour;
    if (newBest && t > 1200 && t < 4000) {         // new best score: rainbow
        for (int i = 0; i < NUM_PADS; i++) leds[i] = CHSV(i * 16 + t / 4, 255, 255);
    }
}

// ---------- Free drums ----------

uint8_t glow[NUM_PADS];

void drumLeds() {
    for (int i = 0; i < NUM_PADS; i++) {
        if (glow[i] > 25) glow[i] = glow[i] - (glow[i] - 25) / 6 - 1;
        else glow[i] = 25;
        leds[i] = DRUM_COLOURS[i];
        leds[i].nscale8(glow[i]);
    }
}

// ---------- MIDI ----------

void midiUpdate(uint16_t pressed, uint16_t released) {
    for (int i = 0; i < NUM_PADS; i++) {
        if (pressed & (1 << i))  { MIDI.noteOn(NOTES[i], VELOCITY, DRUM_CHANNEL); glow[i] = 255; }
        if (released & (1 << i)) MIDI.noteOff(NOTES[i], 0, DRUM_CHANNEL);
    }
    // Notes from the computer light the pads too
    midiEventPacket_t packet;
    while (MIDI.readPacket(&packet)) {
        uint8_t status = packet.byte1 & 0xF0;
        uint8_t channel = (packet.byte1 & 0x0F) + 1;
        if (status == 0x90 && packet.byte3 > 0 && channel == DRUM_CHANNEL) {
            for (int i = 0; i < NUM_PADS; i++) if (NOTES[i] == packet.byte2) glow[i] = 255;
        }
    }
}

// =====================================================================
// Main
// =====================================================================

void setup() {
    Serial.begin(115200);

    MIDI.begin();
    USB.onEvent(usbEvent);
    USB.begin();

    Wire.begin(PIN_SDA, PIN_SCL);
    Wire.setClock(100000);
    Wire.setTimeOut(5);

    FastLED.addLeds<WS2812B, PIN_LED, GRB>(leds, NUM_PADS);
    FastLED.setBrightness(BRIGHTNESS);

    prefs.begin("midipad", false);
    learnMode = prefs.getBool("learn", true);
    level = prefs.getInt("level", 0);
    volumeLevel = prefs.getInt("volume", 1);
    masterVolume = VOLUMES[volumeLevel];

    startSpeaker();

    // Start-up sweep so you know it is alive
    for (int i = 0; i < NUM_PADS; i++) {
        leds[PADS_LOW_TO_HIGH[i]] = CHSV(i * 16, 255, 200);
        FastLED.show();
        delay(30);
    }
    delay(400);
    for (int i = 0; i < NUM_PADS; i++) glow[i] = 0;
}

void loop() {
    uint16_t pressed, released;
    readKeys(pressed, released);

    // Plugging in a computer turns it into MIDI pads; unplugging goes to the menu
    if (computer && screen != MIDI_MODE) { silence(); screen = MIDI_MODE; }
    if (!computer && screen == MIDI_MODE) goMenu();

    if (screen != MENU && screen != MIDI_MODE && exitHeld()) {
        goMenu();
        while (scanKeys()) delay(10);               // wait until the keys are let go
        keysDown = 0;
        return;
    }

    switch (screen) {
        case MENU:
            menuKeys(pressed);
            updatePreview();
            break;
        case PLAYING:
            playingUpdate(pressed);
            break;
        case RESULT:
            if (pressed && millis() - resultStart > 1500) goMenu();
            break;
        case FREE_DRUMS:
            for (int i = 0; i < NUM_PADS; i++) {
                if (pressed & (1 << i)) { drum(i, 1.0); glow[i] = 255; }
            }
            break;
        case MIDI_MODE:
            midiUpdate(pressed, released);
            break;
    }

    // LEDs, about 60 times a second
    static unsigned long lastLed = 0;
    if (millis() - lastLed >= 16) {
        lastLed = millis();
        if (screen == MENU)                                 menuLeds();
        else if (screen == PLAYING)                         playingLeds();
        else if (screen == RESULT)                          resultLeds();
        else                                                drumLeds();
        showLeds();
    }
}
