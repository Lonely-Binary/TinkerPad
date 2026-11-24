// UkuleleTeacher - learn a song on the ukulele, one phrase at a time
// Board: ESP32-S3 N16R8 (16 MB flash, 8 MB PSRAM)
// Setup guide: see ../../01_HardwareTest/README.md
//
// The keypad is a ukulele fretboard - the way you see it when you look down
// at a ukulele while playing it. What you learn here is exactly where your
// fingers go on a real ukulele:
//
//      G string:  [open]  [fret 1]  [fret 2]  [fret 3]     <- top row
//      C string:  [open]  [fret 1]  [fret 2]  [fret 3]
//      E string:  [open]  [fret 1]  [fret 2]  [fret 3]
//      A string:  [open]  [fret 1]  [fret 2]  [fret 3]     <- bottom row
//
// Each string has its own colour: G purple, C blue, E green, A orange.
//
// Choosing a song
//   The top two rows are 8 songs (gold = a song you have already learned).
//   Press one to hear how it starts, press it again to start the lesson.
//   The bottom-left key (white) is free play: just play the ukulele.
//
// A lesson - you never have to remember anything, the lights always lead:
//   For every phrase of the song (one or two bars):
//     1. LISTEN  the teacher plays the phrase; the keys light up as it plays.
//     2. PLAY    you play it: the key to press blinks white, the key after
//                that glows softly so you can get your finger ready.
//   Then you play the whole song the same way, following the lights.
//   Well done: the teacher plays it back for you with a rainbow.
//
// Hold the two TOP CORNER keys for 1.5 s to go back to the songs.
//
// Arduino IDE settings (same as the other TinkerPad games):
//   Tools > USB Mode:        USB-OTG (TinyUSB)
//   Tools > USB CDC On Boot: Enabled
//   Tools > Upload Mode:     USB-OTG CDC (TinyUSB)
// If an upload fails: hold BOOT, tap RESET, release BOOT, upload again.
// Needs the "FastLED" library.
// Sounds: ukulele recorded by Mateusz Dabrowski for FreePats, effects by
// Kenney - all CC0. Songs: see songs.h (add your own there, in jianpu).

#include <Wire.h>
#include <FastLED.h>
#include <Preferences.h>
#include <driver/i2s_std.h>
#include "sounds.h"
#include "lesson.h"

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
#define KEY_FREE_PLAY 12            // bottom-left key in the song menu

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

// The ukulele was recorded on these notes; the others are a neighbour played
// a little faster (higher) or slower (lower)
const Sound UKE[8]      = {UKE_C4, UKE_D4, UKE_E4, UKE_FS4, UKE_G4, UKE_A4, UKE_B4, UKE_CS5};
const int   UKE_MIDI[8] = {60, 62, 64, 66, 67, 69, 71, 73};

void playNote(int midi) {
    int nearest = 0;
    for (int i = 1; i < 8; i++) {
        if (abs(UKE_MIDI[i] - midi) < abs(UKE_MIDI[nearest] - midi)) nearest = i;
    }
    play(UKE[nearest], powf(2, (midi - UKE_MIDI[nearest]) / 12.0f));   // 1 semitone = x1.059
}

// =====================================================================
// Keys
// =====================================================================

uint16_t keysDown = 0;

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
    static unsigned long lastChange[NUM_PADS];
    uint16_t raw = scanKeys(), pressed = 0;
    for (int i = 0; i < NUM_PADS; i++) {
        bool now = raw & (1 << i), was = keysDown & (1 << i);
        if (now == was || millis() - lastChange[i] < 15) continue;
        lastChange[i] = millis();
        if (now) { keysDown |= 1 << i; pressed |= 1 << i; }
        else keysDown &= ~(1 << i);
    }
    return pressed;
}

// True once the two top corner keys have been held for 1.5 s
bool exitHeld() {
    static unsigned long since = 0;
    if ((keysDown & 0b1001) != 0b1001) { since = 0; return false; }
    if (since == 0) since = millis();
    return millis() - since > 1500;
}

// =====================================================================
// LEDs
// =====================================================================

// String colours, top row to bottom: G purple, C blue, E green, A orange
const CRGB STRING_COLOURS[4] = {CRGB(170, 0, 255), CRGB(0, 80, 255), CRGB(0, 220, 60), CRGB(255, 110, 0)};

CRGB    flashColour[NUM_PADS];
uint8_t flashLevel[NUM_PADS];

void flash(int pad, CRGB colour) { flashColour[pad] = colour; flashLevel[pad] = 255; }

// The fretboard, very dim, so you always know where the strings are
void drawFretboard(uint8_t level) {
    for (int i = 0; i < NUM_PADS; i++) {
        leds[i] = STRING_COLOURS[i / 4];
        leds[i].nscale8(level);
    }
}

void showLeds() {
    for (int i = 0; i < NUM_PADS; i++) {
        if (!flashLevel[i]) continue;
        CRGB f = flashColour[i];
        f.nscale8(flashLevel[i]);
        leds[i] += f;
        flashLevel[i] = flashLevel[i] > 12 ? flashLevel[i] - 12 : 0;
    }
    FastLED.show();
}

// =====================================================================
// The lesson plan: a list of steps, worked through one after another
// =====================================================================

enum StepKind { LISTEN, FOLLOW, FINALE };
struct Step { StepKind kind; int from, to; };   // notes from .. to-1

#define MAX_STEPS 80
Step plan[MAX_STEPS];
int  planLength, planIndex;

void addStep(int kind, int from, int to) {         // kind: LISTEN, FOLLOW ...
    if (planLength < MAX_STEPS) plan[planLength++] = {(StepKind)kind, from, to};
}

// For each phrase: listen, then play it following the lights.
// Then the whole song (following the lights), and the finale.
void makePlan() {
    planLength = planIndex = 0;
    for (int p = 0; p < phraseCount; p++) {
        int from = phraseStart[p], to = phraseStart[p + 1];
        addStep(LISTEN, from, to);
        addStep(FOLLOW, from, to);
    }
    if (phraseCount > 1) addStep(FOLLOW, 0, noteCount);
    addStep(FINALE, 0, noteCount);
}

// =====================================================================
// What is happening right now
// =====================================================================

enum Screen { MENU, LESSON, FREE_PLAY };
Screen screen = MENU;

int  song = -1;                     // the song being taught
int  preview = -1;                  // menu: the song being previewed
float beatMs = 600;

// Playing notes to the student (LISTEN, FINALE and the menu preview)
bool  playing = false;
int   playFrom, playTo, playPos;
unsigned long nextNoteAt, playEnd;
int   playingPad = -1;

// The student playing (FOLLOW)
int   target;                       // note index to press next

unsigned long waitUntil = 0;        // a short pause between steps
bool started = false;               // has the current step begun?
bool learned[8];                    // songs already learned (shown gold)

void startPlaying(int from, int to) {
    playing = true;
    playFrom = from;
    playTo = to;
    playPos = from;
    nextNoteAt = millis() + 500;
    playingPad = -1;
}

// Called all the time: plays the next note when it is due
void updatePlaying() {
    if (!playing || millis() < nextNoteAt) return;
    if (playPos >= playTo) {
        playing = false;
        playingPad = -1;
        return;
    }
    Note &n = notes[playPos++];
    if (n.midi >= 0) playNote(n.midi);
    playingPad = n.pad;
    nextNoteAt += n.beats * beatMs;
}

void startStep() {
    Step &s = plan[planIndex];
    if (s.kind == LISTEN) {
        startPlaying(s.from, s.to);
    } else if (s.kind == FINALE) {
        play(CHEER);
        prefs.putBool(("learned" + String(song)).c_str(), true);
        if (song < 8) learned[song] = true;
        startPlaying(0, noteCount);
        nextNoteAt = millis() + 1500;
    } else {                                        // FOLLOW
        target = nextPlayed(s.from, s.to);
        play(YOUR_TURN);
    }
}

void nextStep() {
    planIndex++;
    if (planIndex >= planLength) {                  // lesson finished
        screen = MENU;
        return;
    }
    waitUntil = millis() + 700;
}

void startLesson(int number) {
    song = number;
    char problem[12];
    loadSong(SONGS[song], problem);
    beatMs = 60000.0f / SONGS[song].bpm;
    makePlan();
    play(GO);
    screen = LESSON;
    planIndex = 0;
    playing = false;
    started = false;
    waitUntil = millis() + 800;
}

void lessonUpdate(uint16_t pressed) {
    if (millis() < waitUntil) return;
    Step &s = plan[planIndex];
    if (!started) { startStep(); started = true; return; }

    // Steps where the teacher plays
    if (s.kind == LISTEN || s.kind == FINALE) {
        updatePlaying();
        if (!playing) {
            nextStep();
            started = false;
        }
        return;
    }

    // Steps where the student plays
    for (int p = 0; p < NUM_PADS; p++) {
        if (!(pressed & (1 << p))) continue;
        Note &n = notes[target];
        if (p == n.pad) {
            playNote(n.midi);
            flash(p, CRGB(0, 255, 0));
            target = nextPlayed(target + 1, s.to);
            if (target >= s.to) {                   // the whole phrase: well done
                play(WELL_DONE, 1, 0.7);
                for (int i = 0; i < NUM_PADS; i++) flash(i, CRGB(0, 90, 0));
                nextStep();
                started = false;
                return;
            }
        } else {
            play(WRONG);
            flash(p, CRGB(255, 0, 0));
            flash(n.pad, CRGB::White);              // show where it should be
        }
    }
}

// =====================================================================
// Drawing
// =====================================================================

void drawMenu() {
    fill_solid(leds, NUM_PADS, CRGB::Black);
    for (int i = 0; i < SONG_COUNT && i < 8; i++) {
        leds[i] = learned[i] ? CRGB(255, 180, 0) : (i < 4 ? CRGB(0, 200, 60) : CRGB(30, 90, 255));
        leds[i].nscale8(70);
        if (i == preview && (millis() / 250) % 2) leds[i] = CRGB::White;
    }
    leds[KEY_FREE_PLAY] = CRGB(70, 70, 70);
    // While previewing, show the notes on the fretboard rows below the songs
    if (preview >= 0 && playingPad >= 8) leds[playingPad] = STRING_COLOURS[playingPad / 4];
}

void drawLesson() {
    drawFretboard(10);
    Step &s = plan[planIndex];
    bool teacherPlays = s.kind == LISTEN || s.kind == FINALE;

    if (teacherPlays && playing && playingPad >= 0) {
        leds[playingPad] = STRING_COLOURS[playingPad / 4];         // the note being played
        if (s.kind == FINALE) {                                     // finale: a gentle rainbow
            for (int i = 0; i < NUM_PADS; i++) if (i != playingPad) leds[i] = CHSV(i * 16 + millis() / 8, 255, 40);
        }
    }
    if (!teacherPlays && started && millis() >= waitUntil && target < s.to) {
        // The note after this one glows softly in its string colour...
        int after = nextPlayed(target + 1, s.to);
        if (after < s.to && notes[after].pad != notes[target].pad) {
            int pad = notes[after].pad;
            leds[pad] = STRING_COLOURS[pad / 4];
            leds[pad].nscale8(90);
        }
        // ...and the note to press now blinks bright white / string colour
        int pad = notes[target].pad;
        leds[pad] = (millis() / 200) % 2 ? CRGB::White : STRING_COLOURS[pad / 4];
    }
}

void drawFreePlay() {
    drawFretboard(25);
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
    prefs.begin("ukulele", false);
    startSpeaker();

    for (int i = 0; i < 8; i++) learned[i] = prefs.getBool(("learned" + String(i)).c_str(), false);

    // Check every song fits the fretboard (prints a warning if not)
    for (int i = 0; i < SONG_COUNT; i++) {
        char problem[12];
        if (!loadSong(SONGS[i], problem)) Serial.printf("Song %d: note %s does not fit\n", i + 1, problem);
    }
}

void loop() {
    uint16_t pressed = newPresses();

    if (screen != MENU && exitHeld()) {
        screen = MENU;
        preview = -1;
        playing = false;
        while (scanKeys()) delay(10);
        keysDown = 0;
        return;
    }

    switch (screen) {
        case MENU:
            updatePlaying();                        // the preview
            for (int k = 0; k < NUM_PADS; k++) {
                if (!(pressed & (1 << k))) continue;
                if (k < SONG_COUNT && k < 8) {
                    if (k == preview) { preview = -1; startLesson(k); }
                    else {                          // first press: hear how it starts
                        preview = k;
                        char problem[12];
                        loadSong(SONGS[k], problem);
                        beatMs = 60000.0f / SONGS[k].bpm;
                        startPlaying(0, phraseStart[min(1, phraseCount)]);
                    }
                } else if (k == KEY_FREE_PLAY) {
                    preview = -1;
                    playing = false;
                    screen = FREE_PLAY;
                }
            }
            break;
        case LESSON:
            lessonUpdate(pressed);
            break;
        case FREE_PLAY:
            for (int p = 0; p < NUM_PADS; p++) {
                if (pressed & (1 << p)) { playNote(noteOfPad(p)); flash(p, STRING_COLOURS[p / 4]); }
            }
            break;
    }

    static unsigned long lastLed = 0;
    if (millis() - lastLed >= 16) {
        lastLed = millis();
        if (screen == MENU)        drawMenu();
        else if (screen == LESSON) drawLesson();
        else                       drawFreePlay();
        showLeds();
    }
}
