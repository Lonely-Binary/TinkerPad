#pragma once

#include <Arduino.h>

// On the finished board the key bit, the LED index and the reading-order position
// are all the same number: row by row, left to right, 0 top-left to 15
// bottom-right, and the LED under key n is LED n. There is nothing to calibrate.
//
// The one thing that matters to the interaction model is position, because
// MIC/STOP/BEAT/UNDO are defined by where they sit on the board.

namespace Layout {

// The transport, bottom row, left to right.
//
// There is deliberately no REC. Recording is always on: press a pad and it joins
// the loop and repeats, with no mode to enter first. That was the single biggest
// thing standing between this and a five-year-old — "REC is a state you must be
// in for your playing to count" is a model a child will never build — and it also
// puts the product's whole differentiator behind a gate, which is backwards.
//
// Every key here does its job on a plain tap. Only two gestures on the entire
// board are holds, and both show what they are doing while you hold them.
enum Role : uint8_t {
    ROLE_SLOT,
    ROLE_MIC,   // hold, then hold a pad, to record your voice onto that pad
    ROLE_STOP,  // stop / continue
    ROLE_BEAT,  // tap for the next factory rhythm
    ROLE_UNDO,  // tap to undo the last thing; hold to clear everything
};

// Key n is at position n: keys, LEDs and reading order all count the same way,
// row by row, left to right.
constexpr uint8_t posForKey(uint8_t bit) { return bit; }
constexpr uint8_t keyForPos(uint8_t pos) { return pos; }

// The LED under a key has the key's own number.
constexpr uint8_t ledForKey(uint8_t bit) { return bit; }
constexpr uint8_t ledForPos(uint8_t pos) { return ledForKey(keyForPos(pos)); }

Role roleForKey(uint8_t bit);

// 0..11 for a sound pad, 0xFF for a transport key.
uint8_t slotForKey(uint8_t bit);

// Position of a sound slot's pad, for the LED layer.
uint8_t posForSlot(uint8_t slot);

void print();

const char *roleName(Role r);

}  // namespace Layout
