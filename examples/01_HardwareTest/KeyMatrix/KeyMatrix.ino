// Key Matrix - shows which of the 16 keys are pressed
// Board: ESP32-S3 N16R8 (16 MB flash, 8 MB PSRAM)
// Setup guide: see ../README.md
//
// The keys are wired as a 4 x 4 grid (4 rows, 4 columns). A chip called
// PCF8574 (I2C address 0x20) has 8 pins:
//   pins 0-3 -> rows    (we pull one row low at a time)
//   pins 4-7 -> columns (we read them: low means a key is pressed)
//
// Keys are numbered 0-15, row by row.

#include <Wire.h>

#define PIN_SDA       9
#define PIN_SCL       8
#define PCF8574_ADDR  0x20

// Write one byte to the chip
void writePins(uint8_t value) {
    Wire.beginTransmission(PCF8574_ADDR);
    Wire.write(value);
    Wire.endTransmission();
}

// Read one byte from the chip
uint8_t readPins() {
    Wire.requestFrom(PCF8574_ADDR, 1);
    return Wire.read();
}

void setup() {
    Serial.begin(115200);
    delay(600);
    Wire.begin(PIN_SDA, PIN_SCL);
    Serial.println("Press some keys...");
}

void loop() {
    static uint16_t lastKeys = 0;
    uint16_t keys = 0;                      // bit N is 1 when key N is pressed

    for (int row = 0; row < 4; row++) {
        // Pull this row low, leave everything else high
        writePins(0xF0 | (0x0F & ~(1 << row)));
        delay(1);

        uint8_t pins = readPins();
        for (int col = 0; col < 4; col++) {
            if ((pins & (1 << (col + 4))) == 0) {       // low = pressed
                keys |= 1 << (row * 4 + col);
            }
        }
    }

    // Only print when something changed (and not when all keys are released)
    if (keys != lastKeys) {
        lastKeys = keys;
        if (keys == 0) return;
        Serial.print("Pressed keys:");
        for (int k = 0; k < 16; k++) {
            if (keys & (1 << k)) {
                Serial.printf(" %d", k);
            }
        }
        Serial.println();
    }

    delay(10);
}
