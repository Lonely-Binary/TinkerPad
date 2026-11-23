#pragma once

#include <Arduino.h>

// TinkerPad — VoiceBoard, a family message board.
//
// The audio constants are carried over from the loop-station firmware where they
// were measured on the prototype, and re-argued where voice is not drums.

// ----------------------------------------------------------------- audio

static const uint32_t kSampleRate = 44100;
static const size_t kBlockFrames = 128;
static const uint8_t kTxDmaDescs = 4;
static const uint16_t kTxDmaFrames = 128;
static const uint8_t kRxDmaDescs = 4;
static const uint16_t kRxDmaFrames = 256;

// One message plays at a time, so 8 voices is far more than needed. The mixer
// still wants the constant.
static const uint8_t kMaxVoices = 8;

static const float kVoiceGain = 1.00f;
static const float kMasterGain = 1.00f;

// Speech is already peak-normalised by Mic::polish, so the makeup gain can be
// lower than the drums' 1.8: a limiter working hard on a voice sounds squashed
// where on a snare it sounds big.
static const float kMakeupGain = 1.50f;

// 100 Hz rather than the drums' 120. The speaker cannot reproduce much below
// that anyway, and a male voice has useful body just above it.
static const float kHighPassHz = 100.0f;

// ------------------------------------------------------------- messages

static const uint8_t kPeople = 12;

// Recipient id meaning "everyone".
static const uint8_t kToAll = 0xFF;

// Longest single message. The two playback/capture buffers are each this long
// at 44.1 kHz / 16 bit, so 20 s costs 2 x 1.76 MB of PSRAM.
static const float kMaxMessageSeconds = 20.0f;

// Anything shorter than this is a stray tap on MIC, not a message.
static const uint32_t kMinMessageMs = 400;

// Messages are stored at 1/3 of the capture rate (14.7 kHz) as 4-bit IMA ADPCM:
// about 7.4 KB/s, so the ~9.9 MB FAT partition holds roughly 20 minutes.
static const uint8_t kStoreDecimate = 3;

// Free space to keep in reserve. When a new message would eat into it, the
// oldest already-heard messages go first.
static const uint32_t kReserveBytes = 256 * 1024;

// Recording is hands-free once it has started, so a child never has to hold a key
// and talk at the same time. It ends on a tap of any key, or by itself:
//   - after kSilenceStopMs of quiet once speech has been heard, or
//   - if nothing is said for kNoSpeechMs, which cancels instead of sending.
// 0.006 of full scale is about 15x the measured mic noise floor (-67 dBFS) and a
// good deal below ordinary speech at the distance the mic is used from.
static const float kVoiceLevel = 0.006f;
static const uint32_t kSilenceStopMs = 2500;
static const uint32_t kNoSpeechMs = 8000;

// After MIC is tapped, the board waits this long for a recipient, then gives up.
static const uint32_t kArmedMs = 10000;

// DELETE must be held this long. There is no way to tell whose hand is on the
// key, so a plain tap that destroys someone's message was judged too easy to do
// by accident.
static const uint16_t kDeleteHoldMs = 1000;

// ------------------------------------------------------------------ LEDs

static const uint8_t kLedBrightness = 48;
static const uint16_t kLedFrameMs = 16;  // ~60 Hz
