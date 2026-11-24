#pragma once

#include <Arduino.h>

// TinkerPad — Beatbox.
//
// Every number in here that could reasonably be argued about has the argument
// written next to it, because the ones that matter were all measured on the
// prototype rather than guessed.

// ----------------------------------------------------------------- audio

// 44100 because cymbals and hi-hats need the bandwidth, and the PSRAM budget
// survives it comfortably — see Slots.
static const uint32_t kSampleRate = 44100;

// The mixer renders in blocks of this size. Smaller means the pad-hit-to-sound
// latency drops, but the per-block overhead climbs. 128 frames is 2.9 ms,
// which lines up neatly with the 2.49 ms matrix scan period.
static const size_t kBlockFrames = 128;

// I2S DMA depth: 4 x 128 = 512 frames = 11.6 ms of buffered output. That is the
// floor on trigger latency and it is deliberately shallow. The default IDF
// config is 6 x 240 = 32 ms, which is audibly late on a percussion instrument.
static const uint8_t kTxDmaDescs = 4;
static const uint16_t kTxDmaFrames = 128;

// The microphone can afford deeper buffers: nothing is waiting on it.
static const uint8_t kRxDmaDescs = 4;
static const uint16_t kRxDmaFrames = 256;

// Eight voices cost 3.4% of one core, measured; `s` on the console reports the
// real mixing cost so this can be raised with evidence.
static const uint8_t kMaxVoices = 8;

// Gain staging. The first version of this stacked three separate safety margins
// — samples normalised to 0.85, then 0.8 per voice, then 0.9 master — and the
// result was a single pad hit peaking at -4.3 dBFS. Nearly 6 dB thrown away
// before the limiter was even reached, on a device whose amplifier has no gain
// control of its own (the NS4168's CTRL is tied high; all gain is in software).
//
// So the static gains are now unity and the soft limiter does the job it is for.
static const float kVoiceGain = 1.00f;
static const float kMasterGain = 1.00f;

// Gain applied BEFORE the limiter. This is what actually makes it loud: raising
// it lifts the average level while the limiter holds the peaks, which is how
// every drum bus in existence is made to sound big. Too much and transients
// squash, so it is adjustable from the console (`<` and `>`) — the right value is
// found by ear on the real speaker, not calculated.
static const float kMakeupGain = 1.80f;

// Master high-pass corner, 12 dB/octave, 0 disables.
//
// The speaker on this board is tiny. It cannot reproduce a 46 Hz kick
// fundamental or the 55 Hz bass slot at all, but that energy still consumes the
// full output swing on its way to producing nothing. Removing it frees real
// headroom for everything that IS audible, which is worth more loudness than any
// amount of gain. `h` cycles this at runtime.
static const float kHighPassHz = 120.0f;

// ----------------------------------------------------------------- slots

static const uint8_t kSlotCount = 12;

// 4 s per user slot, pre-allocated at boot. 12 x 4 s x 86 KB/s = 4.1 MB of the
// 8 MB PSRAM. Pre-allocating means no allocation ever happens
// on the audio path and PSRAM cannot fragment mid-performance.
static const float kMaxSampleSeconds = 4.0f;

// ---------------------------------------------------------------- looper

static const uint8_t kBeatsPerBar = 4;

// One 4-beat bar at 100 BPM is 2.4 s, which is not enough room to build a
// groove across 12 sounds — every loop pedal on the market loops 2 bars for the
// same reason. Set to 1 for a single bar.
static const uint8_t kLoopBars = 2;

static const float kDefaultBpm = 100.0f;
static const float kMinBpm = 50.0f;
static const float kMaxBpm = 200.0f;

static const uint16_t kMaxEvents = 512;
static const uint8_t kMaxLayers = 8;

// ------------------------------------------------------------- gestures

// Sampling needs no hold time: it is MIC plus a pad, and holding MIC is already
// the deliberate part, so the pad starts recording the instant it goes down.
// An earlier version used a 350 ms hold on a lone pad and it was a trap — lean on
// a pad a moment too long and its sound was silently replaced by your breathing,
// with no way to get it back. Making the destructive gesture take two keys removed
// both the accident and the need for a timeout.

// UNDO is two-stage, and the split is by frequency of use rather than by symmetry.
//
// Removing the last layer is the common action — it is undo, and you reach for it
// constantly while building a groove — so it is a plain tap, on release. Clearing
// everything is rare and destructive, so it wants a hold long enough that it
// cannot be arrived at by accident. 3 s feels long while you are holding it, which
// is exactly the point: the LED ramps and changes colour on the way, so the second
// stage is announced rather than discovered.
static const uint16_t kClearAllMs = 3000;

// Holding BEAT this long switches to the next sound kit. A tap (shorter than this)
// still changes the rhythm, on release.
static const uint16_t kKitHoldMs = 1000;

// ------------------------------------------------------------------ LEDs

// 16 WS2812B at full white is ~950 mA, which a laptop USB port will not give
// you and which browns out the serial bridge before it browns out the LEDs.
// 48/255 is the ceiling that measured safe on the prototype with no cell.
static const uint8_t kLedBrightness = 48;

static const uint16_t kLedFrameMs = 16;  // ~60 Hz
