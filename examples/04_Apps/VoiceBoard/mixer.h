#pragma once

#include <Arduino.h>

// The audio engine: an 8-voice sample mixer feeding I2S from its own task on
// core 1, while pads, LEDs and the looper run on core 0.
//
// Two things about this module are load-bearing for the whole instrument:
//
// 1. samplesPlayed() is the master clock. The looper schedules against it, not
//    against millis(). Those two clocks are not the same clock — the I2S rate
//    is derived from the APB PLL and the FreeRTOS tick is not — so a loop timed
//    on millis() drifts against the audio it is playing, slowly and audibly.
//
// 2. Nothing here allocates. Sample data lives in PSRAM owned by Slots, voices
//    are a fixed array, and the render buffers are static. An allocation on the
//    audio path is a dropout waiting for the wrong moment.

namespace Mixer {

bool begin(uint32_t sampleRate);

// Starts a sound on the next available voice. Safe to call from any task.
void trigger(const int16_t *data, uint32_t frames, float gain);

void stopAll();

// Silences the output without stopping the clock, so the loop keeps its place
// while a sampling gesture is capturing. Anything the speaker plays during
// capture ends up in the recording.
void setMuted(bool muted);
bool muted();

// Frames written to I2S since boot. Wraps every ~27 hours at 44.1 kHz, which
// unsigned arithmetic handles correctly for every difference the looper takes.
uint32_t samplesPlayed();

uint8_t activeVoices();
uint8_t peakVoices();
void resetStats();

// Render time as a fraction of real time: 0.25 means a quarter of one core is
// going on mixing. This is the number that answers DESIGN.md's open question
// about whether 8 voices is the right target.
float load();

// I2S write timeouts. Anything but zero means the audio task lost a race and
// the output stuttered.
uint32_t underruns();

// Triggers thrown away because the command ring was full.
uint32_t dropped();

// Gain applied before the soft limiter. This is the loudness control: it lifts
// the average level while the limiter holds the peaks. Found by ear.
void setMakeup(float gain);
float makeup();

// Master high-pass corner in Hz, 12 dB/octave. 0 disables it. Removing bass the
// speaker cannot reproduce anyway is worth more loudness than gain is.
void setHighPassHz(float hz);
float highPassHz();

}  // namespace Mixer
