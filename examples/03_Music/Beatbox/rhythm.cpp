#include "rhythm.h"

#include "looper.h"

namespace Rhythm {

// Slot numbers, named so the patterns below read as music rather than as indices.
enum : uint8_t {
    KICK = 0,
    SNARE = 1,
    HAT = 2,
    OPENHAT = 3,
    CLAP = 4,
    TOM_LO = 5,
    TOM_MID = 6,
    TOM_HI = 7,
    COWBELL = 8,
    CLAVE = 9,
    CRASH = 10,
    BASS = 11,
};

// Beat n is step n*4.

// A bare metronome: one click per beat, accented on the first. Not musical, but
// it is the only setting that leaves the whole kit free while still giving you
// something to play against — which is what you want when the point is to record
// a part rather than to jam.
static const Hit kClick[] = {
    {0, Looper::kIdClickAccent}, {4, Looper::kIdClick},
    {8, Looper::kIdClick},       {12, Looper::kIdClick},
};

// Straight backbeat. Kick on 1 and 3, snare on 2 and 4, eighth-note hats. The
// first pattern anybody recognises, and the easiest to play over.
static const Hit kBackbeat[] = {
    {0, KICK},  {0, HAT},   {2, HAT},   {4, SNARE}, {4, HAT},   {6, HAT},
    {8, KICK},  {8, HAT},   {10, HAT},  {12, SNARE}, {12, HAT}, {14, HAT},
};

// Four on the floor: kick every beat, claps on the backbeat, open hats on the
// off-beats. Relentless, and very easy to sing over because the pulse is never
// in question.
static const Hit kFourFloor[] = {
    {0, KICK},    {2, OPENHAT}, {4, KICK},    {4, CLAP},
    {6, OPENHAT}, {8, KICK},    {10, OPENHAT}, {12, KICK},
    {12, CLAP},   {14, OPENHAT},
};

// Boom-bap. The kick is deliberately off the grid on 3-and, the snare lands late
// on 4, and the hats are sparse — the swing comes from what is missing.
static const Hit kBoomBap[] = {
    {0, KICK},   {0, HAT},   {4, SNARE}, {4, HAT},  {7, KICK},
    {8, HAT},    {10, KICK}, {12, SNARE}, {12, HAT}, {14, HAT},
};

// Latin-ish, built on the clave rather than on a backbeat, with toms instead of
// a snare. Included because it uses a completely different half of the kit, so
// the pads that are useful to play over are different too.
static const Hit kClaveGroove[] = {
    {0, KICK},   {0, CLAVE},  {3, CLAVE}, {6, CLAVE},  {6, TOM_LO},
    {8, COWBELL}, {10, CLAVE}, {12, CLAVE}, {12, TOM_MID}, {14, KICK},
};

struct Pattern {
    const char *name;
    const Hit *hits;
    uint16_t count;
    uint32_t colour;
};

static const Pattern kPatterns[] = {
    {"off", nullptr, 0, 0x000000},
    {"click", kClick, sizeof(kClick) / sizeof(Hit), 0xB0B0FF},
    {"backbeat", kBackbeat, sizeof(kBackbeat) / sizeof(Hit), 0x00C060},
    {"four-floor", kFourFloor, sizeof(kFourFloor) / sizeof(Hit), 0xFFA000},
    {"boom-bap", kBoomBap, sizeof(kBoomBap) / sizeof(Hit), 0xFF3070},
    {"clave", kClaveGroove, sizeof(kClaveGroove) / sizeof(Hit), 0x9040FF},
};

uint8_t count() { return sizeof(kPatterns) / sizeof(Pattern); }

const char *name(uint8_t index) {
    return index < count() ? kPatterns[index].name : "?";
}

const Hit *hits(uint8_t index, uint16_t *hitCount) {
    if (index >= count()) {
        if (hitCount) *hitCount = 0;
        return nullptr;
    }
    if (hitCount) *hitCount = kPatterns[index].count;
    return kPatterns[index].hits;
}

uint32_t colour(uint8_t index) {
    return index < count() ? kPatterns[index].colour : 0;
}

}  // namespace Rhythm
