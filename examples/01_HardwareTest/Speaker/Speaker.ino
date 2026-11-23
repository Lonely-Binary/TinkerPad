// Speaker - plays the notes 0 to 7, then 7 back down to 0, forever
// Board: ESP32-S3 N16R8 (16 MB flash, 8 MB PSRAM)
// Setup guide: see ../README.md
//
// The amplifier chip (NS4168) receives digital audio over I2S:
//   BCLK  = bit clock      (GPIO17)
//   LRCLK = left/right clock (GPIO16)
//   DIN   = the audio data (GPIO18)
//
// We calculate a sine wave for each note and send it out.
// Notes 0-7 are the C major scale: Do Re Mi Fa Sol La Si Do.

#include <math.h>
#include <driver/i2s_std.h>

#define PIN_BCLK     17
#define PIN_LRCLK    16
#define PIN_DIN      18
#define SAMPLE_RATE  16000

i2s_chan_handle_t tx;          // the I2S output channel

// Play a sine wave of the given frequency (Hz) and volume (0.0 - 1.0)
void playTone(float freq, float volume, int ms) {
    int total = SAMPLE_RATE * ms / 1000;        // number of samples to send
    static float angle = 0;

    for (int i = 0; i < total; i++) {
        int16_t sample = sinf(angle) * volume * 30000;
        angle += 2 * M_PI * freq / SAMPLE_RATE;

        int16_t frame[2] = {sample, sample};    // left and right
        size_t written;
        i2s_channel_write(tx, frame, sizeof(frame), &written, portMAX_DELAY);
    }
}

void setup() {
    // Create the I2S channel
    i2s_chan_config_t chan = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
    i2s_new_channel(&chan, &tx, nullptr);

    // Set it up: 16 kHz, 16 bit, stereo, on our three pins
    i2s_std_config_t cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                        I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = (gpio_num_t)PIN_BCLK,
            .ws   = (gpio_num_t)PIN_LRCLK,
            .dout = (gpio_num_t)PIN_DIN,
            .din  = I2S_GPIO_UNUSED,
            .invert_flags = {false, false, false},
        },
    };
    i2s_channel_init_std_mode(tx, &cfg);
    i2s_channel_enable(tx);
}

// Frequency (Hz) of notes 0-7
float notes[8] = {262, 294, 330, 349, 392, 440, 494, 523};

void loop() {
    // Go up: 0, 1, 2 ... 7
    for (int n = 0; n <= 7; n++) {
        playTone(notes[n], 0.3, 300);
    }
    // Go down: 6, 5 ... 1 (then loop() starts again at 0)
    for (int n = 6; n >= 1; n--) {
        playTone(notes[n], 0.3, 300);
    }
}
