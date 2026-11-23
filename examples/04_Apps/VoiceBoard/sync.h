#pragma once

#include <Arduino.h>

// Keeps the messages of several boards identical. There is no pairing: every board
// says hello every two seconds, and any board that hears it starts syncing with it.
//
//   ESP-NOW mode   hellos are radio broadcasts; boards in range find each other
//   Wi-Fi mode     hellos are UDP broadcasts; boards on the same network find each other
//
// The two modes do not talk to each other.
//
// Syncing: every hello carries a fingerprint of the board's state. When two differ,
// the board with the lower MAC address opens a session: it fetches the other's list,
// then pulls what it lacks, pushes what the other lacks, and swaps "heard" bits and
// deletes. Nothing needs both boards online at once — whoever comes back catches up.
//
// This is an example program: anything running this firmware within reach is trusted
// and nothing is encrypted. Add pairing before using it anywhere that matters.

namespace Sync {

bool begin();

uint8_t peersOnline();

void print();

}  // namespace Sync
