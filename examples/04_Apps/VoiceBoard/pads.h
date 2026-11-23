#pragma once

#include <Arduino.h>

// 4x4 key matrix behind a PCF8574 on the I2C bus.
//
// Unlike the earlier bring-up code for this board, scan() returns EVERY key
// that is down, not just the first one found. A loop station has to see
// "hold a pad AND press REC" as one gesture, so first-hit scanning is not
// good enough here.

class Pads {
public:
    static const uint8_t ROWS = 4;
    static const uint8_t COLS = 4;
    static const uint8_t COUNT = ROWS * COLS;

    bool begin(uint8_t addr, int sdaPin, int sclPin, uint32_t clockHz = 100000);

    // Bit n (n = row * 4 + col) is 1 when that key is down.
    uint16_t scan();

    // The matrix has no anti-ghosting diodes, so some combinations are
    // genuinely ambiguous: the same electrical state can be produced by
    // different sets of keys. True when the last scan() was one of those.
    static bool hasGhostRisk(uint16_t mask);

    // Microseconds the last scan() took, measured around the I2C traffic.
    uint32_t lastScanMicros() const { return _lastScanMicros; }

    // False when the expander is not answering. scan() returns 0 immediately
    // in that state instead of eating a bus timeout per row — otherwise a
    // board that is not plugged into the carrier makes every test appear to
    // hang for minutes with the serial console unresponsive.
    bool present() const { return _present; }

    static uint8_t bitToRow(uint8_t bit) { return bit / COLS; }
    static uint8_t bitToCol(uint8_t bit) { return bit % COLS; }

private:
    void writePort(uint8_t value);
    uint8_t readPort();

    bool probe();

    uint8_t _addr = 0x20;
    uint32_t _lastScanMicros = 0;
    bool _present = false;
    uint32_t _lastProbeMs = 0;
};
