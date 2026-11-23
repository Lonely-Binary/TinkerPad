// I2C Scan - finds every device on the I2C bus
// Board: ESP32-S3 N16R8 (16 MB flash, 8 MB PSRAM)
// Setup guide: see ../README.md
//
// I2C is a two-wire bus (SDA = data, SCL = clock). Every chip on it has its
// own address. This sketch tries all addresses and prints the ones that answer.
//
// On this board you should see:
//   0x20  PCF8574  (reads the 16 keys)
//   0x75  IP5306   (battery chip - it may be asleep and not answer)

#include <Wire.h>

#define PIN_SDA  9
#define PIN_SCL  8

void setup() {
    Serial.begin(115200);
    delay(600);
    Wire.begin(PIN_SDA, PIN_SCL);
}

void loop() {
    Serial.println("Scanning...");

    int found = 0;
    for (int addr = 1; addr < 127; addr++) {
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() == 0) {      // 0 means the device answered
            Serial.printf("  found device at 0x%02X\n", addr);
            found++;
        }
    }

    if (found == 0) {
        Serial.println("  nothing found");
    }

    delay(3000);
}
