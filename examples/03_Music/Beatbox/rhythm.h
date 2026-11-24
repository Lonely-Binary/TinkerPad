#pragma once

#include <Arduino.h>

// Factory rhythms — the thing that makes this an instrument rather than a board
// of sounds, for somebody who does not already play one.
//
// Shipping good sounds is not optional, because most people
// cannot beatbox and a device that demands it first makes them feel like the
// problem. Factory *patterns* are that same argument one step further. A groove
// is already running when you pick it up, so playing along is the entry point and
// keeping time is the machine's job, not yours. Your hits land in the same bar and
// repeat next to it.
//
// Setting 0 is off, setting 1 is a bare metronome, and the rest are grooves. The
// metronome lives in the same list on purpose: it is the same feature — something
// to play against — at its smallest, so one gesture covers all of it.
//
// Patterns are written as ONE bar of sixteenth notes and repeated for however
// many bars the loop is, so changing kLoopBars does not invalidate them. Nothing
// here stores time in seconds or samples: a hit is a step, the looper turns steps
// into loop-relative phase, and tempo stays free to change afterwards.

namespace Rhythm {

// Sixteenth notes in one bar. A step is a sixteenth; step 0, 4, 8, 12 are the
// four beats.
static const uint8_t kStepsPerBar = 16;

struct Hit {
    uint8_t step;
    uint8_t id;  // a slot 0..11, or kIdClick / kIdClickAccent from looper.h
};

// Includes "off" at index 0.
uint8_t count();

const char *name(uint8_t index);

// Null with *hitCount == 0 for "off".
const Hit *hits(uint8_t index, uint16_t *hitCount);

// Colour for the TAP pad, so which rhythm is running is visible rather than
// something you have to remember. Packed 0xRRGGBB.
uint32_t colour(uint8_t index);

}  // namespace Rhythm
