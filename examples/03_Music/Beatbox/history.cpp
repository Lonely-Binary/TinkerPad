#include "history.h"

#include "config.h"
#include "looper.h"
#include "slots.h"

namespace History {

enum Kind : uint8_t { LAYER, SAMPLE };

struct Action {
    uint8_t kind;
    uint8_t slot;  // SAMPLE only
};

// Deep enough that undo never runs out before the loop's own limits do: layers
// cap at kMaxLayers and there are only kSlotCount pads to record onto.
static const uint8_t kDepth = kMaxLayers + kSlotCount;

static Action g_stack[kDepth];
static uint8_t g_count = 0;

static void push(Action a) {
    if (g_count < kDepth) {
        g_stack[g_count++] = a;
        return;
    }
    // Full: drop the oldest. Losing the ability to undo something from a long
    // time ago is much better than refusing to record something now.
    for (uint8_t i = 1; i < kDepth; i++) {
        g_stack[i - 1] = g_stack[i];
    }
    g_stack[kDepth - 1] = a;
}

void begin() { g_count = 0; }

void pushLayer() { push({LAYER, 0}); }

void pushSample(uint8_t slot) { push({SAMPLE, slot}); }

bool undo() {
    if (g_count == 0) return false;

    Action a = g_stack[--g_count];
    switch (a.kind) {
        case LAYER:
            Looper::clearLastLayer();
            Serial.printf("[undo] layer removed — %u left\n", Looper::layerCount());
            break;
        case SAMPLE:
            Slots::revertToFactory(a.slot);
            Serial.printf("[undo] S%u back to its factory sound (%s)\n", a.slot + 1,
                          Slots::name(a.slot));
            break;
    }
    return true;
}

void clear() { g_count = 0; }

uint8_t depth() { return g_count; }

void print() {
    Serial.printf("  undo stack: %u deep%s", g_count, g_count ? "  " : "\n");
    for (uint8_t i = 0; i < g_count; i++) {
        if (g_stack[i].kind == LAYER) {
            Serial.print("layer ");
        } else {
            Serial.printf("S%u ", g_stack[i].slot + 1);
        }
    }
    if (g_count) Serial.println("   (rightmost is undone first)");
}

}  // namespace History
