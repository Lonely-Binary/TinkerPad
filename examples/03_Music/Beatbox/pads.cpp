#include "pads.h"

#include <Wire.h>

bool Pads::begin(uint8_t addr, int sdaPin, int sclPin, uint32_t clockHz) {
    _addr = addr;

    Wire.begin(sdaPin, sclPin);

    // The PCF8574 is a 100 kHz part. Pushing the bus faster to shorten the
    // scan cycle is out of spec, so the scan period is what it is — measure
    // it rather than assume it can be tuned away.
    Wire.setClock(clockHz);

    // Default is 50 ms per stalled transaction, which is eight seconds of
    // dead serial console for every 2000-scan benchmark on an unplugged board.
    Wire.setTimeOut(5);

    return probe();
}

bool Pads::probe() {
    _lastProbeMs = millis();
    Wire.beginTransmission(_addr);
    _present = (Wire.endTransmission() == 0);
    if (_present) {
        writePort(0xFF);
    }
    return _present;
}

void Pads::writePort(uint8_t value) {
    Wire.beginTransmission(_addr);
    Wire.write(value);
    Wire.endTransmission();
}

uint8_t Pads::readPort() {
    Wire.requestFrom(_addr, static_cast<uint8_t>(1));
    if (Wire.available()) {
        return Wire.read();
    }
    return 0xFF;
}

uint16_t Pads::scan() {
    if (!_present) {
        // Re-probe now and then so seating the dev board in the carrier is
        // picked up without a reflash, but cheaply enough that the idle case
        // stays free.
        if (millis() - _lastProbeMs >= 500) {
            probe();
        }
        if (!_present) {
            _lastScanMicros = 0;
            return 0;
        }
    }

    uint32_t t0 = micros();
    uint16_t mask = 0;

    for (uint8_t row = 0; row < ROWS; row++) {
        // The PCF8574 is quasi-bidirectional: writing 0 drives a strong low,
        // writing 1 leaves a weak pull-up that an input can drag down.
        // So "select this row" means write 0 on its bit and 1 everywhere else.
        writePort((~(1u << row) & 0x0F) | 0xF0);
        delayMicroseconds(50);  // let the weak pull-ups recover on the columns

        uint8_t port = readPort();
        for (uint8_t col = 0; col < COLS; col++) {
            if ((port & (1u << (col + 4))) == 0) {
                mask |= (uint16_t)1 << (row * COLS + col);
            }
        }
    }

    writePort(0xFF);
    _lastScanMicros = micros() - t0;
    return mask;
}

bool Pads::hasGhostRisk(uint16_t mask) {
    uint8_t rowCols[ROWS];
    for (uint8_t r = 0; r < ROWS; r++) {
        rowCols[r] = (mask >> (r * COLS)) & 0x0F;
    }

    // Ambiguity needs two rows that both have two or more columns down, and
    // that share at least two of them — that is the rectangle a phantom key
    // completes. Two keys can never be ambiguous; three sometimes are.
    for (uint8_t a = 0; a < ROWS; a++) {
        if (__builtin_popcount(rowCols[a]) < 2) {
            continue;
        }
        for (uint8_t b = a + 1; b < ROWS; b++) {
            if (__builtin_popcount(rowCols[a] & rowCols[b]) >= 2) {
                return true;
            }
        }
    }
    return false;
}
