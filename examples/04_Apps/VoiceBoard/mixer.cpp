#include "mixer.h"

#include <driver/i2s_std.h>
#include <math.h>

#include "config.h"
#include "pins.h"

namespace Mixer {

struct Voice {
    const int16_t *data = nullptr;
    uint32_t frames = 0;
    uint32_t pos = 0;
    int32_t gainQ12 = 0;  // fixed point: the render loop runs 44100 times a
                          // second and has no business doing float per sample
    bool active = false;
};

// The voice array belongs to the audio task and nothing else ever touches it.
//
// The obvious alternative — a spinlock around the voice array — was tried first
// and is wrong here: the render loop walks up to 8 voices x 128 frames of PSRAM
// per block, and holding a portMUX across that disables interrupts on the core
// for tens of microseconds at a time, every 2.9 ms, forever.
//
// So triggers go through a single-producer ring instead. Every caller is on the
// Arduino task, so there is exactly one producer and no lock is needed at all.
// The cost is that a trigger waits at most one block (2.9 ms) to be picked up,
// which is small against the 11.6 ms of DMA it is queueing behind — and it does
// not affect musical timing either way, because the looper timestamps a pad hit
// when the matrix scan sees it, not when the sound comes out.
static Voice g_voices[kMaxVoices];

struct Cmd {
    const int16_t *data;
    uint32_t frames;
    int32_t gainQ12;
};

static const uint32_t kCmdSlots = 32;  // power of two, for the mask
static Cmd g_cmds[kCmdSlots];
static volatile uint32_t g_cmdHead = 0;  // written only by the producer
static volatile uint32_t g_cmdTail = 0;  // written only by the audio task
static volatile uint32_t g_dropped = 0;
static volatile bool g_stopAllReq = false;

static i2s_chan_handle_t g_tx = nullptr;
static volatile uint32_t g_samplesPlayed = 0;
static volatile bool g_muted = false;
static volatile uint32_t g_underruns = 0;
static volatile uint8_t g_activeVoices = 0;
static volatile uint8_t g_peakVoices = 0;
static volatile uint32_t g_renderUs = 0;
static volatile uint32_t g_wallUs = 0;

// Stereo out because the NS4168 is fed a standard Philips stereo frame; the
// same mono mix goes to both slots.
static int16_t g_out[kBlockFrames * 2];
static int32_t g_acc[kBlockFrames];

static volatile float g_makeup = kMakeupGain;

// Two cascaded one-pole high-passes on the master, 12 dB/octave.
//
//   y[n] = R * (y[n-1] + x[n] - x[n-1]),   R = exp(-2*pi*fc/fs)
//
// Cheap enough not to matter: two float multiply-adds per sample against a
// measured 3.4% for the whole 8-voice mix.
static volatile float g_hpR = 0.0f;  // 0 disables
static float g_hpXa = 0.0f, g_hpYa = 0.0f;
static float g_hpXb = 0.0f, g_hpYb = 0.0f;
static float g_hpHz = kHighPassHz;
static uint32_t g_sampleRate = kSampleRate;

static inline float highPass(float x) {
    float r = g_hpR;
    if (r <= 0.0f) return x;
    float ya = r * (g_hpYa + x - g_hpXa);
    g_hpXa = x;
    g_hpYa = ya;
    float yb = r * (g_hpYb + ya - g_hpXb);
    g_hpXb = ya;
    g_hpYb = yb;
    return yb;
}

// Soft limiter. Eight voices at 0.8 gain can sum well past full scale, and hard
// clipping a drum mix sounds like a fault rather than like loudness. This curve
// is asymptotic to 32000, so it never actually clips, and it is straight integer
// arithmetic — a tanh() per sample at 44.1 kHz is affordable on an S3 but there
// is no reason to spend it.
static inline int16_t softClip(int32_t v) {
    const int32_t knee = 22000;
    const int32_t range = 10000;
    if (v > knee) {
        int32_t over = v - knee;
        v = knee + (over * range) / (over + range);
    } else if (v < -knee) {
        int32_t over = -v - knee;
        v = -(knee + (over * range) / (over + range));
    }
    return (int16_t)constrain(v, -32767, 32767);
}

static void assign(const Cmd &cmd) {
    int8_t chosen = -1;
    for (uint8_t v = 0; v < kMaxVoices; v++) {
        if (!g_voices[v].active) {
            chosen = (int8_t)v;
            break;
        }
    }

    // Voice stealing: take whichever voice is closest to finishing. On a
    // percussion instrument that is nearly always the quietest one, because
    // every sound in the kit decays — so this is the steal you hear least.
    if (chosen < 0) {
        float best = -1.0f;
        for (uint8_t v = 0; v < kMaxVoices; v++) {
            const Voice &voice = g_voices[v];
            float progress = voice.frames ? (float)voice.pos / (float)voice.frames
                                          : 1.0f;
            if (progress > best) {
                best = progress;
                chosen = (int8_t)v;
            }
        }
    }

    Voice &voice = g_voices[chosen];
    voice.data = cmd.data;
    voice.frames = cmd.frames;
    voice.pos = 0;
    voice.gainQ12 = cmd.gainQ12;
    voice.active = true;
}

static void drainCommands() {
    if (g_stopAllReq) {
        g_stopAllReq = false;
        for (uint8_t v = 0; v < kMaxVoices; v++) {
            g_voices[v].active = false;
        }
    }

    while (g_cmdTail != g_cmdHead) {
        assign(g_cmds[g_cmdTail & (kCmdSlots - 1)]);
        g_cmdTail = g_cmdTail + 1;
    }
}

static void render(size_t frames) {
    memset(g_acc, 0, frames * sizeof(int32_t));

    uint8_t active = 0;
    for (uint8_t v = 0; v < kMaxVoices; v++) {
        Voice &voice = g_voices[v];
        if (!voice.active) continue;
        active++;

        uint32_t remaining = voice.frames - voice.pos;
        size_t n = min((size_t)remaining, frames);
        const int16_t *src = voice.data + voice.pos;

        for (size_t i = 0; i < n; i++) {
            g_acc[i] += ((int32_t)src[i] * voice.gainQ12) >> 12;
        }

        voice.pos += n;
        if (voice.pos >= voice.frames) {
            voice.active = false;
        }
    }

    g_activeVoices = active;
    if (active > g_peakVoices) g_peakVoices = active;

    const bool mute = g_muted;
    const float gain = kMasterGain * g_makeup;

    // mix -> high-pass -> makeup -> soft limiter. The high-pass comes before the
    // makeup so that the headroom it frees is headroom the makeup can actually
    // spend, which is the entire point of having it.
    for (size_t i = 0; i < frames; i++) {
        int16_t s;
        if (mute) {
            s = 0;
        } else {
            float v = highPass((float)g_acc[i]) * gain;
            s = softClip((int32_t)v);
        }
        g_out[i * 2] = s;
        g_out[i * 2 + 1] = s;
    }
}

static void audioTask(void *) {
    uint32_t lastWall = micros();
    uint32_t renderAccum = 0;
    uint32_t wallAccum = 0;

    for (;;) {
        uint32_t t0 = micros();
        drainCommands();
        render(kBlockFrames);
        uint32_t t1 = micros();

        size_t written = 0;
        esp_err_t err = i2s_channel_write(g_tx, g_out, sizeof(g_out), &written,
                                          pdMS_TO_TICKS(200));
        if (err != ESP_OK || written != sizeof(g_out)) {
            g_underruns++;
        }

        // Advance the master clock only by what actually reached the DMA, so the
        // looper's idea of time cannot run ahead of the speaker.
        g_samplesPlayed += written / (2 * sizeof(int16_t));

        renderAccum += t1 - t0;
        uint32_t nowUs = micros();
        wallAccum += nowUs - lastWall;
        lastWall = nowUs;

        if (wallAccum >= 500000) {
            g_renderUs = renderAccum;
            g_wallUs = wallAccum;
            renderAccum = 0;
            wallAccum = 0;
        }
    }
}

void setHighPassHz(float hz) {
    g_hpHz = hz;
    g_hpR = hz > 0.0f ? expf(-2.0f * (float)M_PI * hz / (float)g_sampleRate) : 0.0f;
}

float highPassHz() { return g_hpR > 0.0f ? g_hpHz : 0.0f; }

void setMakeup(float gain) { g_makeup = constrain(gain, 0.25f, 8.0f); }
float makeup() { return g_makeup; }

bool begin(uint32_t sampleRate) {
    g_sampleRate = sampleRate;
    setHighPassHz(kHighPassHz);

    i2s_chan_config_t chanCfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
    chanCfg.dma_desc_num = kTxDmaDescs;
    chanCfg.dma_frame_num = kTxDmaFrames;
    chanCfg.auto_clear = true;  // a starved DMA emits silence rather than the
                                // last block on repeat, which is a loud buzz

    if (i2s_new_channel(&chanCfg, &g_tx, nullptr) != ESP_OK) {
        Serial.println("  mixer: i2s_new_channel failed");
        return false;
    }

    i2s_std_config_t stdCfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(sampleRate),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                        I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = (gpio_num_t)PIN_SPK_BCLK,
            .ws = (gpio_num_t)PIN_SPK_LRCLK,
            .dout = (gpio_num_t)PIN_SPK_DIN,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };

    if (i2s_channel_init_std_mode(g_tx, &stdCfg) != ESP_OK) {
        Serial.println("  mixer: i2s_channel_init_std_mode failed");
        return false;
    }
    if (i2s_channel_enable(g_tx) != ESP_OK) {
        Serial.println("  mixer: i2s_channel_enable failed");
        return false;
    }

    // Core 1, priority 10. Core 0 carries the Arduino task, and a matrix scan
    // there blocks on I2C for 2.49 ms at a time — long enough to starve the DMA
    // if the render had to share with it.
    if (xTaskCreatePinnedToCore(audioTask, "mix", 4096, nullptr, 10, nullptr, 1) !=
        pdPASS) {
        Serial.println("  mixer: audio task failed to start");
        return false;
    }

    Serial.printf("  mixer: %u voices @ %u Hz, %u frame blocks, %u frames DMA "
                  "(%.1f ms output latency)\n",
                  kMaxVoices, (unsigned)sampleRate, (unsigned)kBlockFrames,
                  (unsigned)(kTxDmaDescs * kTxDmaFrames),
                  1000.0f * kTxDmaDescs * kTxDmaFrames / (float)sampleRate);
    return true;
}

void trigger(const int16_t *data, uint32_t frames, float gain) {
    if (!data || frames == 0) return;

    uint32_t head = g_cmdHead;
    if (head - g_cmdTail >= kCmdSlots) {
        g_dropped++;  // 32 triggers unconsumed in 2.9 ms is not a real gesture
        return;
    }

    g_cmds[head & (kCmdSlots - 1)] = {data, frames, (int32_t)(gain * 4096.0f)};
    // The command must be fully written before the audio task can see the index
    // move, or it renders from a half-populated slot.
    __sync_synchronize();
    g_cmdHead = head + 1;
}

void stopAll() { g_stopAllReq = true; }

void setMuted(bool m) { g_muted = m; }
bool muted() { return g_muted; }

uint32_t samplesPlayed() { return g_samplesPlayed; }

uint8_t activeVoices() { return g_activeVoices; }
uint8_t peakVoices() { return g_peakVoices; }

void resetStats() {
    g_peakVoices = 0;
    g_underruns = 0;
    g_dropped = 0;
}

float load() {
    uint32_t wall = g_wallUs;
    return wall ? (float)g_renderUs / (float)wall : 0.0f;
}

uint32_t underruns() { return g_underruns; }
uint32_t dropped() { return g_dropped; }

}  // namespace Mixer
