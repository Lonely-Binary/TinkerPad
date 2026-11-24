// synth.h - the sound engine: a small real-time synthesiser plus the band.
//
// Nothing is a recording. Every sound is computed here, sample by sample:
//   PLUCK  the melody notes you play (a bright, short, bell-ish pluck)
//   BASS   the bass line
//   PAD    soft chords under everything
//   KICK / SNARE / HAT   the drums
//
// The band (drums, bass, chords) is played by a sequencer that runs INSIDE the
// audio task, locked to the audio sample counter. That counter is also the
// game's master clock: notes, judgement and the web page all hang off it, so
// the music and the game can never drift apart.
//
// Runs on core 1 at high priority. Wi-Fi lives on core 0, so a busy phone
// cannot make the speaker stutter.

#pragma once
#include <Arduino.h>
#include <math.h>
#include <driver/i2s_std.h>
#include "songs.h"

#define SR            22050
#define CHUNK         64                  // frames mixed at a time (2.9 ms)
#define NVOICES       18
#define PIN_BCLK      17                  // NS4168 speaker amp on I2S
#define PIN_LRCLK     16
#define PIN_DIN       18
#define MASTER_GAIN   1.5f

// How far behind "frames handed to I2S" the sound you can actually hear is.
// The DMA holds 3 chunks (192 frames); this is the part of that still queued.
#define AUDIO_LATENCY_FRAMES  150

enum VType : uint8_t { V_PLUCK, V_BASS, V_PAD, V_KICK, V_SNARE, V_HAT };

struct Cmd { uint8_t type; uint8_t midi; float vol; };

namespace Synth {

static float SINE[1024];
static i2s_chan_handle_t spk;
static QueueHandle_t cmdQueue;

// ---- master clock ----
static volatile uint32_t framesPlayed = 0;   // frames given to I2S since boot
static volatile uint32_t chunkMicros = 0;    // micros() when framesPlayed last moved

// ---- sequencer request (written by the game, read by the audio task) ----
static const Song *volatile seqSong = nullptr;
static volatile uint32_t seqStartFrame = 0;
static volatile uint16_t seqBars = 0;        // count-in + song + ending
static volatile uint32_t seqEpoch = 0;       // bumped for every new start
static volatile bool seqRunning = false;
static volatile bool stopRequest = false;

struct Voice {
    bool on = false;
    uint8_t type = 0;
    uint16_t delay = 0;                      // frames to wait before sounding
    uint32_t ph = 0, inc = 0;                // phase, phase step (x 2^32)
    float env = 0, dec = 0, vol = 0;
    float a = 0;                             // kick: swept phase; hat: last sample
    uint32_t age = 0;
};
static Voice voices[NVOICES];
static uint32_t noiseState = 2463534242u;

static inline float noise() {
    noiseState ^= noiseState << 13;
    noiseState ^= noiseState >> 17;
    noiseState ^= noiseState << 5;
    return (int32_t)noiseState / 2147483648.0f;
}

static inline float tsin(uint32_t ph) { return SINE[ph >> 22]; }

static inline float midiHz(float m) { return 440.0f * powf(2.0f, (m - 69.0f) / 12.0f); }

static inline float decayPerSample(float tauSeconds) {
    return expf(-1.0f / (tauSeconds * SR));
}

static void startVoice(uint8_t type, float midi, float vol, uint16_t delay) {
    int pick = 0;
    uint32_t oldest = 0;
    bool found = false;
    for (int i = 0; i < NVOICES; i++) {
        if (!voices[i].on) { pick = i; found = true; break; }
        if (voices[i].age >= oldest) { oldest = voices[i].age; pick = i; }
    }
    (void)found;
    Voice &v = voices[pick];
    v.on = true;
    v.type = type;
    v.delay = delay;
    v.ph = 0;
    v.age = 0;
    v.vol = vol;
    v.env = 1.0f;
    v.a = 0;
    float hz = midiHz(midi);
    v.inc = (uint32_t)(hz / SR * 4294967296.0f);
    switch (type) {
        case V_PLUCK: v.dec = decayPerSample(0.20f); break;
        case V_BASS:  v.dec = decayPerSample(0.22f); break;
        case V_PAD:   v.dec = decayPerSample(1.10f); break;
        case V_KICK:  v.dec = decayPerSample(0.10f); break;
        case V_SNARE: v.dec = decayPerSample(0.07f); break;
        case V_HAT:   v.dec = decayPerSample(0.025f); break;
    }
}

static inline float renderVoice(Voice &v) {
    float s = 0;
    float e = v.env;
    switch (v.type) {
        case V_PLUCK: {
            float att = v.age < 40 ? v.age / 40.0f : 1.0f;
            s = tsin(v.ph) + 0.45f * tsin(v.ph * 2) * e + 0.25f * tsin(v.ph * 3) * e * e;
            s *= e * att * 0.55f;
            break;
        }
        case V_BASS: {
            float att = v.age < 60 ? v.age / 60.0f : 1.0f;
            s = (tsin(v.ph) + 0.7f * tsin(v.ph * 2) + 0.25f * tsin(v.ph * 3)) * e * att * 0.5f;
            break;
        }
        case V_PAD: {
            float att = v.age < 2500 ? v.age / 2500.0f : 1.0f;
            s = (tsin(v.ph) * 0.6f + tsin(v.ph * 2) * 0.18f + tsin(v.ph + (v.ph >> 7)) * 0.3f) * e * att * 0.35f;
            break;
        }
        case V_KICK: {
            float t = v.age / (float)SR;
            float f = 46.0f + 150.0f * expf(-t * 45.0f);
            v.a += f / SR;
            uint32_t ph = (uint32_t)((v.a - floorf(v.a)) * 4294967296.0f);
            s = tsin(ph) * e * 1.1f;
            if (v.age < 30) s += noise() * 0.3f * (1.0f - v.age / 30.0f);   // beater click
            break;
        }
        case V_SNARE: {
            float n = noise();
            float tone = tsin(v.ph) * e * 0.5f;
            s = (n * e * 0.8f + tone);
            break;
        }
        case V_HAT: {
            float n = noise();
            s = (n - v.a) * e * 0.6f;                                      // high-passed noise
            v.a = n;
            break;
        }
    }
    v.ph += v.inc;
    v.env = e * v.dec;
    v.age++;
    if (v.env < 0.004f) v.on = false;
    return s * v.vol;
}

// What the band plays on one sixteenth-note step of the song.
static void bandStep(const Song &sg, uint32_t step, uint16_t delay, uint16_t totalBars) {
    uint32_t bar = step / 16;
    uint8_t st = step % 16;
    const uint8_t *scale = sg.minor ? SCALE_MINOR : SCALE_MAJOR;
    uint8_t deg = sg.prog[bar % 4];
    float rootMidi = sg.root + scale[deg % 7] + 12 * (deg / 7);
    bool last = (bar == (uint32_t)totalBars - 1u);

    if (last) {                                    // the ending: one big chord
        if (st == 0) {
            startVoice(V_KICK, 0, 1.0f, delay);
            startVoice(V_PAD, sg.root + 0, 0.9f, delay);
            startVoice(V_PAD, sg.root + (sg.minor ? 3 : 4), 0.8f, delay);
            startVoice(V_PAD, sg.root + 7, 0.8f, delay);
            startVoice(V_BASS, sg.root - 12, 0.9f, delay);
        }
        return;
    }
    if (sg.kick[st] == 'x') startVoice(V_KICK, 0, 1.0f, delay);
    if (sg.hat[st] == 'x') startVoice(V_HAT, 0, st % 4 == 2 ? 0.55f : 0.35f, delay);
    if (bar == 0) {                                // count-in: just kick and hats
        if (st == 0) startVoice(V_PAD, rootMidi, 0.6f, delay);
        return;
    }
    if (sg.snare[st] == 'x') startVoice(V_SNARE, 52, 0.8f, delay);
    if (sg.bass[st] == 'x') startVoice(V_BASS, rootMidi - 12, 0.85f, delay);
    if (st == 0) {                                 // the chord for this bar
        startVoice(V_PAD, rootMidi, 0.55f, delay);
        uint8_t third = scale[(deg + 2) % 7] + 12 * ((deg + 2) / 7);
        uint8_t fifth = scale[(deg + 4) % 7] + 12 * ((deg + 4) / 7);
        startVoice(V_PAD, sg.root + third, 0.5f, delay);
        startVoice(V_PAD, sg.root + fifth, 0.5f, delay);
    }
}

static void runSequencer(uint32_t chunkStart) {
    static uint32_t lastEpoch = 0, step = 0;
    if (seqEpoch != lastEpoch) {                   // a new song was requested
        lastEpoch = seqEpoch;
        step = 0;
    }
    if (!seqRunning) return;
    const Song *sg = seqSong;
    float stepFrames = SR * 60.0f / sg->bpm / 4.0f;
    uint32_t total = (uint32_t)seqBars * 16;
    while (step < total) {
        uint32_t f = seqStartFrame + (uint32_t)(step * stepFrames);
        if ((int32_t)(f - (chunkStart + CHUNK)) >= 0) return;   // later chunk
        uint16_t delay = (int32_t)(f - chunkStart) > 0 ? (uint16_t)(f - chunkStart) : 0;
        bandStep(*sg, step, delay, seqBars);
        step++;
    }
    seqRunning = false;                            // last step issued
}

static void audioTask(void *) {
    int16_t buf[CHUNK * 2];
    while (true) {
        if (stopRequest) {
            stopRequest = false;
            seqRunning = false;
            for (Voice &v : voices) v.on = false;
        }
        Cmd c;
        while (xQueueReceive(cmdQueue, &c, 0)) startVoice(c.type, c.midi, c.vol, 0);

        runSequencer(framesPlayed);

        for (int i = 0; i < CHUNK; i++) {
            float sum = 0;
            for (Voice &v : voices) {
                if (!v.on) continue;
                if (v.delay) { v.delay--; continue; }
                sum += renderVoice(v);
            }
            int16_t s = (int16_t)(tanhf(sum * MASTER_GAIN) * 30000.0f);
            buf[i * 2] = s;
            buf[i * 2 + 1] = s;
        }
        size_t written;
        i2s_channel_write(spk, buf, sizeof(buf), &written, portMAX_DELAY);
        chunkMicros = micros();
        framesPlayed = framesPlayed + CHUNK;
    }
}

static void begin() {
    for (int i = 0; i < 1024; i++) SINE[i] = sinf(2.0f * (float)M_PI * i / 1024.0f);
    cmdQueue = xQueueCreate(24, sizeof(Cmd));

    i2s_chan_config_t chan = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
    chan.dma_desc_num = 3;                         // small buffers = little delay
    chan.dma_frame_num = CHUNK;
    chan.auto_clear = true;                        // silence, never a stuck buzz
    i2s_new_channel(&chan, &spk, nullptr);
    i2s_std_config_t cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SR),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = (gpio_num_t)PIN_BCLK,
            .ws = (gpio_num_t)PIN_LRCLK,
            .dout = (gpio_num_t)PIN_DIN,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {false, false, false},
        },
    };
    i2s_channel_init_std_mode(spk, &cfg);
    i2s_channel_enable(spk);
    xTaskCreatePinnedToCore(audioTask, "audio", 6144, nullptr, 10, nullptr, 1);
}

// ---- called from the game ----

static void play(uint8_t type, float midi, float vol = 1.0f) {
    Cmd c = {type, (uint8_t)midi, vol};
    xQueueSend(cmdQueue, &c, 0);
}

// Audio frames given to I2S, with the part of the current chunk filled in.
static uint32_t now() {
    uint32_t f = framesPlayed;
    uint32_t us = micros() - chunkMicros;
    uint32_t extra = (uint64_t)us * SR / 1000000u;
    if (extra > CHUNK) extra = CHUNK;
    return f + extra;
}

static void startSong(const Song *sg, uint16_t bars, uint32_t startFrame) {
    seqRunning = false;
    seqSong = sg;
    seqBars = bars;
    seqStartFrame = startFrame;
    seqEpoch = seqEpoch + 1;
    seqRunning = true;
}

static void stopAll() { stopRequest = true; }

}  // namespace Synth
