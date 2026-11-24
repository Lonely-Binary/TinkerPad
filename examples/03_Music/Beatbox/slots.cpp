#include "slots.h"

#include <esp_heap_caps.h>

#include "config.h"

namespace Slots {

struct Slot {
    const int16_t *factory = nullptr;
    uint32_t factoryFrames = 0;
    const char *factoryName = "-";

    int16_t *user = nullptr;
    uint32_t userFrames = 0;

    bool useUser = false;
};

static Slot g_slots[kSlotCount];
static uint32_t g_userCapacity = 0;
static uint32_t g_bytes = 0;

bool begin(uint32_t sampleRate, float maxSeconds) {
    g_userCapacity = (uint32_t)(sampleRate * maxSeconds);

    for (uint8_t i = 0; i < kSlotCount; i++) {
        // MALLOC_CAP_SPIRAM, not ps_malloc, so the failure is explicit: with
        // OPI PSRAM misconfigured in the board options this returns null rather
        // than quietly landing 4 MB of buffers in 512 KB of internal RAM.
        g_slots[i].user = (int16_t *)heap_caps_malloc(
            g_userCapacity * sizeof(int16_t), MALLOC_CAP_SPIRAM);
        if (!g_slots[i].user) {
            Serial.printf(
                "  slot %u: PSRAM allocation of %u KB FAILED — is OPI PSRAM "
                "enabled in the board options?\n",
                i, (unsigned)(g_userCapacity * sizeof(int16_t) / 1024));
            return false;
        }
        g_bytes += g_userCapacity * sizeof(int16_t);
    }
    return true;
}

void setFactory(uint8_t slot, const int16_t *data, uint32_t frames,
                const char *name) {
    if (slot >= kSlotCount) return;
    g_slots[slot].factory = data;
    g_slots[slot].factoryFrames = frames;
    g_slots[slot].factoryName = name;
}

const int16_t *data(uint8_t slot) {
    if (slot >= kSlotCount) return nullptr;
    const Slot &s = g_slots[slot];
    return s.useUser ? s.user : s.factory;
}

uint32_t frames(uint8_t slot) {
    if (slot >= kSlotCount) return 0;
    const Slot &s = g_slots[slot];
    return s.useUser ? s.userFrames : s.factoryFrames;
}

bool isUser(uint8_t slot) {
    return slot < kSlotCount && g_slots[slot].useUser;
}

const char *name(uint8_t slot) {
    if (slot >= kSlotCount) return "-";
    return g_slots[slot].useUser ? "recorded" : g_slots[slot].factoryName;
}

int16_t *userBuffer(uint8_t slot) {
    return slot < kSlotCount ? g_slots[slot].user : nullptr;
}

uint32_t userCapacity() { return g_userCapacity; }

void commitUser(uint8_t slot, uint32_t frameCount) {
    if (slot >= kSlotCount) return;
    if (frameCount == 0) return;  // nothing captured: leave the old sound alone
    g_slots[slot].userFrames = min(frameCount, g_userCapacity);
    g_slots[slot].useUser = true;
}

void revertToFactory(uint8_t slot) {
    if (slot >= kSlotCount) return;
    g_slots[slot].useUser = false;
    g_slots[slot].userFrames = 0;
}

void revertAll() {
    for (uint8_t i = 0; i < kSlotCount; i++) {
        revertToFactory(i);
    }
}

uint32_t bytesUsed() { return g_bytes; }

void print() {
    for (uint8_t i = 0; i < kSlotCount; i++) {
        uint32_t f = frames(i);
        Serial.printf("  S%-2u %-9s %6.0f ms%s\n", i + 1, name(i),
                      1000.0f * (float)f / (float)kSampleRate,
                      isUser(i) ? "  (recorded)" : "");
    }
}

}  // namespace Slots
