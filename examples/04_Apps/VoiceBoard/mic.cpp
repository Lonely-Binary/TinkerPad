#include "mic.h"

#include <driver/i2s_pdm.h>
#include <math.h>

#include "config.h"
#include "pins.h"

namespace Mic {

static i2s_chan_handle_t g_rx = nullptr;
static uint32_t g_sampleRate = kSampleRate;

// 150 ms is enough to cover the gap between "I decided to record" and the
// gesture being recognised (kSampleHoldMs plus human reaction), while staying
// small enough to live in internal RAM where the mic task can touch it without
// a PSRAM round trip.
static const float kPreRollSeconds = 0.15f;
static int16_t *g_ring = nullptr;
static uint32_t g_ringLen = 0;
static uint32_t g_ringWrite = 0;
static uint32_t g_ringFilled = 0;

static int16_t g_chunk[kRxDmaFrames];

enum State : uint8_t { IDLE = 0, START_REQUESTED = 1, CAPTURING = 2 };
static volatile uint8_t g_state = IDLE;
static int16_t *volatile g_dest = nullptr;
static volatile uint32_t g_destCap = 0;
static volatile uint32_t g_captured = 0;
static volatile uint32_t g_clipped = 0;
static volatile int32_t g_capturePeak = 0;
static volatile float g_level = 0.0f;

// One-pole DC blocker. R close to 1 keeps the corner low enough (around 3 Hz at
// 44.1 kHz) that it removes the offset without touching the bottom of a kick.
static const float kDcR = 0.9995f;
static float g_dcX1 = 0.0f;
static float g_dcY1 = 0.0f;

static inline int16_t dcBlock(int16_t x) {
    float y = (float)x - g_dcX1 + kDcR * g_dcY1;
    g_dcX1 = (float)x;
    g_dcY1 = y;
    return (int16_t)constrain(y, -32767.0f, 32767.0f);
}

static void micTask(void *) {
    for (;;) {
        size_t bytesRead = 0;
        if (i2s_channel_read(g_rx, g_chunk, sizeof(g_chunk), &bytesRead,
                             pdMS_TO_TICKS(200)) != ESP_OK ||
            bytesRead == 0) {
            continue;
        }
        size_t n = bytesRead / sizeof(int16_t);

        int64_t sumSq = 0;
        for (size_t i = 0; i < n; i++) {
            g_chunk[i] = dcBlock(g_chunk[i]);
            int32_t v = g_chunk[i];
            sumSq += (int64_t)v * v;
        }

        float rms = sqrtf((float)sumSq / (float)n) / 32768.0f;
        // Fast attack, slow release: the LED should jump when you make a noise
        // and settle gently, not flicker at the block rate.
        g_level = rms > g_level ? rms : g_level * 0.85f + rms * 0.15f;

        // Pre-roll ring is written unconditionally, even mid-capture, so the
        // next gesture also has its attack available.
        for (size_t i = 0; i < n; i++) {
            g_ring[g_ringWrite] = g_chunk[i];
            g_ringWrite = (g_ringWrite + 1) % g_ringLen;
            if (g_ringFilled < g_ringLen) g_ringFilled++;
        }

        uint8_t state = g_state;

        if (state == START_REQUESTED) {
            // Replay the ring oldest-first into the destination.
            uint32_t avail = min(g_ringFilled, g_ringLen);
            uint32_t start = (g_ringWrite + g_ringLen - avail) % g_ringLen;
            uint32_t written = 0;
            for (uint32_t i = 0; i < avail && written < g_destCap; i++) {
                g_dest[written++] = g_ring[(start + i) % g_ringLen];
            }
            g_captured = written;
            g_clipped = 0;
            g_capturePeak = 0;
            g_state = CAPTURING;
            continue;  // this block is already in the pre-roll we just copied
        }

        if (state == CAPTURING) {
            uint32_t cap = g_destCap;
            uint32_t pos = g_captured;
            for (size_t i = 0; i < n && pos < cap; i++) {
                int16_t v = g_chunk[i];
                g_dest[pos++] = v;
                int32_t a = abs((int32_t)v);
                if (a > g_capturePeak) g_capturePeak = a;
                if (a >= 32700) g_clipped++;
            }
            g_captured = pos;
            if (pos >= cap) {
                g_state = IDLE;  // hit the 4 s ceiling
            }
        }
    }
}

bool begin(uint32_t sampleRate) {
    g_sampleRate = sampleRate;

    g_ringLen = (uint32_t)(kPreRollSeconds * sampleRate);
    g_ring = (int16_t *)malloc(g_ringLen * sizeof(int16_t));
    if (!g_ring) {
        Serial.println("  mic: pre-roll ring allocation failed");
        return false;
    }
    memset(g_ring, 0, g_ringLen * sizeof(int16_t));

    // PDM RX only exists on I2S0 on the ESP32-S3; the mixer has I2S1.
    i2s_chan_config_t chanCfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chanCfg.dma_desc_num = kRxDmaDescs;
    chanCfg.dma_frame_num = kRxDmaFrames;

    if (i2s_new_channel(&chanCfg, nullptr, &g_rx) != ESP_OK) {
        Serial.println("  mic: i2s_new_channel failed");
        return false;
    }

    i2s_pdm_rx_config_t pdmCfg = {
        .clk_cfg = I2S_PDM_RX_CLK_DEFAULT_CONFIG(sampleRate),
        .slot_cfg = I2S_PDM_RX_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                   I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .clk = (gpio_num_t)PIN_MIC_CLK,
            .din = (gpio_num_t)PIN_MIC_DATA,
            .invert_flags = {
                .clk_inv = false,
            },
        },
    };
    // L/R on the LMD2718 is tied to GND, so it only ever drives the left slot.
    // Averaging it with the empty right slot throws away 6 dB.
    pdmCfg.slot_cfg.slot_mask = I2S_PDM_SLOT_LEFT;

    if (i2s_channel_init_pdm_rx_mode(g_rx, &pdmCfg) != ESP_OK) {
        Serial.println("  mic: i2s_channel_init_pdm_rx_mode failed");
        return false;
    }
    if (i2s_channel_enable(g_rx) != ESP_OK) {
        Serial.println("  mic: i2s_channel_enable failed");
        return false;
    }

    // Priority 9: one below the mixer. If both are ready at once the speaker
    // wins, because a dropout is audible and a late microphone block is not.
    if (xTaskCreatePinnedToCore(micTask, "mic", 4096, nullptr, 9, nullptr, 1) !=
        pdPASS) {
        Serial.println("  mic: task failed to start");
        return false;
    }

    Serial.printf("  mic: PDM @ %u Hz, %.0f ms pre-roll\n", (unsigned)sampleRate,
                  kPreRollSeconds * 1000.0f);
    return true;
}

void startCapture(int16_t *dest, uint32_t capacity) {
    if (!dest || capacity == 0) return;
    g_dest = dest;
    g_destCap = capacity;
    g_captured = 0;
    g_state = START_REQUESTED;
}

uint32_t stopCapture() {
    g_state = IDLE;
    return g_captured;
}

bool capturing() { return g_state != IDLE; }
uint32_t capturedFrames() { return g_captured; }
uint32_t clipped() { return g_clipped; }

float lastPeakDbfs() {
    int32_t p = g_capturePeak;
    return 20.0f * log10f(max((float)p, 1.0f) / 32768.0f);
}

float level() { return g_level; }

uint32_t preRollFrames() { return g_ringLen; }

uint32_t polish(int16_t *data, uint32_t frames) {
    if (!data || frames == 0) return 0;

    int32_t peak = 0;
    for (uint32_t i = 0; i < frames; i++) {
        int32_t a = abs((int32_t)data[i]);
        if (a > peak) peak = a;
    }
    if (peak < 64) return 0;  // nothing but noise floor: treat as no capture

    // Trim to where the sound actually starts and stops. The gate is relative
    // to this capture's own peak, so it works the same whether the user
    // whispered or shouted, with an absolute floor so a silent recording does
    // not gate against its own noise.
    const int32_t gate = max((int32_t)(peak * 0.04f), (int32_t)150);

    uint32_t start = 0;
    while (start < frames && abs((int32_t)data[start]) < gate) start++;
    uint32_t end = frames;
    while (end > start && abs((int32_t)data[end - 1]) < gate) end--;
    if (end <= start) return 0;

    // Back off a little before the detected onset: the gate necessarily fires
    // part way up the attack, and clipping the first millisecond of a transient
    // is exactly what makes a sampled kick sound weak.
    const uint32_t lead = (uint32_t)(0.004f * g_sampleRate);
    start = start > lead ? start - lead : 0;

    uint32_t len = end - start;
    if (start > 0) {
        memmove(data, data + start, len * sizeof(int16_t));
    }

    const uint32_t fadeIn = min<uint32_t>(len / 20, (uint32_t)(0.002f * g_sampleRate));
    const uint32_t fadeOut = min<uint32_t>(len / 8, (uint32_t)(0.015f * g_sampleRate));
    const float scale = 0.85f * 32767.0f / (float)peak;

    for (uint32_t i = 0; i < len; i++) {
        float v = (float)data[i] * scale;
        if (fadeIn && i < fadeIn) v *= (float)i / (float)fadeIn;
        if (fadeOut && i >= len - fadeOut) {
            v *= (float)(len - i) / (float)fadeOut;
        }
        data[i] = (int16_t)constrain(v, -32767.0f, 32767.0f);
    }

    return len;
}

}  // namespace Mic
