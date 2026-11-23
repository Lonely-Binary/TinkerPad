#pragma once

#include <Arduino.h>

// Network settings, and the little web page that changes them.
//
// For 15 minutes after power-on the board runs its own Wi-Fi hotspot, named
// VoiceBoard-XXXX, with no password. Join it from a phone and the settings page opens
// (or browse to http://192.168.4.1). On it you choose how the boards find each other:
//
//   ESP-NOW (default)   board to board by radio, no router needed
//   Wi-Fi               through your router: boards on the same Wi-Fi find each other
//
// In Wi-Fi mode the hotspot stays open for as long as the board cannot connect, so a
// wrong password can always be fixed.
//
// This is an example program, so it is deliberately open: no password on the hotspot,
// no confirmation before boards join each other. Add both before shipping anything.

namespace Portal {

enum Mode : uint8_t { MODE_ESPNOW = 0, MODE_WIFI = 1 };

void begin();
void loop();

Mode mode();
bool wifiConnected();
bool hotspotOn();

void closeHotspot();
void print();

}  // namespace Portal
