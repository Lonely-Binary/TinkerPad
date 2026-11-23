#include "ledui.h"

#include <FastLED.h>
#include <math.h>

#include "config.h"
#include "layout.h"
#include "mic.h"
#include "pins.h"

namespace LedUI {

static CRGB g_leds[NUM_LEDS];

// The status pixel on the ESP32-S3 board, separate from the 16 key LEDs.
static const uint8_t kStatusPin = 48;
static CRGB g_status[1];
static NetStatus g_net = NET_ESPNOW;
static bool g_netPeers = false;

// Twelve colours that stay distinguishable at the low brightness the board runs
// at. Neighbouring keys differ in hue, so two people next to each other are never
// confused at a glance.
static const CRGB kPersonColour[kPeople] = {
    CRGB(255, 60, 0),    // 1  red-orange
    CRGB(255, 170, 0),   // 2  amber
    CRGB(170, 230, 40),  // 3  yellow-green
    CRGB(50, 200, 60),   // 4  green
    CRGB(0, 200, 160),   // 5  teal
    CRGB(0, 170, 230),   // 6  cyan
    CRGB(40, 80, 255),   // 7  blue
    CRGB(150, 80, 255),  // 8  violet
    CRGB(230, 50, 200),  // 9  magenta
    CRGB(255, 80, 140),  // 10 pink
    CRGB(230, 220, 190), // 11 warm white
    CRGB(200, 110, 30),  // 12 brown
};

static const CRGB kMicColour(255, 60, 60);
static const CRGB kReplayColour(0, 200, 230);
static const CRGB kDeleteColour(255, 140, 0);
static const CRGB kAllColour(255, 200, 40);

static uint8_t g_unread[kPeople];
static bool g_haveBroadcast = false;
static bool g_haveLast = false;
static int8_t g_playPos = -1;
static uint32_t g_playUntil = 0;
static int8_t g_recPos = -1;
static uint32_t g_recMax = 0;
static int8_t g_ackPos = -1;
static bool g_ackOk = true;
static uint32_t g_ackMs = 0;
static bool g_armed = false;
static uint32_t g_deleteHeld = 0;

static inline CRGB scale(CRGB c, float f) {
    f = constrain(f, 0.0f, 1.0f);
    return CRGB((uint8_t)(c.r * f), (uint8_t)(c.g * f), (uint8_t)(c.b * f));
}

static inline void put(uint8_t pos, CRGB colour) {
    uint8_t led = Layout::ledForPos(pos);
    if (led < NUM_LEDS) g_leds[led] = colour;
}

bool begin() {
    FastLED.addLeds<WS2812B, PIN_LED_DATA, GRB>(g_leds, NUM_LEDS);
    FastLED.addLeds<WS2812, kStatusPin, GRB>(g_status, 1);
    FastLED.setBrightness(kLedBrightness);
    fill_solid(g_leds, NUM_LEDS, CRGB::Black);
    FastLED.show();
    memset(g_unread, 0, sizeof(g_unread));
    return true;
}

void bootSweep() {
    for (uint8_t pos = 0; pos < 16; pos++) {
        fill_solid(g_leds, NUM_LEDS, CRGB::Black);
        put(pos, pos < kPeople ? kPersonColour[pos] : CRGB(200, 200, 200));
        FastLED.show();
        delay(35);
    }
    fill_solid(g_leds, NUM_LEDS, CRGB::Black);
    FastLED.show();
}

void setUnread(uint8_t person, uint8_t count) {
    if (person < kPeople) g_unread[person] = count;
}
void setHaveBroadcast(bool have) { g_haveBroadcast = have; }
void setHaveLast(bool have) { g_haveLast = have; }
void setPlaying(int8_t pos, uint32_t untilMs) {
    g_playPos = pos;
    g_playUntil = untilMs;
}
void setRecording(int8_t pos, uint32_t maxFrames) {
    g_recPos = pos;
    g_recMax = maxFrames;
}
void ack(int8_t pos, bool ok) {
    g_ackPos = pos;
    g_ackOk = ok;
    g_ackMs = millis();
}
void setArmed(bool armed) { g_armed = armed; }

void setNetStatus(NetStatus s, bool peers) {
    g_net = s;
    g_netPeers = peers;
}

static void renderStatus(uint32_t now) {
    float level = g_netPeers ? 0.9f : 0.25f;
    CRGB c;
    bool on = true;
    switch (g_net) {
        case NET_HOTSPOT:
            c = CRGB(0, 60, 255);
            on = (now % 1600) < 800;
            break;
        case NET_WIFI_CONNECTING:
            c = CRGB(255, 170, 0);
            on = (now % 400) < 200;
            break;
        case NET_WIFI_OK:
            c = CRGB(0, 255, 40);
            break;
        default:
            c = CRGB(0, 200, 200);
            break;
    }
    g_status[0] = on ? scale(c, level) : CRGB::Black;
}
void setDeleteHold(uint32_t heldMs) { g_deleteHeld = heldMs; }

// n blinks, then a pause, repeating. Returns 0..1.
static float blinkCount(uint32_t now, uint8_t n) {
    const uint32_t cycle = 2000;
    const uint32_t on = 130, period = 260;
    uint32_t t = now % cycle;
    if (t >= (uint32_t)n * period) return 0.0f;
    return (t % period) < on ? 1.0f : 0.0f;
}

void render() {
    uint32_t now = millis();
    renderStatus(now);
    fill_solid(g_leds, NUM_LEDS, CRGB::Black);

    // --- recording takes the board over, and shows almost nothing: the recipient's
    // key pulsing red with the voice, and MIC steady so it is clear how to stop.
    if (g_recPos >= 0) {
        float pulse = 0.35f + 0.65f * (0.5f + 0.5f * sinf(now * 0.012f));
        float level = constrain(Mic::level() * 6.0f, 0.0f, 1.0f);

        // The last ~15% of the time is announced by a fast blink.
        uint32_t got = Mic::capturedFrames();
        bool nearEnd = g_recMax && got > g_recMax - g_recMax / 7;
        float blink = nearEnd ? (((now / 120) & 1) ? 1.0f : 0.2f) : 1.0f;

        put((uint8_t)g_recPos,
            scale(CRGB(255, 0, 0), pulse * (0.4f + 0.6f * level) * blink));
        put(12, scale(kMicColour, 0.7f));
        FastLED.show();
        return;
    }

    // --- MIC tapped: "who is it for?" The board has to say what to do next, so every
    // person's key lights in its own colour and MIC breathes. Kept at a moderate
    // level on purpose — it is a prompt, not a light show.
    if (g_armed) {
        float breathe = 0.5f + 0.5f * sinf(now * 0.008f);
        for (uint8_t p = 0; p < kPeople; p++) {
            put(p, scale(kPersonColour[p], 0.30f + 0.25f * breathe));
        }
        put(15, scale(kAllColour, 0.30f + 0.25f * breathe));
        put(12, scale(kMicColour, 0.4f + 0.6f * breathe));
        FastLED.show();
        return;
    }

    // --- person keys
    bool playing = g_playPos >= 0 && (int32_t)(g_playUntil - now) > 0;
    for (uint8_t p = 0; p < kPeople; p++) {
        CRGB base = kPersonColour[p];
        float f = 0.0f;  // dark unless something is happening to it
        if (playing && g_playPos == p) {
            f = 0.55f + 0.45f * (0.5f + 0.5f * sinf(now * 0.008f));
        } else if (g_unread[p] > 0) {
            // Lit, not just blinking: a waiting message is a steady glow in the
            // person's colour so it is seen from across the room, with one bright
            // pulse per message (up to 5) riding on top so the number can be read.
            uint8_t n = g_unread[p] > 5 ? 5 : g_unread[p];
            f = 0.35f + 0.65f * blinkCount(now, n);
        }
        put(p, scale(base, f));
    }

    // --- control row
    put(12, scale(kMicColour, 0.10f));

    {
        // Fills over the hold and turns red for the last third, so the delete is
        // announced while there is still time to let go.
        CRGB c = kDeleteColour;
        float f = 0.0f;  // dark until it is held
        if (g_deleteHeld > 0) {
            float t = constrain((float)g_deleteHeld / kDeleteHoldMs, 0.0f, 1.0f);
            f = 0.10f + 0.90f * t;
            if (t > 0.66f) c = CRGB(255, 20, 0);
            if (g_deleteHeld >= kDeleteHoldMs) c = CRGB(255, 255, 255);
        }
        put(14, scale(c, f));
    }

    // ALL: dark unless an announcement is playing.
    if (playing && g_playPos == 15) {
        float f = 0.55f + 0.45f * (0.5f + 0.5f * sinf(now * 0.008f));
        put(15, scale(kAllColour, f));
    }

    // --- acknowledgement overlay: two blinks, ~560 ms
    if (g_ackPos >= 0) {
        uint32_t since = now - g_ackMs;
        if (since < 560) {
            bool on = (since % 280) < 140;
            if (on) {
                put((uint8_t)g_ackPos,
                    g_ackOk ? CRGB(255, 255, 255) : CRGB(255, 0, 0));
            }
        } else {
            g_ackPos = -1;
        }
    }

    FastLED.show();
}

}  // namespace LedUI
