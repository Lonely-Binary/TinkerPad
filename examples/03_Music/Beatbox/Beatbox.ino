// TinkerPad — Beatbox
// ESP32-S3 Blueprints No.02.
//
// A pocket vocal loop station. A bar is always going round; everything you play
// lands in it and repeats. Beatbox a kick onto a pad, hum a bass onto another,
// and layer them into a groove on 16 backlit keys. There is no screen: the pads
// are the whole interface.
//
// The interaction model:
//
//   press a pad          it plays, and it keeps playing. That is the whole thing
//   MIC + a pad          your voice goes onto that pad
//   UNDO                 takes back the last thing you did, whichever kind it was
//   UNDO held 3 s        clears everything
//   STOP                 stop / continue
//   BEAT                 next backing rhythm
//   BEAT held 1 s        next sound kit: 808, Acoustic, Lo-fi, Vocal
//
// There is no REC button, and that is deliberate. Loop pedals work by arming a
// record mode and then playing — but "REC is a state you must be in for your playing to count" is a model a small
// child will never build, and it puts the product's entire differentiator behind a
// gate. Recording is always on instead: press a pad and it joins the bar. Layers
// close themselves each time the loop comes round, so overdub and undo both still
// have sensible granularity with nothing to press for them.
//
// Two gestures on the whole board are holds — MIC to sample and UNDO to wipe — and
// both show what they are doing while you hold them. Everything else is a tap.
// Sampling takes two keys precisely because it is destructive: a pad on its own
// can be leaned on all day and will only ever make its sound.
//
// Board: TinkerPad (ESP32-S3 N16R8).
//   PSRAM=opi          — the sample and loop buffers are budgeted against 8 MB
//   CDCOnBoot=default  — on a wchusbserial* port. On usbmodem* set it to cdc,
//                        or the console stays blank while the firmware runs fine.
//
// The key, LED and on-screen-position numberings are fixed by the finished PCB
// and live as constants in layout.h. There is nothing to calibrate.

#include <Preferences.h>
#include <Wire.h>

#include "config.h"
#include "history.h"
#include "kit.h"
#include "layout.h"
#include "ledui.h"
#include "looper.h"
#include "mic.h"
#include "mixer.h"
#include "pads.h"
#include "pins.h"
#include "rhythm.h"
#include "slots.h"

static Pads pads;

struct KeyState {
    bool down = false;
    uint32_t downMs = 0;
    bool becameSampling = false;
    bool clearedAll = false;
    bool cycledRhythm = false;
};

static KeyState g_keys[Pads::COUNT];
static int8_t g_samplingSlot = -1;
static uint32_t g_lastGhostWarnMs = 0;
static uint32_t g_lastLedMs = 0;
static bool g_ready = false;

// ------------------------------------------------------------------ sound

static void playSlot(uint8_t slot) {
    const int16_t *data = Slots::data(slot);
    uint32_t frames = Slots::frames(slot);
    if (!data || frames == 0) return;
    Mixer::trigger(data, frames, kVoiceGain);
    LedUI::flash(slot);
}

// Called by the looper when a recorded event or a factory rhythm hit falls due.
// Slots go down exactly the same path as a live hit, which is why swapping the
// sound under a pad makes the existing groove play with the new sound.
static void onLoopEvent(uint8_t id) {
    if (id < kSlotCount) {
        playSlot(id);
        return;
    }
    // The metronome. No pad, so no LED flash — PLAY already shows the beat, and a
    // click that lit something would compete with it.
    bool accent = id == Looper::kIdClickAccent;
    Mixer::trigger(Kit::clickData(accent), Kit::clickFrames(accent), kVoiceGain);
}

// --------------------------------------------------------------- sampling

static void beginSampling(uint8_t slot) {
    // Everything the speaker plays during a capture ends up in the recording,
    // and the mic and speaker are centimetres apart on the same PCB. So the
    // mixer goes quiet — but the clock keeps running, so the loop holds its
    // place and you come back in on the beat.
    Mixer::stopAll();
    Mixer::setMuted(true);

    Mic::startCapture(Slots::userBuffer(slot), Slots::userCapacity());
    g_samplingSlot = (int8_t)slot;
    LedUI::setSampling((int8_t)slot);

    Serial.printf("[sample] S%u — recording, up to %.1f s\n", slot + 1,
                  kMaxSampleSeconds);
}

static void endSampling() {
    uint8_t slot = (uint8_t)g_samplingSlot;
    uint32_t raw = Mic::stopCapture();
    uint32_t clipped = Mic::clipped();
    float peak = Mic::lastPeakDbfs();

    // Let the microphone task notice the stop before touching the buffer it was
    // writing into.
    delay(5);

    uint32_t polished = Mic::polish(Slots::userBuffer(slot), raw);

    g_samplingSlot = -1;
    LedUI::setSampling(-1);
    Mixer::setMuted(false);

    if (polished == 0) {
        Serial.println("[sample] nothing above the noise floor — slot unchanged");
        return;
    }

    Slots::commitUser(slot, polished);
    History::pushSample(slot);  // one tap of UNDO puts the factory sound back

    Serial.printf("[sample] S%u  %.0f ms raw -> %.0f ms trimmed  peak %.1f dBFS\n",
                  slot + 1, 1000.0f * raw / kSampleRate,
                  1000.0f * polished / kSampleRate, peak);

    if (clipped > 0) {
        // Close-up beatboxing is loud. A clipped plosive is a
        // flat "pfft" instead of a punchy kick, and no amount of software can
        // put back what the microphone never heard.
        Serial.printf("[sample] WARNING %u clipped samples — back off from the "
                      "mic or hit it softer\n",
                      (unsigned)clipped);
    } else if (peak < -30.0f) {
        Serial.println("[sample] quiet capture — normalising brought the noise "
                       "floor up with it. Get closer.");
    }

    playSlot(slot);  // hear what you just made, immediately
}

// -------------------------------------------------------------- gestures

// True while the MIC key is down. Sampling is the one destructive thing a pad can
// do, so it takes two keys — which makes it impossible to trigger by accident and
// frees the pad hold to mean nothing at all. A child can lean on a pad for as long
// as they like and only ever hear the sound.
static bool g_micHeld = false;

// The chosen kit survives a power cut; recorded sounds deliberately do not.
static void saveKit(uint8_t k) {
    Preferences p;
    if (p.begin("beatbox", false)) {
        p.putUChar("kit", k);
        p.end();
    }
}

static uint8_t loadKit() {
    Preferences p;
    uint8_t k = 0;
    if (p.begin("beatbox", true)) {
        k = p.getUChar("kit", 0);
        p.end();
    }
    return k < Kit::kCount ? k : 0;
}

static void nextKit() {
    uint8_t k = (Kit::current() + 1) % Kit::kCount;
    if (!Kit::select(k)) {
        Serial.println("[kit] could not build that kit (out of PSRAM?)");
        return;
    }
    saveKit(k);
    LedUI::flashKit(Kit::colour(k));
    Serial.printf("[kit] %s\n", Kit::name(k));
}

static void onPress(uint8_t bit, uint32_t atSample) {
    Layout::Role role = Layout::roleForKey(bit);

    // While a capture is running the board belongs to that capture: everything
    // else would either be inaudible (the mixer is muted) or land in the
    // recording.
    if (g_samplingSlot >= 0 && role != Layout::ROLE_SLOT) return;

    switch (role) {
        case Layout::ROLE_SLOT: {
            uint8_t slot = Layout::slotForKey(bit);
            if (slot >= kSlotCount) return;

            if (g_micHeld && g_samplingSlot < 0) {
                // MIC is already deliberate, so the pad starts recording on the
                // press rather than after another hold. Hold both, make the
                // sound, let go.
                g_keys[bit].becameSampling = true;
                beginSampling(slot);
                return;
            }
            if (g_samplingSlot >= 0) return;

            playSlot(slot);
            Looper::noteHit(slot, atSample);
            break;
        }

        case Layout::ROLE_MIC:
            break;  // MIC does nothing by itself; it arms the pads

        case Layout::ROLE_STOP:
            Looper::togglePlay();
            Serial.printf("[stop] %s\n", Looper::playing() ? "running" : "stopped");
            break;

        case Layout::ROLE_BEAT:
            // A tap changes the rhythm, a one-second hold changes the kit. The tap
            // therefore acts on release, so that holding for a kit never also
            // skips a rhythm on the way.
            break;

        case Layout::ROLE_UNDO:
            break;  // acts on release (undo) or on the hold (clear everything)
    }
}

static void onHold(uint8_t bit, uint32_t heldMs, uint16_t mask) {
    KeyState &st = g_keys[bit];
    (void)mask;

    if (Layout::roleForKey(bit) == Layout::ROLE_BEAT) {
        if (!st.cycledRhythm && heldMs >= kKitHoldMs) {
            st.cycledRhythm = true;  // used here as "this hold already changed kit"
            nextKit();
        }
        return;
    }

    if (Layout::roleForKey(bit) == Layout::ROLE_UNDO) {
        LedUI::setClearHold(heldMs);
        // Only the destructive stage fires on the hold. Undo happens on release,
        // so a tap is undo and a long hold is a wipe — and holding through the
        // wipe cannot also undo something on the way.
        if (!st.clearedAll && heldMs >= kClearAllMs) {
            st.clearedAll = true;
            Looper::clearAll();
            History::clear();
            Serial.println("[undo] everything cleared");
        }
    }
}

static void onRelease(uint8_t bit) {
    KeyState &st = g_keys[bit];
    Layout::Role role = Layout::roleForKey(bit);

    // Letting go of either key ends the capture, because either one is the player
    // saying they are done. Requiring a particular one to be released first would
    // be a rule nobody was told.
    if (g_samplingSlot >= 0 &&
        (st.becameSampling || role == Layout::ROLE_MIC)) {
        endSampling();
    }

    if (role == Layout::ROLE_UNDO) {
        LedUI::setClearHold(0);
        if (!st.clearedAll) {
            if (!History::undo()) {
                Serial.println("[undo] nothing to undo");
            }
            LedUI::flashUndo();
        }
    }

    if (role == Layout::ROLE_BEAT && !st.cycledRhythm && g_samplingSlot < 0) {
        // Cycling grooves is the most rewarding thing on the board for someone
        // who cannot play yet, so it stays a plain tap.
        Looper::nextRhythm();
        LedUI::flashTap();
        Serial.printf("[beat] %s\n", Rhythm::name(Looper::rhythm()));
    }

    st.becameSampling = false;
    st.clearedAll = false;
    st.cycledRhythm = false;
}

static void servicePads() {
    uint16_t mask = pads.scan();
    uint32_t nowMs = millis();

    // A key could have gone down at any point during the scan that just found
    // it, so the best estimate of when it happened is half a scan period ago.
    // At the measured 2.49 ms that halves the average timing error to ~0.6 ms,
    // well under the ~10 ms where a listener starts to hear a pad drag.
    uint32_t halfScanSamples =
        (uint32_t)((uint64_t)(pads.lastScanMicros() / 2) * kSampleRate / 1000000ULL);
    uint32_t atSample = Mixer::samplesPlayed() - halfScanSamples;

    // The matrix has no anti-ghosting diodes, so some three-key states are
    // genuinely ambiguous — the same electrical pattern has more than one cause
    // and no firmware can tell which. Refusing to act on a new press is the only
    // honest response; the alternative is triggering a pad nobody touched.
    // Latched before the per-key loop so MIC's state does not depend on whether
    // its bit happens to come before or after the pad's in the scan order.
    g_micHeld = false;
    for (uint8_t bit = 0; bit < Pads::COUNT; bit++) {
        if ((mask >> bit) & 1) {
            if (Layout::roleForKey(bit) == Layout::ROLE_MIC) {
                g_micHeld = true;
                break;
            }
        }
    }

    bool ghost = Pads::hasGhostRisk(mask);
    if (ghost && nowMs - g_lastGhostWarnMs > 1000) {
        g_lastGhostWarnMs = nowMs;
        Serial.printf("[ghost] 0x%04X is ambiguous — ignoring new presses\n", mask);
    }

    for (uint8_t bit = 0; bit < Pads::COUNT; bit++) {
        bool down = (mask >> bit) & 1;
        KeyState &st = g_keys[bit];

        if (down && !st.down) {
            st.down = true;
            st.downMs = nowMs;
            st.becameSampling = false;
                    st.clearedAll = false;
            st.cycledRhythm = false;
            if (!ghost) {
                onPress(bit, atSample);
            }
        } else if (!down && st.down) {
            st.down = false;
            onRelease(bit);
        } else if (down) {
            onHold(bit, nowMs - st.downMs, mask);
        }
    }
}

// ------------------------------------------------------------- I2C probe

// "Nothing on the bus" has several causes that call for completely different
// responses, so this measures first and concludes once.
//
// The carrier pulls SDA and SCL to 3V3 through 10k (R17 and R18 on the
// schematic), and both pins happen to be ADC-capable on the S3 — GPIO9 is
// ADC1_CH8, GPIO8 is ADC1_CH7. So the useful measurement is not a bit but a
// voltage: where the line actually settles, passively, with no internal pull
// either way. Nothing is driven, so no measurement here can make a fault worse.
//
//   both at the rail        healthy idle bus
//   one low, one at rail    3V3 is up, and that one line is genuinely loaded.
//                           Rload = 10k * V / (3.3 - V) separates a solder
//                           bridge (tens of ohms) from a damaged input on a
//                           part (kilohms)
//   BOTH a few hundred mV   the rail those 10k resistors pull up TO is itself
//                           down. A short would read near 0 mV, not 400 mV
//   both near 0 mV          no pull-ups reaching the pins at all
//
// An earlier version of this did digital reads only — internal pull-down, then
// internal pull-up — and concluded "held low means a short to ground". That was
// wrong, and wrong in a way that cost real time: through a digital read, a bus
// whose supply is down is indistinguishable from a shorted one, and it got
// reported as a hardware fault when the actual answer was that the IP5306 was
// asleep. The digital reads are still taken, but as supporting data. They no
// longer get to draw the conclusion.
static void probeI2c() {
    Serial.println("[i2c] SDA=GPIO9  SCL=GPIO8");

    Wire.end();

    pinMode(PIN_I2C_SDA, INPUT);
    pinMode(PIN_I2C_SCL, INPUT);
    delay(5);
    uint32_t sdaMv = analogReadMilliVolts(PIN_I2C_SDA);
    uint32_t sclMv = analogReadMilliVolts(PIN_I2C_SCL);

    // Supporting data: does an external pull-up beat the internal pull-down, and
    // is the line free when pulled up from inside?
    pinMode(PIN_I2C_SDA, INPUT_PULLDOWN);
    pinMode(PIN_I2C_SCL, INPUT_PULLDOWN);
    delay(10);
    bool sdaExt = digitalRead(PIN_I2C_SDA);
    bool sclExt = digitalRead(PIN_I2C_SCL);

    pinMode(PIN_I2C_SDA, INPUT_PULLUP);
    pinMode(PIN_I2C_SCL, INPUT_PULLUP);
    delay(10);
    bool sdaFree = digitalRead(PIN_I2C_SDA);
    bool sclFree = digitalRead(PIN_I2C_SCL);

    // The ADC saturates around 2.5 V, so a line sitting at 3V3 reads "at the
    // rail" rather than exactly 3300 mV. Only the low readings need to be
    // accurate, and those are well inside range.
    Serial.printf("  SDA  %4u mV   beats internal pulldown: %-3s   free: %s\n",
                  (unsigned)sdaMv, sdaExt ? "yes" : "no", sdaFree ? "yes" : "no");
    Serial.printf("  SCL  %4u mV   beats internal pulldown: %-3s   free: %s\n",
                  (unsigned)sclMv, sclExt ? "yes" : "no", sclFree ? "yes" : "no");

    // The one conclusion. Reading these correctly comes down to remembering what
    // the pull-ups are pulling up TO — see the table above the function.
    const bool sdaRail = sdaMv > 2400;
    const bool sclRail = sclMv > 2400;

    if (sdaRail && sclRail) {
        Serial.println("  both at the rail — the bus is idle and healthy");
    } else if (!sdaRail && !sclRail && sdaMv > 100 && sclMv > 100) {
        Serial.println("  BOTH lines sit well below the rail but well above ground.");
        Serial.println("  That is not a short — a short reads near 0 mV. It is the 3V3");
        Serial.println("  rail these 10k pull-ups go to being down, which also leaves the");
        Serial.println("  PCF8574 unpowered and its input clamps holding the lines here.");
        Serial.println();
        Serial.println("  On this carrier that usually means the IP5306 is asleep, and it");
        Serial.println("  has no power at all with no cell fitted and no charger plugged");
        Serial.println("  in — which is why 0x75 is missing from the scan too.");
        Serial.println("  Fit a cell, plug the charger in, or short-press the power key,");
        Serial.println("  then run `i` again. The keypad needs no reflash: it re-probes");
        Serial.println("  twice a second and will simply start working.");
    } else if (!sdaRail || !sclRail) {
        const char *bad = !sdaRail ? "SDA (GPIO9)" : "SCL (GPIO8)";
        uint32_t badMv = !sdaRail ? sdaMv : sclMv;
        float rf = 10000.0f * (float)badMv / max(3300.0f - (float)badMv, 1.0f);
        Serial.printf("  %s is held down while the other line sits at the rail,\n", bad);
        Serial.printf("  so 3V3 is up and that one line is genuinely loaded: about\n");
        Serial.printf("  %.0f ohms to ground. Tens of ohms is a bridge or a shorted\n", rf);
        Serial.printf("  track; a few kilohms is more like a damaged input on a part.\n");
        Serial.println("  To find which side: unseat the dev board and run `i` again.");
        Serial.println("  Still held low -> the dev board's own GPIO. Released -> the");
        Serial.println("  carrier, its wiring, or a part on the bus.");
    } else {
        Serial.println("  Both lines are at ground with nothing pulling them up, so the");
        Serial.println("  10k pull-ups are not reaching the pins at all: the dev board is");
        Serial.println("  not seated in the carrier.");
    }

    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
    Wire.setClock(100000);
    Wire.setTimeOut(5);

    Serial.println("[i2c] scan");
    uint8_t found = 0;
    for (uint8_t addr = 1; addr < 127; addr++) {
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() == 0) {
            Serial.printf("  0x%02X", addr);
            if (addr == ADDR_PCF8574) Serial.print("  PCF8574 keypad");
            if (addr == ADDR_IP5306) Serial.print("  IP5306 power");
            Serial.println();
            found++;
        }
    }
    if (found == 0) {
        Serial.println("  nothing answered");
    }
    Serial.println("  the IP5306 does not ACK while asleep — a missing 0x75 with");
    Serial.println("  no cell and no charger fitted is expected, not a fault.");

    // Re-probe so the keypad is picked up without a reboot.
    pads.begin(ADDR_PCF8574, PIN_I2C_SDA, PIN_I2C_SCL);
}

// ------------------------------------------------------- sampling self-test

// Records the speaker into S12 and plays the result back.
//
// The microphone and the speaker are centimetres apart on the same PCB, so a
// working capture chain cannot miss this — it proves capture, DC removal,
// trimming, normalisation, commit and playback end to end with nobody in the
// room. Unlike a real sampling gesture the mixer is deliberately left unmuted,
// because here the speaker IS the signal source.
static void selfTestSampling() {
    const uint8_t slot = kSlotCount - 1;  // S12
    Serial.println("[selftest] recording the speaker into S12");

    Mic::startCapture(Slots::userBuffer(slot), Slots::userCapacity());
    for (uint8_t i = 0; i < 4; i++) {
        playSlot(8);  // cowbell: narrow-band and loud, easy to recognise
        delay(300);
    }
    uint32_t raw = Mic::stopCapture();
    uint32_t clipped = Mic::clipped();
    float peak = Mic::lastPeakDbfs();
    delay(5);

    uint32_t polished = Mic::polish(Slots::userBuffer(slot), raw);

    Serial.printf("  %.0f ms raw -> %.0f ms trimmed, peak %.1f dBFS, %u clipped\n",
                  1000.0f * raw / kSampleRate, 1000.0f * polished / kSampleRate,
                  peak, (unsigned)clipped);

    if (polished == 0) {
        Serial.println("  FAIL — nothing above the noise floor. Either the mic is");
        Serial.println("  not being captured or the speaker is not making sound.");
        return;
    }

    Slots::commitUser(slot, polished);
    // Same as the real gesture: committing a sample is an undoable action. The
    // first version of this skipped it, and the result was that `u` after `v`
    // silently removed a layer instead — a diagnostic that does not take the same
    // path as the feature is a diagnostic that can lie to you about the feature.
    History::pushSample(slot);
    Serial.println("  PASS — S12 now holds that recording; playing it back");
    delay(200);
    playSlot(slot);
}

// ---------------------------------------------------------------- console

static void printStatus() {
    Serial.println();
    Serial.println("--- status");
    Serial.printf("  PSRAM   %u KB free of %u KB\n",
                  (unsigned)(ESP.getFreePsram() / 1024),
                  (unsigned)(ESP.getPsramSize() / 1024));
    Serial.printf("  audio   %u Hz, mixer load %.1f%%, voices %u now / %u peak, "
                  "underruns %u, dropped %u\n",
                  (unsigned)kSampleRate, Mixer::load() * 100.0f,
                  Mixer::activeVoices(), Mixer::peakVoices(),
                  (unsigned)Mixer::underruns(), (unsigned)Mixer::dropped());
    Serial.printf("  output  makeup %.2fx (%+.1f dB), high-pass %.0f Hz\n",
                  Mixer::makeup(), 20.0f * log10f(Mixer::makeup()),
                  Mixer::highPassHz());
    Serial.printf("  matrix  scan %u us%s\n", (unsigned)pads.lastScanMicros(),
                  pads.present() ? "" : "  (expander not answering)");
    Serial.printf("  mic     level %.3f\n", Mic::level());
    Looper::print();
    History::print();
    Serial.println("  slots");
    Slots::print();
    Serial.println("  layout");
    Layout::print();
}

static void printHelp() {
    Serial.println();
    Serial.println("TinkerPad — Beatbox");
    Serial.println("  Press a pad. It plays, and it keeps playing. That is the whole thing.");
    Serial.println();
    Serial.println("  S1-S12   press to play. Everything you play joins the loop and");
    Serial.println("           repeats — there is nothing to arm and no mode to enter");
    Serial.println("  MIC      hold MIC, then hold a pad, and make a sound: your voice");
    Serial.println("           goes onto that pad. Let go of either key to finish");
    Serial.println("  STOP     stop / continue. Pulses every beat, brightest on beat 1");
    Serial.println("  BEAT     next backing rhythm:");
    Serial.println("           off -> click -> backbeat -> four-floor -> boom-bap -> clave");
    Serial.println("           hold BEAT 1 s for the next sound kit:");
    Serial.println("           808 -> Acoustic -> Lo-fi -> Vocal");
    Serial.println("  UNDO     tap to take back the last thing you did — a layer you");
    Serial.println("           played, or a sound you recorded. Hold 3 s to clear all;");
    Serial.println("           the pad fills and turns red first, so let go if unsure");
    Serial.println();
    Serial.println("  Console:");
    Serial.println("    s  status — mixer load, voices, PSRAM, looper, slots");
    Serial.println("    i  I2C pull-up check and bus scan");
    Serial.println("    v  sampling self-test — records the speaker into S12");
    Serial.println("    r  revert every slot to its factory sound");
    Serial.println("    a  recording on / off — audition without committing.");
    Serial.println("       Deliberately not on the board: one more hidden gesture");
    Serial.println("       costs more than this is worth to whoever wants it");
    Serial.println("    t  start / stop the loop (what STOP does)");
    Serial.println("    n  next rhythm (what a tap on BEAT does)");
    Serial.println("    k  next sound kit (what holding BEAT does)");
    Serial.println("    u  undo the last thing (what UNDO does)");
    Serial.println("    x  clear everything");
    Serial.println("    + / -  tempo by 2 BPM");
    Serial.println("    > / <  louder / quieter (makeup gain before the limiter)");
    Serial.println("    h  cycle the master high-pass: off 80 120 180 250 Hz");
    Serial.println("    1..9 0 [ ]  trigger slots 1..12 with no hands on the board");
    Serial.println("    ?  this");
    Serial.print("> ");
}

static void handleConsole(int c) {
    // Slot triggers, so the audio engine can be exercised with no hands on the
    // board — useful for measuring mixer load under a known number of voices.
    // A console trigger takes exactly the same path as a pad hit, Looper included,
    // so the whole instrument can be exercised — sample, record, overdub, undo —
    // with no working keypad. That mattered on this prototype: SCL failed, and
    // without this the looper could not have been tested at all.
    const char *slotKeys = "123456789";
    const char *found = c ? strchr(slotKeys, c) : nullptr;
    if (found) {
        uint8_t slot = (uint8_t)(found - slotKeys);
        Serial.printf("S%u %s\n", slot + 1, Slots::name(slot));
        playSlot(slot);
        Looper::noteHit(slot, Mixer::samplesPlayed());
        return;
    }

    switch (c) {
        case '0':
        case '[':
        case ']': {
            uint8_t slot = c == '0' ? 9 : (c == '[' ? 10 : 11);
            Serial.printf("S%u %s\n", slot + 1, Slots::name(slot));
            playSlot(slot);
            Looper::noteHit(slot, Mixer::samplesPlayed());
            break;
        }

        case 'k':
            nextKit();
            break;
        case 'n':
            Looper::nextRhythm();
            Serial.printf("rhythm: %s\n", Rhythm::name(Looper::rhythm()));
            break;

        // Loudness, by ear. The makeup gain sits before the soft limiter, so
        // raising it lifts the average level rather than just the peaks.
        case '>':
            Mixer::setMakeup(Mixer::makeup() * 1.26f);  // +2 dB
            Serial.printf("makeup %.2fx (%+.1f dB)\n", Mixer::makeup(),
                          20.0f * log10f(Mixer::makeup()));
            break;
        case '<':
            Mixer::setMakeup(Mixer::makeup() / 1.26f);
            Serial.printf("makeup %.2fx (%+.1f dB)\n", Mixer::makeup(),
                          20.0f * log10f(Mixer::makeup()));
            break;
        case 'h': {
            // Cycles the master high-pass. On a speaker this small, removing bass
            // it cannot reproduce buys more apparent loudness than gain does —
            // but how much is a question about this enclosure, so it is a knob.
            const float corners[] = {0.0f, 80.0f, 120.0f, 180.0f, 250.0f};
            float now = Mixer::highPassHz();
            uint8_t next = 0;
            for (uint8_t i = 0; i < 5; i++) {
                if (fabsf(corners[i] - now) < 1.0f) {
                    next = (uint8_t)((i + 1) % 5);
                    break;
                }
            }
            Mixer::setHighPassHz(corners[next]);
            if (corners[next] == 0.0f) {
                Serial.println("high-pass off — full bass, less headroom");
            } else {
                Serial.printf("high-pass %.0f Hz\n", corners[next]);
            }
            break;
        }

        case 'a':
            // The audition escape hatch. Not on the board on purpose.
            Looper::setRecording(!Looper::recording());
            Serial.printf("recording %s\n",
                          Looper::recording() ? "on" : "OFF — audition only");
            break;
        case 'u':
            if (!History::undo()) Serial.println("nothing to undo");
            break;

        case 's': printStatus(); break;
        case 'v': selfTestSampling(); break;
        case 'i': probeI2c(); break;
        case 'r':
            Slots::revertAll();
            Serial.println("all slots back to factory sounds");
            break;
        case 't':
            Looper::togglePlay();
            Serial.printf("loop %s\n", Looper::playing() ? "running" : "stopped");
            break;
        case 'x':
            Looper::clearAll();
            Serial.println("loop cleared");
            break;
        case '+':
        case '=':
            Looper::setTempo(Looper::tempo() + 2.0f);
            Serial.printf("%.1f BPM\n", Looper::tempo());
            break;
        case '-':
            Looper::setTempo(Looper::tempo() - 2.0f);
            Serial.printf("%.1f BPM\n", Looper::tempo());
            break;
        default:
            printHelp();
            return;
    }
    Serial.print("> ");
}

// ------------------------------------------------------------------- setup

void setup() {
    Serial.begin(115200);
    delay(600);

    Serial.println();
    Serial.println("=== TinkerPad — Beatbox — ESP32-S3 Blueprints No.02 ===");
    Serial.printf("PSRAM %u KB   Flash %u MB\n",
                  (unsigned)(ESP.getPsramSize() / 1024),
                  (unsigned)(ESP.getFlashChipSize() / (1024 * 1024)));

    if (ESP.getPsramSize() == 0) {
        Serial.println();
        Serial.println("FATAL: no PSRAM. Enable OPI PSRAM in the board options —");
        Serial.println("4.4 MB of sample buffers have nowhere else to live.");
        return;
    }

    // LEDs first, and the sweep before the kit is synthesised, so the board comes
    // alive at once instead of sitting dark while the sounds are built.
    Serial.println("LEDs");
    LedUI::begin();
    LedUI::bootSweep();

    Serial.println("slots");
    if (!Slots::begin(kSampleRate, kMaxSampleSeconds)) {
        Serial.println("FATAL: could not reserve the sample buffers.");
        return;
    }
    Serial.printf("  %u x %.1f s user buffers reserved (%u KB)\n", kSlotCount,
                  kMaxSampleSeconds, (unsigned)(Slots::bytesUsed() / 1024));

    Serial.println("sound kit");
    if (!Kit::begin(kSampleRate, loadKit())) {
        Serial.println("FATAL: could not synthesise the sound kit.");
        return;
    }
    Serial.printf("  %s kit, %u KB\n", Kit::name(Kit::current()),
                  (unsigned)(Kit::bytesUsed() / 1024));

    Serial.println("audio");
    if (!Mixer::begin(kSampleRate)) {
        Serial.println("FATAL: the mixer did not start.");
        return;
    }
    if (!Mic::begin(kSampleRate)) {
        Serial.println("FATAL: the microphone did not start.");
        return;
    }

    Serial.println("looper");
    Looper::begin(kSampleRate);
    Looper::setTriggerCallback(onLoopEvent);
    History::begin();
    // Layers open by themselves now that REC is gone, so the undo stack has to
    // hear about them from the looper rather than from a gesture.
    Looper::setLayerOpenedCallback(History::pushLayer);
    Serial.printf("  %u beats per loop, %.0f BPM to start\n",
                  Looper::beatsPerLoop(), kDefaultBpm);

    Serial.println("keypad");
    if (pads.begin(ADDR_PCF8574, PIN_I2C_SDA, PIN_I2C_SCL)) {
        Serial.println("  PCF8574 at 0x20");
    } else {
        Serial.println("  PCF8574 not answering — the pads will do nothing until");
        Serial.println("  the dev board is seated in the carrier. It re-probes");
        Serial.println("  twice a second, so no reflash is needed.");
    }

    g_ready = true;

    printHelp();
}

void loop() {
    if (!g_ready) {
        delay(500);
        return;
    }

    servicePads();
    Looper::tick();

    uint32_t now = millis();
    if (now - g_lastLedMs >= kLedFrameMs) {
        g_lastLedMs = now;
        LedUI::render();
    }

    while (Serial.available()) {
        int c = Serial.read();
        if (c == '\r' || c == '\n' || c == ' ') continue;
        Serial.println((char)c);
        handleConsole(c);
    }
}
