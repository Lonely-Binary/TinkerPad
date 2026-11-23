#pragma once

#include <Arduino.h>

// Three numberings for the same 16 pads, which this board fixes at manufacture:
//
// On the finished board all three numberings are the same, and they count the way
// you read: row by row, left to right. Key 0 is top-left, key 3 is top-right, key 15
// is bottom-right, and the LED under key n is LED n.
//
// (An earlier version assumed the keys were numbered down the columns and
// transposed everything. The board said otherwise, so there is no mapping at all.)
//
//   [ 0 ][ 1 ][ 2 ][ 3 ]     twelve people, one key each
//   [ 4 ][ 5 ][ 6 ][ 7 ]
//   [ 8 ][ 9 ][10 ][11 ]
//   [MIC][重听][删除][全家]

namespace Layout {

enum Role : uint8_t {
    ROLE_PERSON,  // a family member's mailbox
    ROLE_MIC,     // hold, then hold a person's key, to leave them a message
    ROLE_REPLAY,  // play the last message again
    ROLE_DELETE,  // hold to delete the last message played
    ROLE_ALL,     // tap: play the latest family announcement.
                  // MIC + this: leave one
};

// Key n is at position n: keys, LEDs and reading order all count the same way,
// row by row, left to right.
constexpr uint8_t posForKey(uint8_t bit) { return bit; }
constexpr uint8_t keyForPos(uint8_t pos) { return pos; }

// The LED under a key has the key's own number.
constexpr uint8_t ledForKey(uint8_t bit) { return bit; }
constexpr uint8_t ledForPos(uint8_t pos) { return ledForKey(keyForPos(pos)); }

Role roleForKey(uint8_t bit);

// 0..11 for a person's key, 0xFF otherwise.
uint8_t personForKey(uint8_t bit);

void print();

}  // namespace Layout
