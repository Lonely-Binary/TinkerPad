#pragma once

#include <Arduino.h>

// One undo stack for everything the player does.
//
// UNDO does not mean "remove a layer" and it does not mean "put the factory sound
// back". It means *take back the last thing I did*, whichever of those it was.
// That single idea is worth more than any feature on this device:
//
//   - It is the only way a five-year-old can be trusted with a destructive
//     gesture. Recording over a pad's sound is irreversible-feeling and silent;
//     one undo makes exploring safe, and a device that is safe to explore is a
//     device that gets explored.
//   - It removes a whole gesture. Reverting one pad to its factory sound was a
//     real gap with no way to reach it, and it needs no key of its own: recording
//     a sample is just another action on the stack.
//   - It is how everything else the user already owns behaves.
//
// Layers are pushed by the looper as they open — nothing on the board marks a
// layer boundary since REC was removed, so the looper is the only thing that
// knows. Samples are pushed when a capture is committed.
//
// One deliberate limit: a slot keeps only its factory sound and its current
// recording, so undoing a sample always lands on the factory sound rather than on
// whatever you had recorded there before. Storing every past take would cost 4 s
// of PSRAM per level, and "back to how it came" is what people mean anyway.

namespace History {

void begin();

void pushLayer();
void pushSample(uint8_t slot);

// Undoes the most recent action. False when there was nothing left to undo.
bool undo();

void clear();

uint8_t depth();

// Prints the stack oldest-first. Worth having permanently: "why did UNDO do that"
// is otherwise unanswerable, and it caught a real bug where a console diagnostic
// committed a sample without recording the action.
void print();

}  // namespace History
