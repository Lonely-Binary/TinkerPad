#include "layout.h"

#include "config.h"

namespace Layout {

// The bottom row is the control row. Every gesture here is at most two keys
// (MIC + one more), which is all a diode-less matrix can resolve reliably.
Role roleForKey(uint8_t bit) {
    switch (posForKey(bit)) {
        case 12: return ROLE_MIC;
        case 13: return ROLE_REPLAY;
        case 14: return ROLE_DELETE;
        case 15: return ROLE_ALL;
        default: return ROLE_PERSON;
    }
}

uint8_t personForKey(uint8_t bit) {
    uint8_t pos = posForKey(bit);
    return pos < kPeople ? pos : 0xFF;
}

void print() {
    static const char *kCtl[4] = {"MIC", "REPLAY", "DELETE", "ALL"};
    Serial.println("  layout (fixed): what the user sees, and the key/LED under it");
    for (uint8_t r = 0; r < 4; r++) {
        Serial.print("      ");
        for (uint8_t c = 0; c < 4; c++) {
            uint8_t pos = r * 4 + c;
            char label[10];
            if (pos < kPeople) {
                snprintf(label, sizeof(label), "P%u", pos + 1);
            } else {
                snprintf(label, sizeof(label), "%s", kCtl[pos - 12]);
            }
            Serial.printf("%-7s[k%-2u led%-2u]  ", label, keyForPos(pos),
                          ledForPos(pos));
        }
        Serial.println();
    }
}

}  // namespace Layout
