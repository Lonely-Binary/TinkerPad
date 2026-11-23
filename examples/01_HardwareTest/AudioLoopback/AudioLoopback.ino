// Audio Loopback - hold any key to record, release it to play back
// Board: ESP32-S3 N16R8 (16 MB flash, 8 MB PSRAM)
// Setup guide: see ../README.md
//
// Microphone (PDM):  CLK = GPIO40, DATA = GPIO39
// Speaker (I2S):     BCLK = GPIO17, LRCLK = GPIO16, DIN = GPIO18
// Keys (PCF8574):    I2C address 0x20, SDA = GPIO9, SCL = GPIO8
// LEDs (WS2812B):    16 LEDs on GPIO38, LED number = key number
//
// While you hold a key: its LED is red (recording).
// After you release it: its LED is blue (playing), then it turns off.
//
// The recording is stored in PSRAM (set Tools > PSRAM to "OPI PSRAM").
// Maximum recording length: 10 seconds.

#include <Wire.h>
#include <FastLED.h>
#include <driver/i2s_std.h>
#include <driver/i2s_pdm.h>

#define PIN_SDA       9
#define PIN_SCL       8
#define PCF8574_ADDR  0x20

#define PIN_BCLK      17
#define PIN_LRCLK     16
#define PIN_DIN       18
#define PIN_MIC_CLK   40
#define PIN_MIC_DATA  39

#define PIN_LED       38
#define NUM_LEDS      16
#define BRIGHTNESS    40

#define SAMPLE_RATE   16000
#define MAX_SECONDS   10
#define MAX_SAMPLES   (SAMPLE_RATE * MAX_SECONDS)
#define BLOCK         256
#define GAIN          2        // how much louder (1 = same volume)

i2s_chan_handle_t tx;          // speaker channel
i2s_chan_handle_t rx;          // microphone channel
int16_t *recording;            // the recorded sound (lives in PSRAM)
int16_t block[BLOCK];          // one small block read from the microphone
CRGB leds[NUM_LEDS];           // the colour of every LED

// Returns true if any of the 16 keys is pressed
bool anyKeyPressed() {
    // Pull all 4 rows low, then look at the 4 column pins (bits 4-7)
    Wire.beginTransmission(PCF8574_ADDR);
    Wire.write(0xF0);
    Wire.endTransmission();

    Wire.requestFrom(PCF8574_ADDR, 1);
    uint8_t pins = Wire.read();
    return (pins & 0xF0) != 0xF0;       // any column low = a key is down
}

// Returns the number (0-15) of a pressed key, or -1 if no key is pressed
int findKey() {
    for (int row = 0; row < 4; row++) {
        // Pull this row low, leave everything else high
        Wire.beginTransmission(PCF8574_ADDR);
        Wire.write(0xF0 | (0x0F & ~(1 << row)));
        Wire.endTransmission();

        Wire.requestFrom(PCF8574_ADDR, 1);
        uint8_t pins = Wire.read();
        for (int col = 0; col < 4; col++) {
            if ((pins & (1 << (col + 4))) == 0) {
                return row * 4 + col;
            }
        }
    }
    return -1;
}

// Light one LED with a colour (CRGB::Black turns it off)
void setLed(int index, CRGB colour) {
    fill_solid(leds, NUM_LEDS, CRGB::Black);
    leds[index] = colour;
    FastLED.show();
}

// Read one block from the microphone. Returns how many samples we got.
int readMic() {
    size_t bytesRead = 0;
    i2s_channel_read(rx, block, sizeof(block), &bytesRead, 100);
    return bytesRead / 2;
}

void play(int count) {
    // The microphone has a constant offset. Remove it so there is no click.
    long sum = 0;
    for (int i = 0; i < count; i++) sum += recording[i];
    int offset = sum / count;

    for (int i = 0; i < count; i++) {
        int16_t s = (recording[i] - offset) * GAIN;
        int16_t frame[2] = {s, s};              // left and right
        size_t written;
        i2s_channel_write(tx, frame, sizeof(frame), &written, portMAX_DELAY);
    }

    // Finish with silence. When we stop sending data, the I2S hardware keeps
    // repeating the last data it has, which sounds like endless clicking.
    // Filling it with zeros makes it repeat silence instead.
    int16_t silence[2] = {0, 0};
    for (int i = 0; i < SAMPLE_RATE / 4; i++) {
        size_t written;
        i2s_channel_write(tx, silence, sizeof(silence), &written, portMAX_DELAY);
    }
}

void setup() {
    Serial.begin(115200);
    delay(600);

    recording = (int16_t *)ps_malloc(MAX_SAMPLES * sizeof(int16_t));
    if (recording == NULL) {
        Serial.println("Not enough memory. Set Tools > PSRAM to OPI PSRAM.");
        while (true) delay(1000);
    }

    Wire.begin(PIN_SDA, PIN_SCL);

    FastLED.addLeds<WS2812B, PIN_LED, GRB>(leds, NUM_LEDS);
    FastLED.setBrightness(BRIGHTNESS);
    FastLED.clear(true);                // all LEDs off

    // ----- Speaker -----
    i2s_chan_config_t txChan = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
    i2s_new_channel(&txChan, &tx, nullptr);
    i2s_std_config_t txCfg = {
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
    i2s_channel_init_std_mode(tx, &txCfg);
    i2s_channel_enable(tx);

    // ----- Microphone -----
    i2s_chan_config_t rxChan = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    i2s_new_channel(&rxChan, nullptr, &rx);
    i2s_pdm_rx_config_t rxCfg = {
        .clk_cfg  = I2S_PDM_RX_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = I2S_PDM_RX_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                   I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .clk = (gpio_num_t)PIN_MIC_CLK,
            .din = (gpio_num_t)PIN_MIC_DATA,
            .invert_flags = {false},
        },
    };
    rxCfg.slot_cfg.slot_mask = I2S_PDM_SLOT_LEFT;
    i2s_channel_init_pdm_rx_mode(rx, &rxCfg);
    i2s_channel_enable(rx);

    Serial.println("Hold any key to record. Release it to play back.");
}

void loop() {
    int key = findKey();
    if (key < 0) {
        delay(10);
        return;
    }

    // A key went down: its LED turns red, and we record
    setLed(key, CRGB::Red);
    for (int i = 0; i < 4; i++) readMic();      // throw away old microphone data
    Serial.println("Recording...");

    int count = 0;
    while (anyKeyPressed() && count + BLOCK <= MAX_SAMPLES) {
        int n = readMic();
        memcpy(&recording[count], block, n * sizeof(int16_t));
        count += n;
    }

    // The key was released (or the recording is full): LED turns blue, play it
    setLed(key, CRGB::Blue);
    Serial.println("Playing...");
    if (count > 0) play(count);

    // Done: LED off
    FastLED.clear(true);
    Serial.println("Done. Hold any key to record again.");
}
