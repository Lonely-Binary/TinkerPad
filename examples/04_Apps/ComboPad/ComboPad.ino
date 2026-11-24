// ComboPad - a 16-key fighting-game combo keyboard (King of Fighters, Street Fighter)
// Board: ESP32-S3 N16R8 (16 MB flash, 8 MB PSRAM)
// Setup guide: see ../../01_HardwareTest/README.md
//
// Every key plays a whole combo, frame by frame (60 frames per second), as a
// USB keyboard. Plug the board's "USB" Type-C socket into the computer that
// runs the game. Press a key: the combo plays. Press it again: it stops.
//
// Mirror layout (on by default): the left two columns are for when your
// character faces RIGHT, and the right two columns play the same combos
// mirrored, for when your character faces LEFT:
//
//      [1] [2] | [2'] [1']
//      [3] [4] | [4'] [3']
//      [5] [6] | [6'] [5']
//      [7] [8] | [8'] [7']
//
// Setting up the combos (Wi-Fi):
//   1. Wi-Fi turns on for 10 minutes every time the board starts.
//      Later, hold the BOOT button for 3 seconds to turn it on again.
//   2. Join Wi-Fi "ComboPad-XXXX" (open network, no password).
//   3. Open http://192.168.4.1 - pick a game, a character, a combo,
//      or paste your own combo notation.
//   After 10 minutes Wi-Fi turns off by itself (best timing while you play).
//   Key 1 blinks blue while Wi-Fi is on.
//
// Arduino IDE settings (different from the other sketches!):
//   Tools > USB Mode:        USB-OTG (TinyUSB)
//   Tools > USB CDC On Boot: Enabled
//   Tools > Upload Mode:     USB-OTG CDC (TinyUSB)
// If an upload fails: hold BOOT, tap RESET, release BOOT, upload again.
//
// Needs the "FastLED" library. Everything else comes with the ESP32 core.
// For offline / training / emulator play. Most online modes do not allow macros.

#include <Wire.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <FastLED.h>
#include "USB.h"
#include "USBHIDKeyboard.h"
#include "web_page.h"

// ---------- Pins ----------
#define PIN_SDA       9
#define PIN_SCL       8
#define PCF8574_ADDR  0x20
#define PIN_LED       38
#define NUM_KEYS      16
#define BRIGHTNESS    40

#define FRAME_US      16667         // one frame at 60 frames per second
#define MAX_STEPS     96            // longest combo, in steps

#define PIN_BOOT      0             // the BOOT button on the ESP32-S3 board
#define WIFI_MINUTES  10            // how long Wi-Fi stays on

// ---------- Combos ----------
// A combo is a list of steps. Each step holds some inputs for some frames.
//   dir:     direction in "numpad notation" (5 = neutral, 6 = forward ...)
//   buttons: bit 0-5 = game buttons 1-6 (SF: LP MP HP LK MK HK, KOF: A B C D)
//   frames:  how long to hold it
struct Step {
    uint8_t dir;
    uint8_t buttons;
    uint8_t frames;
};

struct Combo {
    Step steps[MAX_STEPS];
    int  count;
};

Combo   combos[NUM_KEYS];
uint8_t keyMap[10];                 // HID codes for: up down left right, buttons 1-6
String  config;                     // all settings as text (the page reads it back)

// ---------- Hardware ----------
USBHIDKeyboard keyboard;
Preferences    prefs;
WebServer      server(80);
DNSServer      dns;
CRGB           leds[NUM_KEYS];
bool           wifiOn = false;
unsigned long  wifiOffTime = 0;     // millis() when Wi-Fi turns off

// ---------- Reading the settings text ----------
// The page saves everything as lines like:
//   map 82 81 80 79 24 12 18 13 14 15
//   k0 Name|notation|2.0.2 3.0.2 6.4.3
// Only "map" and the steps after the last "|" matter to the firmware.

void parseConfig() {
    memset(combos, 0, sizeof(combos));
    // Default keys: arrows + U I O J K L (overwritten by the "map" line)
    const uint8_t defaults[10] = {0x52, 0x51, 0x50, 0x4F, 0x18, 0x0C, 0x12, 0x0D, 0x0E, 0x0F};
    memcpy(keyMap, defaults, sizeof(keyMap));

    int start = 0;
    while (start < (int)config.length()) {
        int end = config.indexOf('\n', start);
        if (end < 0) end = config.length();
        String line = config.substring(start, end);
        start = end + 1;

        if (line.startsWith("map ")) {
            int v[10];
            if (sscanf(line.c_str() + 4, "%d %d %d %d %d %d %d %d %d %d",
                       &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7], &v[8], &v[9]) == 10) {
                for (int i = 0; i < 10; i++) keyMap[i] = v[i];
            }
        } else if (line.startsWith("k")) {
            int key = line.substring(1, line.indexOf(' ')).toInt();
            int bar = line.lastIndexOf('|');
            if (key < 0 || key >= NUM_KEYS || bar < 0) continue;
            parseSteps(line.substring(bar + 1), combos[key]);
        }
    }
}

// "2.0.2 3.0.2 6.4.3" -> three steps (dir.buttons.frames)
void parseSteps(const String &text, Combo &combo) {
    combo.count = 0;
    const char *p = text.c_str();
    int dir, buttons, frames, used;
    while (combo.count < MAX_STEPS && sscanf(p, " %d.%d.%d%n", &dir, &buttons, &frames, &used) == 3) {
        combo.steps[combo.count++] = {(uint8_t)dir, (uint8_t)buttons, (uint8_t)frames};
        p += used;
    }
}

// ---------- Playing a combo ----------

Combo        *playing = nullptr;    // combo being played, or nullptr
int           playingKey = -1;
int           stepIndex = 0;
unsigned long stepEnd = 0;          // micros() when the current step ends
Combo         testCombo;            // combo sent from the page with "Test"

// Turn one step into a USB keyboard report and send it
void sendStep(const Step &s) {
    KeyReport report = {};
    int n = 0;
    // Numpad direction -> arrow keys (facing right: 6 = right = forward)
    bool up    = s.dir >= 7;
    bool down  = s.dir >= 1 && s.dir <= 3;
    bool left  = s.dir == 1 || s.dir == 4 || s.dir == 7;
    bool right = s.dir == 3 || s.dir == 6 || s.dir == 9;
    if (up)    report.keys[n++] = keyMap[0];
    if (down)  report.keys[n++] = keyMap[1];
    if (left)  report.keys[n++] = keyMap[2];
    if (right) report.keys[n++] = keyMap[3];
    for (int b = 0; b < 6 && n < 6; b++) {
        if (s.buttons & (1 << b)) report.keys[n++] = keyMap[4 + b];
    }
    keyboard.sendReport(&report);
}

void releaseAll() {
    KeyReport report = {};
    keyboard.sendReport(&report);
}

void startCombo(Combo *combo, int key) {
    if (combo->count == 0) return;
    playing = combo;
    playingKey = key;
    stepIndex = 0;
    sendStep(playing->steps[0]);
    stepEnd = micros() + playing->steps[0].frames * FRAME_US;
}

void stopCombo() {
    playing = nullptr;
    playingKey = -1;
    releaseAll();
}

// Called all the time: moves to the next step when its frames are up
void updateCombo() {
    if (!playing) return;
    if ((long)(micros() - stepEnd) < 0) return;

    stepIndex++;
    if (stepIndex >= playing->count) {
        stopCombo();
        return;
    }
    sendStep(playing->steps[stepIndex]);
    stepEnd += playing->steps[stepIndex].frames * FRAME_US;   // no drift
}

// ---------- Keys and LEDs ----------

// Returns the number (0-15) of a pressed key, or -1 if no key is pressed
int findKey() {
    for (int row = 0; row < 4; row++) {
        Wire.beginTransmission(PCF8574_ADDR);
        Wire.write(0xF0 | (0x0F & ~(1 << row)));    // pull this row low
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

// Keys with a combo glow softly: teal on the left half, orange on the right
// half. The key that is playing is bright white.
void updateLeds() {
    for (int i = 0; i < NUM_KEYS; i++) {
        if (i == playingKey)            leds[i] = CRGB::White;
        else if (combos[i].count == 0)  leds[i] = CRGB::Black;
        else if (i % 4 < 2)             leds[i] = CRGB(0x14, 0x89, 0x8F).nscale8(90);
        else                            leds[i] = CRGB(0xE0, 0x70, 0x10).nscale8(90);
    }
    // In setup mode, key 1 blinks so you know Wi-Fi is on
    if (wifiOn && playingKey != 0 && (millis() / 500) % 2) leds[0] = CRGB(0, 0, 60);
    FastLED.show();
}

// ---------- Web server ----------

void handleGetConfig() {
    server.send(200, "text/plain", config);
}

void handleSave() {
    config = server.arg("plain");                   // the whole body
    prefs.putBytes("config", config.c_str(), config.length());
    stopCombo();
    parseConfig();
    server.send(200, "text/plain", "ok");
}

// Play steps sent from the page once ("2.0.2 3.0.2 6.4.3")
void handleTest() {
    parseSteps(server.arg("plain"), testCombo);
    stopCombo();
    startCombo(&testCombo, -1);
    server.send(200, "text/plain", "ok");
}

// How many seconds until Wi-Fi turns off
void handleGetTimer() {
    long left = (long)(wifiOffTime - millis()) / 1000;
    server.send(200, "text/plain", String(left > 0 ? left : 0));
}

// "Reset timer" button: another 10 minutes
void handleResetTimer() {
    wifiOffTime = millis() + WIFI_MINUTES * 60000UL;
    handleGetTimer();
}

// "Reset to defaults" button: forget every setting
void handleFactoryReset() {
    prefs.remove("config");
    config = "";
    stopCombo();
    parseConfig();
    server.send(200, "text/plain", "ok");
}

// Tell the web server which page / API answers which address
void startWebServer() {
    server.on("/", HTTP_GET, []() { server.send(200, "text/html", WEB_PAGE); });
    server.on("/api/config", HTTP_GET, handleGetConfig);
    server.on("/api/save", HTTP_POST, handleSave);
    server.on("/api/test", HTTP_POST, handleTest);
    server.on("/api/timer", HTTP_GET, handleGetTimer);
    server.on("/api/timer", HTTP_POST, handleResetTimer);
    server.on("/api/reset", HTTP_POST, handleFactoryReset);
    server.onNotFound([]() {
        server.sendHeader("Location", "http://192.168.4.1/");
        server.send(302, "text/plain", "");
    });
}

// ---------- Wi-Fi on / off ----------

void wifiStart() {
    wifiOffTime = millis() + WIFI_MINUTES * 60000UL;
    if (wifiOn) return;                             // already on: just more time

    uint64_t mac = ESP.getEfuseMac();
    char name[20];
    snprintf(name, sizeof(name), "ComboPad-%02X%02X", (uint8_t)(mac >> 32), (uint8_t)(mac >> 40));

    WiFi.mode(WIFI_AP);
    WiFi.softAP(name);                              // open network: no password
    dns.start(53, "*", WiFi.softAPIP());            // the page opens by itself
    server.begin();
    wifiOn = true;
    Serial.printf("Wi-Fi on for %d minutes: join \"%s\" (no password), open http://192.168.4.1\n",
                  WIFI_MINUTES, name);
}

void wifiStop() {
    server.stop();
    dns.stop();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
    wifiOn = false;
    Serial.println("Wi-Fi off. Hold BOOT for 3 seconds to turn it on again.");
}

// Turn Wi-Fi off when the time is up, and on when BOOT is held for 3 s
void updateWiFi() {
    static unsigned long bootPressed = 0;           // when BOOT went down (0 = not down)
    static bool done = false;                       // already turned Wi-Fi on for this press

    if (wifiOn) {
        dns.processNextRequest();
        server.handleClient();
        if ((long)(millis() - wifiOffTime) >= 0) wifiStop();
    }

    if (digitalRead(PIN_BOOT) == LOW) {             // BOOT is pressed
        if (bootPressed == 0) bootPressed = millis();
        if (!done && millis() - bootPressed >= 3000) {
            wifiStart();
            done = true;
        }
    } else {
        bootPressed = 0;
        done = false;
    }
}

// ---------- Main ----------

void setup() {
    Serial.begin(115200);

    // Show up on the computer as a USB keyboard (named "ESP32S3_DEV")
    keyboard.begin();
    USB.begin();

    Wire.begin(PIN_SDA, PIN_SCL);
    Wire.setTimeOut(5);

    FastLED.addLeds<WS2812B, PIN_LED, GRB>(leds, NUM_KEYS);
    FastLED.setBrightness(BRIGHTNESS);
    FastLED.clear(true);

    // Load the saved settings
    prefs.begin("combopad", false);
    size_t size = prefs.getBytesLength("config");
    if (size > 0) {
        char *buf = (char *)malloc(size + 1);
        prefs.getBytes("config", buf, size);
        buf[size] = 0;
        config = buf;
        free(buf);
    }
    parseConfig();

    pinMode(PIN_BOOT, INPUT_PULLUP);
    startWebServer();
    wifiStart();                                    // Wi-Fi on for the first 10 minutes
}

void loop() {
    static int heldKey = -1;                        // last key we acted on
    static int lastRead = -1;
    static unsigned long changeTime = 0;

    updateCombo();                                  // keep the frames exact

    updateWiFi();

    // Debounce: only believe a reading once it has stayed the same for 10 ms
    int key = findKey();
    if (key != lastRead) {
        lastRead = key;
        changeTime = millis();
    }
    if (millis() - changeTime >= 10 && key != heldKey) {
        heldKey = key;
        if (key >= 0) {
            if (key == playingKey) {
                stopCombo();                        // same key again: stop
            } else {
                stopCombo();
                startCombo(&combos[key], key);      // play this key's combo
            }
        }
    }

    static unsigned long lastLed = 0;
    if (millis() - lastLed >= 30) {
        lastLed = millis();
        updateLeds();
    }
}
