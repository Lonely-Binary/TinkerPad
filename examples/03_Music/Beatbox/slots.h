#pragma once

#include <Arduino.h>

// The 12 sound slots.
//
// Each slot has two independent bodies of audio in PSRAM: the factory sound,
// which is never overwritten, and a 4 s user buffer that a sampling gesture
// writes into. A slot plays whichever one is current.
//
// That split is what makes "power-cycling is the factory reset" true rather
// than aspirational, and it also makes reverting one slot free.
//
// Everything is allocated once at boot. No allocation ever happens while audio
// is running, so PSRAM cannot fragment mid-performance and a slot pointer handed
// to the mixer stays valid for the life of the program.

namespace Slots {

bool begin(uint32_t sampleRate, float maxSeconds);

// What this slot currently plays. Never null once begin() has succeeded.
const int16_t *data(uint8_t slot);
uint32_t frames(uint8_t slot);

bool isUser(uint8_t slot);
const char *name(uint8_t slot);

// Where a sampling gesture writes.
int16_t *userBuffer(uint8_t slot);
uint32_t userCapacity();

// Makes the just-captured audio the slot's current sound.
void commitUser(uint8_t slot, uint32_t frames);

void revertToFactory(uint8_t slot);
void revertAll();

// Registered by Kit at boot.
void setFactory(uint8_t slot, const int16_t *data, uint32_t frames,
                const char *name);

uint32_t bytesUsed();

void print();

}  // namespace Slots
