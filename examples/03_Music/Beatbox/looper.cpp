#include "looper.h"

#include "config.h"
#include "mixer.h"
#include "rhythm.h"

namespace Looper {

struct Event {
    uint32_t phase;  // Q32 fraction of the loop
    uint8_t slot;
    uint8_t layer;
};

static Event g_events[kMaxEvents];
static uint16_t g_eventCount = 0;
static uint8_t g_layerCount = 0;

// The factory rhythm, expanded from one bar of steps into loop-relative phases
// once when it is selected rather than every tick.
static const uint16_t kMaxRhythmEvents = 256;
static Event g_rhythmEvents[kMaxRhythmEvents];
static uint16_t g_rhythmEventCount = 0;
static uint8_t g_rhythmIndex = 0;

static uint32_t g_sampleRate = kSampleRate;
static float g_bpm = kDefaultBpm;
static uint32_t g_loopFrames = 0;

static bool g_playing = false;
static uint32_t g_startSample = 0;
static uint32_t g_lastPhase = 0;
static bool g_wrapped = false;

static bool g_recording = true;

// The layer a new event goes into.
//
// With no REC key there is nothing to mark a layer boundary by hand, so layers
// close themselves every time the loop comes round: whatever you played during
// one pass is one layer, and the next hit after the wrap starts a new one. That
// is both automatic and the granularity people actually want from undo — you
// build a bar, hear it come back, add to it.
static bool g_layerOpen = false;
static uint8_t g_activeLayer = 0;

static void (*g_trigger)(uint8_t) = nullptr;
static void (*g_layerOpened)() = nullptr;

// Tap tempo history. Four taps is the sweet spot: two is enough to get a tempo
// but noticeably jittery, and asking for more than four feels like a test.
static const uint8_t kTapHistory = 4;
static uint32_t g_taps[kTapHistory];
static uint8_t g_tapCount = 0;

static void recomputeLoopFrames() {
    float beats = (float)beatsPerLoop();
    g_loopFrames = (uint32_t)((60.0f / g_bpm) * beats * (float)g_sampleRate);
}

static inline uint32_t phaseAt(uint32_t sample) {
    uint32_t elapsed = sample - g_startSample;  // unsigned: wrap-safe
    uint32_t pos = elapsed % g_loopFrames;
    return (uint32_t)(((uint64_t)pos << 32) / g_loopFrames);
}

void begin(uint32_t sampleRate) {
    g_sampleRate = sampleRate;
    recomputeLoopFrames();
}

void setTriggerCallback(void (*fn)(uint8_t)) { g_trigger = fn; }

uint8_t beatsPerLoop() { return kBeatsPerBar * kLoopBars; }

static void start(uint32_t atSample) {
    g_startSample = atSample;
    g_lastPhase = 0;
    g_playing = true;
}

void togglePlay() {
    if (g_playing) {
        stop();
    } else {
        start(Mixer::samplesPlayed());
    }
}

void stop() {
    g_playing = false;
    g_layerOpen = false;
}

bool playing() { return g_playing; }

void setRecording(bool on) {
    g_recording = on;
    if (!on) {
        g_layerOpen = false;
    }
}

bool recording() { return g_recording; }

void setLayerOpenedCallback(void (*fn)()) { g_layerOpened = fn; }

void noteHit(uint8_t slot, uint32_t atSample) {
    if (!g_recording) return;

    // The first press on a silent device starts the loop, with that hit as beat
    // one. Nothing has to be armed and nothing has to be started: press a pad,
    // hear it, hear it come back. That is the product, in one gesture, with no
    // instructions.
    if (!g_playing) {
        start(atSample);
    }

    if (!g_layerOpen) {
        if (g_layerCount >= kMaxLayers) {
            return;  // layer ceiling: refuse rather than silently overwrite
        }
        g_activeLayer = g_layerCount++;
        g_layerOpen = true;
        if (g_layerOpened) {
            g_layerOpened();
        }
    }

    if (g_eventCount >= kMaxEvents) return;

    g_events[g_eventCount].phase = phaseAt(atSample);
    g_events[g_eventCount].slot = slot;
    g_events[g_eventCount].layer = g_activeLayer;
    g_eventCount++;
}

// Expands one bar of sixteenth-note steps across every bar of the loop, in Q32
// loop-relative phase. Doing it here rather than at tick time means a rhythm
// costs nothing per block, and it inherits tempo independence for free: phases
// are fractions of the loop, so changing the tempo re-times the rhythm and the
// recorded layers identically.
static void buildRhythm() {
    g_rhythmEventCount = 0;

    uint16_t hitCount = 0;
    const Rhythm::Hit *hits = Rhythm::hits(g_rhythmIndex, &hitCount);
    if (!hits || hitCount == 0) return;

    const uint32_t stepsPerLoop = (uint32_t)Rhythm::kStepsPerBar * kLoopBars;

    for (uint8_t bar = 0; bar < kLoopBars; bar++) {
        for (uint16_t h = 0; h < hitCount; h++) {
            if (g_rhythmEventCount >= kMaxRhythmEvents) return;
            uint32_t step = (uint32_t)bar * Rhythm::kStepsPerBar + hits[h].step;

            Event &e = g_rhythmEvents[g_rhythmEventCount++];
            e.phase = (uint32_t)(((uint64_t)step << 32) / stepsPerLoop);
            e.slot = hits[h].id;
            e.layer = 0xFF;  // not a user layer
        }
    }
}

void setRhythm(uint8_t index) {
    g_rhythmIndex = index < Rhythm::count() ? index : 0;
    buildRhythm();

    // Choosing a groove is a request to hear it. Starting the transport here is
    // what makes the feature work for someone who does not play: pick a rhythm,
    // it runs, you join in — rather than pick a rhythm, hear nothing, and have to
    // know that PLAY is the next step.
    if (g_rhythmIndex > 0 && !g_playing) {
        start(Mixer::samplesPlayed());
    }
}

uint8_t rhythm() { return g_rhythmIndex; }

void nextRhythm() { setRhythm((uint8_t)((g_rhythmIndex + 1) % Rhythm::count())); }

void clearLastLayer() {
    if (g_layerCount == 0) return;
    uint8_t target = g_layerCount - 1;

    uint16_t kept = 0;
    for (uint16_t i = 0; i < g_eventCount; i++) {
        if (g_events[i].layer != target) {
            g_events[kept++] = g_events[i];
        }
    }
    g_eventCount = kept;
    g_layerCount--;
    g_layerOpen = false;

    if (g_layerCount == 0 && g_rhythmIndex == 0) {
        // Nothing left to play. Keeping the transport running with an empty loop
        // would leave PLAY pulsing over silence, which reads as a fault. A rhythm
        // still running is not silence, so that case keeps going.
        stop();
    }
}

void clearAll() {
    g_eventCount = 0;
    g_layerCount = 0;
    // Held-long CLR is the panic button, and the mental model of "clear
    // everything" is silence. Taking the rhythm down with it costs one hold of
    // TAP to get back, and is much less surprising than a groove that survives
    // the gesture that was supposed to stop everything.
    g_rhythmIndex = 0;
    g_rhythmEventCount = 0;
    stop();
}

void setTempo(float bpm) {
    bpm = constrain(bpm, kMinBpm, kMaxBpm);
    if (!g_playing) {
        g_bpm = bpm;
        recomputeLoopFrames();
        return;
    }

    // Preserve where we are in the bar across the change, so a tempo nudge
    // mid-performance does not jump the groove. Events are stored as fractions,
    // so they need no rewriting at all — this is the whole point of Q32 phases.
    uint32_t phase = g_lastPhase;
    g_bpm = bpm;
    recomputeLoopFrames();

    // Re-anchor the loop origin so the current phase survives the change.
    uint32_t pos = (uint32_t)(((uint64_t)phase * g_loopFrames) >> 32);
    g_startSample = Mixer::samplesPlayed() - pos;
}

float tempo() { return g_bpm; }

void tap(uint32_t atSample) {
    // A gap longer than the slowest plausible bar means this is a new count-in,
    // not the continuation of the last one.
    const uint32_t resetGap = (uint32_t)(2.5f * g_sampleRate);
    if (g_tapCount > 0 && (atSample - g_taps[g_tapCount - 1]) > resetGap) {
        g_tapCount = 0;
    }

    if (g_tapCount == kTapHistory) {
        for (uint8_t i = 1; i < kTapHistory; i++) {
            g_taps[i - 1] = g_taps[i];
        }
        g_tapCount--;
    }
    g_taps[g_tapCount++] = atSample;

    if (g_tapCount < 2) return;

    uint32_t span = g_taps[g_tapCount - 1] - g_taps[0];
    float beatFrames = (float)span / (float)(g_tapCount - 1);
    if (beatFrames < 1.0f) return;

    setTempo(60.0f * (float)g_sampleRate / beatFrames);
}

uint8_t layerCount() { return g_layerCount; }
uint16_t eventCount() { return g_eventCount; }

uint8_t currentBeat() {
    uint32_t beats = beatsPerLoop();
    return (uint8_t)(((uint64_t)g_lastPhase * beats) >> 32);
}

float beatPhase() {
    uint32_t beats = beatsPerLoop();
    uint64_t scaled = (uint64_t)g_lastPhase * beats;  // Q32 with an integer part
    uint32_t frac = (uint32_t)(scaled & 0xFFFFFFFFull);
    return (float)frac / 4294967296.0f;
}

bool wrappedThisTick() { return g_wrapped; }

// Fires every event whose phase falls in (from, to]. When the loop wrapped this
// tick the window is two pieces — (from, end) and [start, to] — which is why this
// is a helper rather than a plain range check repeated twice.
static void fireDue(const Event *events, uint16_t count, uint32_t from,
                    uint32_t to, bool wrapped) {
    if (!g_trigger) return;
    for (uint16_t i = 0; i < count; i++) {
        uint32_t p = events[i].phase;
        bool due = wrapped ? (p > from || p <= to) : (p > from && p <= to);
        if (due) {
            g_trigger(events[i].slot);
        }
    }
}

void tick() {
    g_wrapped = false;
    if (!g_playing || g_loopFrames == 0) return;

    uint32_t phase = phaseAt(Mixer::samplesPlayed());

    if (phase == g_lastPhase) return;

    bool wrapped = phase < g_lastPhase;
    g_wrapped = wrapped;

    // Close the current layer at the wrap. The next hit opens a new one, so a
    // "layer" is simply one time round the bar and nobody had to say so.
    if (wrapped) {
        g_layerOpen = false;
    }

    // The recorded layers and the factory rhythm are fired by the same window,
    // in the same tick, against the same phase. That is the whole of "the rhythm
    // and your hits play together" — there is no synchronisation code because
    // there are no two clocks to synchronise.
    fireDue(g_rhythmEvents, g_rhythmEventCount, g_lastPhase, phase, wrapped);
    fireDue(g_events, g_eventCount, g_lastPhase, phase, wrapped);

    g_lastPhase = phase;
}

void print() {
    Serial.printf("  %s  %.1f BPM  %u beats/loop (%.2f s)\n",
                  g_playing ? "playing" : "stopped", g_bpm, beatsPerLoop(),
                  (float)g_loopFrames / (float)g_sampleRate);
    Serial.printf("  %u layers, %u events, recording %s\n", g_layerCount,
                  g_eventCount, g_recording ? "on" : "OFF (audition only)");
    Serial.printf("  rhythm: %s (%u events)\n", Rhythm::name(g_rhythmIndex),
                  g_rhythmEventCount);
}

}  // namespace Looper
