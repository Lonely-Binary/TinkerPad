// PSRAM Test - writes and reads back the external RAM in 256 KB steps
// Board: ESP32-S3 N16R8, PSRAM = OPI PSRAM
//
// Prints how far it got, flushing after every step, so a board whose PSRAM is
// faulty shows exactly where it dies instead of just rebooting.

#include <esp_heap_caps.h>

void setup() {
    Serial.begin(115200);
    delay(1500);
    Serial.printf("PSRAM size %u KB, free %u KB\n", (unsigned)(ESP.getPsramSize() / 1024),
                  (unsigned)(ESP.getFreePsram() / 1024));
    Serial.flush();

    const size_t step = 256 * 1024;
    const size_t total = 6 * 1024 * 1024;
    uint32_t *mem = (uint32_t *)heap_caps_malloc(total, MALLOC_CAP_SPIRAM);
    if (!mem) {
        Serial.println("could not allocate 6 MB");
        return;
    }
    for (size_t off = 0; off < total; off += step) {
        uint32_t *p = mem + off / 4;
        for (size_t i = 0; i < step / 4; i++) p[i] = (uint32_t)(off + i * 4) ^ 0xA5A5A5A5u;
        size_t bad = 0;
        for (size_t i = 0; i < step / 4; i++) {
            if (p[i] != ((uint32_t)(off + i * 4) ^ 0xA5A5A5A5u)) bad++;
        }
        Serial.printf("  %5u KB ok%s\n", (unsigned)((off + step) / 1024), bad ? "  BAD WORDS" : "");
        Serial.flush();
    }
    Serial.println("PSRAM test PASSED");
}

void loop() { delay(1000); }
