#pragma once

#include <Arduino.h>

// The loop is a list of "you hit pad N at this point in the bar", not a mixed
// recording. It costs kilobytes instead of megabytes,
// a layer can be undone cleanly, the sound under a pad can be swapped and the
// existing groove plays with the new sound, and repeated overdubs suffer no
// generation loss.
//
// Event positions are stored as a fraction of the loop in 32-bit fixed point,
// NOT as a sample offset. That is what makes "change the tempo after recording"
// true rather than a claim — with sample offsets, every event would have to be
// rewritten on a tempo change, and rounding would smear the groove a little more
// each time you nudged it.
//
// Time comes from Mixer::samplesPlayed(). See mixer.h for why not millis().

namespace Looper {

// Ids the trigger callback can receive. 0..kSlotCount-1 are sound slots; these
// two are the metronome, which has no pad of its own.
static const uint8_t kIdClick = 12;
static const uint8_t kIdClickAccent = 13;

void begin(uint32_t sampleRate);

void setTriggerCallback(void (*fn)(uint8_t id));

// --- factory rhythm
//
// A rhythm is a second event list that plays through exactly the same tick, the
// same phase and the same trigger path as the recorded layers, so "the rhythm and
// your hits play together" needs no code of its own — it falls out of the event
// model. It is separate from the user layers in only two respects: CLR never
// touches it, and it does not count towards the layer limit.
void setRhythm(uint8_t index);
uint8_t rhythm();
void nextRhythm();

// --- transport
void togglePlay();
void stop();
bool playing();

// Recording is on by default and there is no arming gesture. This exists only as
// an escape hatch for someone who wants to audition sounds without committing
// them, and it lives on the console rather than on the board — the board's job is
// to be obvious, and one more hidden gesture costs more than this feature is
// worth to the person who needs it.
void setRecording(bool on);
bool recording();

// A pad was hit at this point on the master clock.
//
// Starts the loop if it was stopped, so the very first press on a silent device
// produces a sound that then repeats, with nothing else to press first. That one
// behaviour is the entire product demonstrated in one gesture.
void noteHit(uint8_t slot, uint32_t atSample);

// Called whenever a new layer opens. Layers close by themselves at each loop
// wrap, so this is how anything outside knows a new one began — nothing on the
// board defines a layer boundary, which is what removing REC bought.
void setLayerOpenedCallback(void (*fn)());

void clearLastLayer();
void clearAll();

// --- tempo
//
// Tap tempo is deliberately not on the board any more: the factory rhythms carry
// their own tempo, a child has no use for it, and it was occupying one of the
// hidden hold gestures. It is still here for the console.
void tap(uint32_t atSample);
void setTempo(float bpm);
float tempo();

// --- state for the LED layer
uint8_t layerCount();
uint16_t eventCount();
uint8_t beatsPerLoop();
uint8_t currentBeat();     // 0-based within the loop
float beatPhase();         // 0..1 through the current beat
bool wrappedThisTick();

// Fires any events that fell due since the last call. Called from the main loop,
// so its resolution is the matrix scan period — the same resolution the events
// were recorded at, which keeps playback symmetric with performance.
void tick();

void print();

}  // namespace Looper
