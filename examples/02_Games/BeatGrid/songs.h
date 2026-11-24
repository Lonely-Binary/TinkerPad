// songs.h - the three original songs (no samples, no licences: the board
// synthesises everything itself).
//
// A song is a chord progression, a drum pattern, a bass pattern and some
// melody phrases. The melody is what you PLAY: every note is a pad to hit.
// Hit it and the note sounds; miss it and the music has a hole in it.
//
// A melody bar is 16 characters, one per sixteenth note:
//     '.'      rest
//     '0'-'9'  scale degree 0-9
//     'a'-'e'  scale degree 10-14        (degree 7 = the root, one octave up)
// Difficulty only decides which of the notes become targets:
//     Easy    notes on the beat            (step 0, 4, 8, 12)
//     Normal  notes on the eighth notes    (every even step)
//     Hard    every note
//
// Drum / bass patterns are also 16 characters: 'x' = hit, anything else = rest.
//
// The song plays: 1 bar count-in, then the phrases in `order` (4 bars each),
// then 1 bar to finish.

#pragma once
#include <stdint.h>

struct Song {
    const char *name;
    uint8_t bpm;
    uint8_t root;             // MIDI note of scale degree 0
    bool minor;
    uint8_t prog[4];          // chord root (scale degree) for each bar, repeating
    const char *kick, *snare, *hat, *bass;
    const char *phrase[3][4]; // phrases A, B, C: four bars each
    const char *order;        // e.g. "AABCA"
    uint16_t hue;             // the song's colour on the pads and in the page
};

static const uint8_t SCALE_MINOR[7] = {0, 2, 3, 5, 7, 8, 10};
static const uint8_t SCALE_MAJOR[7] = {0, 2, 4, 5, 7, 9, 11};

static const Song SONGS[3] = {
    {   // A minor, four on the floor
        "Neon Rush", 124, 57, true, {0, 5, 2, 6},
        "x...x...x...x...", "....x.......x...", "..x...x...x...x.", "x..x..x.x..x..x.",
        {
            {"7.9.b9b.9.75.7.9", "9.7.5.7.9b9.7.5.", "9.b.d.b.9.b9b.d.", "a.8.6.8.a.8.6.4."},
            {"b.b.d.b.9.b.9.7.", "9.9.b.9.7.9.7.5.", "b.d.e.d.b.d.b.9.", "d.b.a.8.a.8.6.8."},
            {"7.7.9.7.b.9.7.9.", "5.5.7.5.9.7.5.7.", "9.9.b.9.d.b.9.b.", "a.8.6.8.a.....7."},
        },
        "AABCA", 200,
    },
    {   // C major, bright and slow
        "Pixel Sunrise", 104, 60, false, {0, 4, 5, 3},
        "x.....x...x.....", "....x.......x...", "x.x.x.x.x.x.x.x.", "x.......x.....x.",
        {
            {"7.9.b.9.7...4.7.", "6.8.b.8.6...4.6.", "5.7.9.7.5...7.9.", "5.7.9.7.5.3.5.7."},
            {"9.b.d.b.9.b.d.b.", "8.b.d.b.8.6.8.b.", "9.b.9.7.9.b.d.b.", "7.9.b.9.7.5.7.9."},
            {"7.7.9.7.b.9.7.4.", "6.6.8.6.b.8.6.4.", "5.5.7.5.9.7.5.7.", "7.5.3.5.7.....0."},
        },
        "AABCA", 140,
    },
    {   // D minor, fast and syncopated
        "Night Drive", 140, 62, true, {0, 3, 6, 4},
        "x..x..x.x..x..x.", "....x.......x..x", "x.x.x.x.x.x.x.xx", "x.xx.x.xx.xx.x.x",
        {
            {"7..9.7b.9...7.5.", "5..7.59.7...5.3.", "6..8.6a.8...6.4.", "4..6.48.6...4.2."},
            {"9..b.9d.b...9.7.", "7..9.7b.9...7.5.", "8..a.8c.a...8.6.", "6..8.6a.8...6.4."},
            {"7.7.9.7.9.b.9.7.", "5.5.7.5.7.9.7.5.", "6.6.8.6.8.a.8.6.", "4.4.6.4.6.8.6.4."},
        },
        "AABCA", 280,
    },
};

static const uint8_t SONG_COUNT = 3;
