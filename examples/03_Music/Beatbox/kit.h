#pragma once

#include <Arduino.h>

// The factory kits, synthesised into PSRAM instead of shipped as sample files.
//
// The board has to make a good noise before anybody beatboxes at it: most people
// cannot beatbox, and a device that demands it first makes them feel like the
// problem. Synthesising the sounds means there are no sample files at all, so no
// licences to track and nothing that can quietly ship without permission.
//
// Four kits, twelve sounds each, one per pad:
//
//   0  808       the classic drum machine
//   1  Acoustic  a real kit: ride, rimshot, shaker, floor tom
//   2  Lo-fi     the 808 sounds through a crushed, dull, crackly old sampler
//   3  Vocal     mouth sounds and sung "ah / oh / ee / oo" chops
//
// A kit is synthesised the first time it is chosen and kept from then on. The
// envelopes use a fast exp() approximation, which is what makes that quick enough
// to do while you play (a few hundred milliseconds per kit).
//
// Every sound is peak-normalised to the same level, so switching kits never means
// re-tuning the mixer. Your recorded sounds (MIC + pad) stay on their pads when
// the kit changes; only the factory sounds underneath change.

namespace Kit {

static const uint8_t kCount = 4;

// Builds the clicks and the chosen kit, and puts the kit on the pads.
bool begin(uint32_t sampleRate, uint8_t kit);

// Puts a kit on the pads, synthesising it first if this is its first use.
bool select(uint8_t kit);

uint8_t current();
const char *name(uint8_t kit);

// A colour for each kit (0xRRGGBB), flashed on the pads when it is chosen.
uint32_t colour(uint8_t kit);

// The metronome clicks. Deliberately not slots — there are twelve pads and all
// twelve belong to the player. Short, bright and narrow-band so they stay
// audible over a full bar on a small speaker.
const int16_t *clickData(bool accent);
uint32_t clickFrames(bool accent);

uint32_t bytesUsed();
uint32_t buildMillis();  // how long the last kit took to synthesise

}  // namespace Kit
