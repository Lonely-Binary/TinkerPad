#pragma once

#include <Arduino.h>

// There is no screen, so every piece of state is colour and motion on the key the
// person is already looking at. Anything that asks a child to read several keys at
// once has failed, so each state has ONE bright thing in it.
//
//   idle                    only MIC is lit (dim red). Nothing else, unless a
//                           message is waiting
//   unread                  steady glow in the person's colour, with one bright pulse
//                           per waiting message (up to 5), then a pause
//   playing                 that key breathes in its colour
//   MIC tapped ("for who?") MIC breathes red and every person's key (and 全家) lights
//                           in its own colour: "now tap who it is for"
//   recording               ONLY the recipient's key is lit, pulsing red with the
//                           voice; MIC steady red. Everything else is dark
//   sent                    two white blinks on the recipient's key + a chime
//   nothing / failed        two red blinks
//   DELETE                  amber; a held key fills and turns red before it fires

namespace LedUI {

bool begin();
void bootSweep();

// Position 0..11 is a person, 15 is ALL.
void setUnread(uint8_t person, uint8_t count);
void setHaveBroadcast(bool have);
void setHaveLast(bool have);

void setPlaying(int8_t pos, uint32_t untilMs);

// pos is the recipient's key: 0..11, or 15 for everyone. -1 for none.
// maxFrames is the capture capacity, for the last-seconds blink.
void setRecording(int8_t pos, uint32_t maxFrames);

void ack(int8_t pos, bool ok);
// MIC has been tapped and the board is waiting to be told who it is for.
void setArmed(bool armed);
void setDeleteHold(uint32_t heldMs);

// The single WS2812 on the ESP32-S3 board itself (GPIO48) shows the network:
//   blue, slow blink     the settings hotspot is open
//   yellow, fast blink   Wi-Fi mode, not connected (yet)
//   green                Wi-Fi mode, connected
//   cyan                 ESP-NOW mode
// Dim when this board is alone, bright when another board is in sync range.
enum NetStatus : uint8_t { NET_HOTSPOT, NET_WIFI_CONNECTING, NET_WIFI_OK, NET_ESPNOW };
void setNetStatus(NetStatus s, bool peers);

void render();

}  // namespace LedUI
