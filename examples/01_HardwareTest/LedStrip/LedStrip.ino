// LED Strip - the simplest possible strip test
// Board: ESP32-S3 N16R8 (16 MB flash, 8 MB PSRAM)
// Setup guide: see ../README.md
//
// 16 x WS2812B LEDs on GPIO38, one under each key.
//
// What it does (forever):
//   1. All red for 1 second
//   2. All green for 1 second
//   3. All blue for 1 second
//   4. A white LED sweeps quickly from LED 0 to LED 15
//
// Note: keep BRIGHTNESS low while powered from USB. Too much current makes
// the USB port drop out.

#include <FastLED.h>

#define LED_PIN     38    // data pin of the LED strip
#define NUM_LEDS    16    // number of LEDs
#define BRIGHTNESS  40    // 0-255

CRGB leds[NUM_LEDS];      // the colour of every LED is stored in this array

void setup() {
    FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, NUM_LEDS);
    FastLED.setBrightness(BRIGHTNESS);
}

// Set every LED to one colour and hold it for a while
void showAll(CRGB colour, int ms) {
    fill_solid(leds, NUM_LEDS, colour);
    FastLED.show();
    delay(ms);
}

void loop() {
    // Red, green, blue: 1 second each
    showAll(CRGB::Red,   1000);
    showAll(CRGB::Green, 1000);
    showAll(CRGB::Blue,  1000);

    // White sweep from LED 0 to LED 15, 30 ms per LED
    for (int i = 0; i < NUM_LEDS; i++) {
        fill_solid(leds, NUM_LEDS, CRGB::Black);   // turn everything off first
        leds[i] = CRGB::White;                     // light only LED i
        FastLED.show();
        delay(100);
    }

    // Turn off briefly, then start over
    showAll(CRGB::Black, 300);
}
