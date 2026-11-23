// Microphone - shows how loud it is, as a bar on the Serial Monitor
// Board: ESP32-S3 N16R8 (16 MB flash, 8 MB PSRAM)
// Setup guide: see ../README.md
//
// The microphone (LMD2718) is a PDM microphone:
//   CLK  = GPIO40
//   DATA = GPIO39
//
// Make some noise (talk, clap) and watch the bar grow.

#include <math.h>
#include <driver/i2s_pdm.h>

#define PIN_MIC_CLK   40
#define PIN_MIC_DATA  39
#define SAMPLE_RATE   16000
#define NUM_SAMPLES   512

i2s_chan_handle_t rx;          // the I2S input channel
int16_t samples[NUM_SAMPLES];

void setup() {
    Serial.begin(115200);
    delay(600);

    // Create the I2S input channel
    i2s_chan_config_t chan = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    i2s_new_channel(&chan, nullptr, &rx);

    // Set it up for a PDM microphone on our two pins
    i2s_pdm_rx_config_t cfg = {
        .clk_cfg  = I2S_PDM_RX_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = I2S_PDM_RX_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                   I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .clk = (gpio_num_t)PIN_MIC_CLK,
            .din = (gpio_num_t)PIN_MIC_DATA,
            .invert_flags = {false},
        },
    };
    cfg.slot_cfg.slot_mask = I2S_PDM_SLOT_LEFT;
    i2s_channel_init_pdm_rx_mode(rx, &cfg);
    i2s_channel_enable(rx);
}

void loop() {
    // Read a block of samples from the microphone
    size_t bytesRead = 0;
    i2s_channel_read(rx, samples, sizeof(samples), &bytesRead, 1000);
    int n = bytesRead / 2;
    if (n == 0) return;

    // This microphone has a constant offset, so first find the average...
    float mean = 0;
    for (int i = 0; i < n; i++) mean += samples[i];
    mean /= n;

    // ...then measure how far the samples move away from it (the loudness)
    float sum = 0;
    for (int i = 0; i < n; i++) {
        float d = samples[i] - mean;
        sum += d * d;
    }
    float loudness = sqrtf(sum / n);

    // Draw a bar: one '#' for every 20 units of loudness
    int bars = loudness / 20;
    if (bars > 50) bars = 50;
    for (int i = 0; i < bars; i++) Serial.print('#');
    Serial.printf(" %.0f\n", loudness);

    delay(100);
}
