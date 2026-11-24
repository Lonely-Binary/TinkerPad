#include "layout.h"

#include "config.h"

namespace Layout {

// The bottom physical row is the transport. Putting
// it there is not arbitrary: the matrix has no anti-ghosting diodes, so only
// two-key combinations are guaranteed resolvable. Every gesture the interaction
// model needs — hold a pad, press REC — is exactly two keys.
Role roleForKey(uint8_t bit) {
    switch (posForKey(bit)) {
        case 12: return ROLE_MIC;
        case 13: return ROLE_STOP;
        case 14: return ROLE_BEAT;
        case 15: return ROLE_UNDO;
        default: return ROLE_SLOT;
    }
}

uint8_t slotForKey(uint8_t bit) {
    uint8_t pos = posForKey(bit);
    return pos < kSlotCount ? pos : 0xFF;
}

uint8_t posForSlot(uint8_t slot) { return slot; }

const char *roleName(Role r) {
    switch (r) {
        case ROLE_MIC: return "MIC";
        case ROLE_STOP: return "STOP";
        case ROLE_BEAT: return "BEAT";
        case ROLE_UNDO: return "UNDO";
        default: return "slot";
    }
}

void print() {
    Serial.println("  layout (fixed): what the user sees, and the key/LED under it");
    for (uint8_t r = 0; r < 4; r++) {
        Serial.print("      ");
        for (uint8_t col = 0; col < 4; col++) {
            uint8_t pos = r * 4 + col;
            uint8_t bit = keyForPos(pos);
            char label[8];
            if (pos < kSlotCount) {
                snprintf(label, sizeof(label), "S%u", pos + 1);
            } else {
                snprintf(label, sizeof(label), "%s", roleName(roleForKey(bit)));
            }
            Serial.printf("%-5s[k%-2u led%-2u]  ", label, bit, ledForKey(bit));
        }
        Serial.println();
    }
}

}  // namespace Layout
