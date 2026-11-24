#pragma once

#include <Arduino.h>

// There is no screen. The 16 RGB pads are the entire user interface, which means
// every piece of state the player needs has to be expressible as colour and
// motion on the key they are already looking at.
//
// The vocabulary:
//
//   empty slot        off
//   loaded slot       steady, coloured by kit
//   recorded slot     the same colour with a white core, so you can see at a
//                     glance which pads are yours and which are factory
//   triggered         bright flash, decaying
//   sampling          pulsing red, brightness following the microphone level so
//                     the player can see it is hearing them
//   loop position     PLAY pulses each beat, brightest on beat 1
//   record armed      REC pulsing red
//   clearing          CLR ramps as you hold, and changes colour at each of the
//                     two thresholds so the second stage is never a surprise

namespace LedUI {

bool begin();

// A short sweep at boot. Doubles as a check that all 16 pixels obey data.
void bootSweep();

void flash(uint8_t slot);

// -1 for none.
void setSampling(int8_t slot);

void flashTap();

// All twelve sound pads flash in the new kit's colour (0xRRGGBB).
void flashKit(uint32_t rgb);

// A visible acknowledgement that UNDO did something. Removing a layer can be
// inaudible — if the layer you took back was quiet, or the rhythm is covering it —
// and a destructive button that might have done nothing is worse than one that
// clearly did.
void flashUndo();

// 0 when CLR is not held, otherwise how long it has been held, in ms.
void setClearHold(uint32_t heldMs);

void render();

}  // namespace LedUI
