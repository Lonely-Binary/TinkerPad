// sound.h - everything the speaker plays
//
//   Drums:  the real recordings in drum_samples.h (one voice per pad)
//   Melody: a marimba-like tone made with maths (a few voices, so notes can
//           ring into each other)
//
// mixSample() adds everything that is playing into one sample.

#pragma once
#include <math.h>
#include "drum_samples.h"           // also sets SAMPLE_RATE (32000)

float masterVolume = 0.6;           // set by the volume key

// ---------- Drums ----------

struct DrumVoice {
    bool     on = false;
    uint32_t pos = 0;               // next sample of the recording
    float    volume = 1;
};
DrumVoice drums[16];

#define PAD_PEDAL_HAT   10
#define PAD_CLOSED_HAT  14
#define PAD_OPEN_HAT    15

void playDrum(int pad, float volume) {
    drums[pad].on = true;
    drums[pad].pos = 0;
    drums[pad].volume = volume;
    // A closing hi-hat stops the open hi-hat, like a real one
    if (pad == PAD_PEDAL_HAT || pad == PAD_CLOSED_HAT) drums[PAD_OPEN_HAT].on = false;
}

// ---------- Melody (marimba) ----------
// A marimba note = a sine wave that fades slowly, plus a higher "knock"
// (four times the pitch) that fades very fast. That knock is what makes it
// sound like a mallet hitting wood instead of a beep.

#define NOTE_VOICES 4

struct NoteVoice {
    bool  on = false;
    float hz = 0;
    float phase = 0, knockPhase = 0;
    float env = 0, knock = 0;       // loudness of the tone and of the knock
    float fall = 0;                 // env is multiplied by this every sample
    long  left = 0;                 // samples until the note is let go
    float volume = 1;
};
NoteVoice notes[NOTE_VOICES];

float fallFor(float seconds) { return expf(-1.0f / (seconds * SAMPLE_RATE)); }

void playNote(float hz, int ms, float volume) {
    // Use a free voice, or the quietest one
    int best = 0;
    for (int i = 0; i < NOTE_VOICES; i++) {
        if (!notes[i].on) { best = i; break; }
        if (notes[i].env < notes[best].env) best = i;
    }
    NoteVoice &v = notes[best];
    v.on = true;
    v.hz = hz;
    v.phase = v.knockPhase = 0;
    v.env = 1;
    v.knock = 1;
    v.fall = fallFor(0.9);
    v.left = (long)ms * SAMPLE_RATE / 1000;
    v.volume = volume;
}

void stopAllSound() {
    for (auto &d : drums) d.on = false;
    for (auto &n : notes) n.on = false;
}

float nextNoteSample(NoteVoice &v) {
    v.phase += 2 * M_PI * v.hz / SAMPLE_RATE;
    if (v.phase > 2 * M_PI) v.phase -= 2 * M_PI;
    v.knockPhase += 2 * M_PI * v.hz * 4 / SAMPLE_RATE;
    if (v.knockPhase > 2 * M_PI) v.knockPhase -= 2 * M_PI;

    float out = sinf(v.phase) * v.env + 0.35f * sinf(v.knockPhase) * v.knock;
    v.knock *= 0.9985f;                             // knock gone in ~20 ms
    if (--v.left == 0) v.fall = fallFor(0.04);      // note let go: fade quickly
    v.env *= v.fall;
    if (v.env < 0.001f) v.on = false;
    return out * v.volume;
}

// ---------- Mixer ----------

float mixSample() {
    float sum = 0;
    for (int i = 0; i < 16; i++) {
        DrumVoice &d = drums[i];
        if (!d.on) continue;
        sum += SAMPLES[i][d.pos++] / 32768.0f * d.volume;
        if (d.pos >= LENGTHS[i]) d.on = false;
    }
    for (auto &n : notes) {
        if (n.on) sum += 0.55f * nextNoteSample(n);
    }
    return tanhf(sum * masterVolume);               // soft limit: no crackle
}
