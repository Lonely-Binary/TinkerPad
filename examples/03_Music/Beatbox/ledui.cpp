#include "ledui.h"

#include <FastLED.h>
#include <math.h>

#include "config.h"
#include "layout.h"
#include "looper.h"
#include "mic.h"
#include "pins.h"
#include "rhythm.h"
#include "slots.h"

namespace LedUI {

static CRGB g_leds[NUM_LEDS];

// One colour per slot, grouped by family so a player learns the board by colour
// rather than by position: reds are the low drums, cyans the hats, greens the
// toms, and the two outliers (crash, bass) sit at the ends of the spectrum.
static const CRGB kSlotColour[kSlotCount] = {
    CRGB(255, 60, 0),     // kick
    CRGB(255, 170, 0),    // snare
    CRGB(0, 200, 220),    // hat
    CRGB(0, 150, 200),    // open hat
    CRGB(255, 60, 140),   // clap
    CRGB(50, 200, 60),    // tom lo
    CRGB(110, 220, 50),   // tom mid
    CRGB(170, 230, 40),   // tom hi
    CRGB(230, 195, 40),   // cowbell
    CRGB(220, 220, 190),  // clave
    CRGB(150, 80, 255),   // crash
    CRGB(40, 80, 255),    // bass
};

static float g_flash[kSlotCount];
static int8_t g_sampling = -1;
static uint32_t g_tapFlashMs = 0;
static uint32_t g_undoFlashMs = 0;
static uint32_t g_clearHeldMs = 0;
static uint32_t g_lastFrameMs = 0;

static inline CRGB scale(CRGB c, float f) {
    f = constrain(f, 0.0f, 1.0f);
    return CRGB((uint8_t)(c.r * f), (uint8_t)(c.g * f), (uint8_t)(c.b * f));
}

static inline void put(uint8_t pos, CRGB colour) {
    uint8_t led = Layout::ledForPos(pos);
    if (led < NUM_LEDS) {
        g_leds[led] = colour;
    }
}

bool begin() {
    FastLED.addLeds<WS2812B, PIN_LED_DATA, GRB>(g_leds, NUM_LEDS);
    FastLED.setBrightness(kLedBrightness);
    fill_solid(g_leds, NUM_LEDS, CRGB::Black);
    FastLED.show();
    memset(g_flash, 0, sizeof(g_flash));
    g_lastFrameMs = millis();
    return true;
}

void bootSweep() {
    // Walks the visible positions rather than the chain, so what you see is a
    // left-to-right sweep whichever way the WS2812 chain happens to be wired.
    for (uint8_t pos = 0; pos < 16; pos++) {
        fill_solid(g_leds, NUM_LEDS, CRGB::Black);
        CRGB c = pos < kSlotCount ? kSlotColour[pos] : CRGB(200, 200, 200);
        put(pos, c);
        FastLED.show();
        delay(35);
    }
    fill_solid(g_leds, NUM_LEDS, CRGB::Black);
    FastLED.show();
}

void flash(uint8_t slot) {
    if (slot < kSlotCount) {
        g_flash[slot] = 1.0f;
    }
}

void setSampling(int8_t slot) { g_sampling = slot; }

void flashTap() { g_tapFlashMs = millis(); }

static uint32_t g_kitFlashMs = 0;
static CRGB g_kitColour;
void flashKit(uint32_t rgb) {
    g_kitColour = CRGB((uint8_t)(rgb >> 16), (uint8_t)(rgb >> 8), (uint8_t)rgb);
    g_kitFlashMs = millis();
}

void flashUndo() { g_undoFlashMs = millis(); }

void setClearHold(uint32_t heldMs) { g_clearHeldMs = heldMs; }

void render() {
    uint32_t now = millis();
    float dt = (float)(now - g_lastFrameMs) * 0.001f;
    g_lastFrameMs = now;

    // ~130 ms to fall away. Long enough to read as a hit on a fast pattern,
    // short enough that sixteenth notes do not smear into a steady glow.
    const float decay = expf(-dt / 0.13f);
    for (uint8_t i = 0; i < kSlotCount; i++) {
        g_flash[i] *= decay;
    }

    fill_solid(g_leds, NUM_LEDS, CRGB::Black);

    // --- sampling takes the whole board over
    if (g_sampling >= 0) {
        float pulse = 0.35f + 0.65f * (0.5f + 0.5f * sinf(now * 0.012f));
        // The microphone level rides on top of the pulse, so the player can see
        // that it is hearing them without having to trust that it is.
        float level = constrain(Mic::level() * 6.0f, 0.0f, 1.0f);
        put((uint8_t)g_sampling, scale(CRGB(255, 0, 0), pulse * (0.4f + 0.6f * level)));
        FastLED.show();
        return;
    }

    // --- sound pads
    for (uint8_t slot = 0; slot < kSlotCount; slot++) {
        if (Slots::frames(slot) == 0) {
            continue;  // empty: off
        }
        CRGB base = kSlotColour[slot];
        float f = g_flash[slot];

        // Resting level is low so the flashes have somewhere to go, and so the
        // whole board is not a bright glow that competes with the transport.
        CRGB c = scale(base, 0.10f + 0.90f * f);

        if (Slots::isUser(slot)) {
            // A white core marks a pad the player recorded. Additive so it
            // brightens with the flash instead of washing the colour out.
            uint8_t w = (uint8_t)(30 + 90 * f);
            c += CRGB(w, w, w);
        }
        // A new kit: every pad shows the kit's colour for a moment, fading out.
        uint32_t sinceKit = now - g_kitFlashMs;
        if (g_kitFlashMs && sinceKit < 700) {
            c = scale(g_kitColour, 1.0f - (float)sinceKit / 700.0f);
        }
        put(slot, c);
    }

    // --- MIC. Steady and quiet: it is an invitation, not a state. There is no
    // "armed" appearance to show because there is nothing to arm — while a capture
    // is actually running the whole board is given over to it above.
    put(12, scale(CRGB(255, 60, 60), Looper::recording() ? 0.14f : 0.05f));

    // --- STOP: the bar made visible
    {
        CRGB c = CRGB(0, 255, 90);
        float f;
        if (Looper::playing()) {
            // Squared so the pulse has a percussive shape rather than a
            // triangular fade, which reads as a beat rather than as breathing.
            float within = 1.0f - Looper::beatPhase();
            float env = within * within;
            bool downbeat = Looper::currentBeat() == 0;
            f = 0.08f + (downbeat ? 0.92f : 0.40f) * env;
        } else {
            f = Looper::eventCount() > 0 ? 0.14f : 0.06f;
        }
        put(13, scale(c, f));
    }

    // --- TAP, which also shows which factory rhythm is running.
    //
    // Colour rather than a count, because there is nowhere to display a number
    // and "which groove is this" has to be answerable at a glance. The rhythm
    // colour also pulses on the beat, so TAP reads as the thing that owns tempo.
    {
        uint8_t r = Looper::rhythm();
        uint32_t since = now - g_tapFlashMs;
        float tapFlash = since < 120 ? 1.0f - (float)since / 120.0f : 0.0f;

        if (r == 0) {
            put(14, scale(CRGB(180, 180, 255), 0.06f + 0.94f * tapFlash));
        } else {
            uint32_t packed = Rhythm::colour(r);
            CRGB c((uint8_t)(packed >> 16), (uint8_t)(packed >> 8), (uint8_t)packed);
            float f = 0.18f;
            if (Looper::playing()) {
                float within = 1.0f - Looper::beatPhase();
                f += 0.55f * within * within;
            }
            put(14, scale(c, max(f, tapFlash)));
        }
    }

    // --- UNDO.
    //
    // A tap undoes and fires on release, so it gets a flash afterwards rather than
    // a ramp before. The ramp is for the destructive stage: it fills over the 3 s
    // hold and turns from amber to red well before it fires, so the wipe is
    // announced while there is still time to let go. Nothing about a three-second
    // hold should ever be a surprise.
    {
        const uint32_t warnMs = (uint32_t)kClearAllMs * 3 / 5;
        uint32_t sinceUndo = now - g_undoFlashMs;
        float undoFlash = sinceUndo < 200 ? 1.0f - (float)sinceUndo / 200.0f : 0.0f;

        CRGB c;
        float f;
        if (g_clearHeldMs == 0) {
            c = CRGB(255, 140, 0);
            f = max(0.06f, undoFlash);
        } else if (g_clearHeldMs < warnMs) {
            c = CRGB(255, 140, 0);
            f = 0.06f + 0.44f * (float)g_clearHeldMs / (float)warnMs;
        } else if (g_clearHeldMs < kClearAllMs) {
            c = CRGB(255, 30, 0);
            float t = (float)(g_clearHeldMs - warnMs) / (float)(kClearAllMs - warnMs);
            f = 0.50f + 0.50f * t;
        } else {
            c = CRGB(255, 255, 255);
            f = 1.0f;
        }
        put(15, scale(c, f));
    }

    FastLED.show();
}

}  // namespace LedUI
