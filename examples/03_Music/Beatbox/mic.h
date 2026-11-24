#pragma once

#include <Arduino.h>

// PDM microphone capture, running continuously in its own task so a sampling
// gesture never has to wait for the I2S channel to start.
//
// Two details here are the difference between a sample that feels tight and one
// that feels amateur:
//
// Pre-roll. The microphone is always writing into a short ring buffer, so a
// capture starts ~150 ms BEFORE the gesture was recognised. Without it every
// recorded sound loses its attack — you cannot press a key and start a plosive
// at the same instant, and the attack is the part that makes a kick a kick.
//
// DC removal. The LMD2718 sits on a DC offset of about -500 counts. Measuring
// plain RMS reads roughly the same whether or not the
// carrier board is even attached; capturing without removing it puts a step at
// the start of every sample and wastes headroom that normalisation then
// amplifies.

namespace Mic {

bool begin(uint32_t sampleRate);

// Begins writing into dest, starting with the pre-roll already captured.
void startCapture(int16_t *dest, uint32_t capacity);

// Ends the capture and returns how many frames were written.
uint32_t stopCapture();

bool capturing();
uint32_t capturedFrames();

// Samples at or beyond 0.1% of full scale during the last capture. Beatboxing
// right at the microphone can clip it, and software cannot repair a clipped
// recording — this is the number that says whether it happened.
uint32_t clipped();

// Peak of the last capture, in dBFS, before normalisation. Very low means the
// user was too far away and normalising will bring the noise floor up with it.
float lastPeakDbfs();

// Smoothed AC level, 0..1, for live LED feedback while sampling.
float level();

// Trims silence, fades the edges and peak-normalises in place. Returns the new
// length. Call only after stopCapture(), when nothing is writing any more.
uint32_t polish(int16_t *data, uint32_t frames);

uint32_t preRollFrames();

}  // namespace Mic
