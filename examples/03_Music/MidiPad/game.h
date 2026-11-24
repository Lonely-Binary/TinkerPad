// game.h - turns a song from songs.h into a list of notes to play
//
// Every note gets a start time, a length, a pitch, and the pad you press
// for it. Low notes go on the bottom row, high notes higher up:
//
//   Easy   -> the 4 bottom pads
//   Normal -> the 8 bottom pads
//   Hard   -> all 16 pads

#pragma once
#include <math.h>
#include "songs.h"

#define MAX_NOTES    200
#define MAX_SECONDS  45            // long songs stop here (a game should be short)

// The speaker sounds best between these pitches; notes outside are moved by octaves
#define LOWEST_HZ    220
#define HIGHEST_HZ   1100

enum Result : uint8_t { WAITING, PERFECT, GOOD, MISSED };

struct GameNote {
    uint32_t start;     // ms from the start of the song
    uint32_t length;    // ms
    float    hz;
    int8_t   pad;
    Result   result;
    bool     wrongFirst;            // learn mode: a wrong pad was pressed first
};

GameNote gameNotes[MAX_NOTES];
int      noteCount = 0;
uint32_t songEnd = 0;               // ms
float    beatMs = 500;              // one quarter note, ms
int      beatsPerBar = 4;

// Pads from lowest to highest: bottom row left to right, then the row above...
const int8_t PADS_LOW_TO_HIGH[16] = {12, 13, 14, 15, 8, 9, 10, 11, 4, 5, 6, 7, 0, 1, 2, 3};

void loadSong(int number, int difficulty) {
    const Song &song = SONGS[number];
    float wholeNote = 60000.0f * 4 / song.tempo;
    beatMs = wholeNote / 4;
    beatsPerBar = song.beatsPerBar;

    // 1. Read the note/duration pairs into times
    noteCount = 0;
    float t = 0;
    for (int i = 0; i + 1 < song.length && noteCount < MAX_NOTES; i += 2) {
        int pitch = song.melody[i];
        int divider = song.melody[i + 1];
        float ms = wholeNote / abs(divider);
        if (divider < 0) ms *= 1.5f;                // dotted note

        if (pitch > 0) {
            float hz = pitch;
            while (hz > HIGHEST_HZ) hz /= 2;
            while (hz < LOWEST_HZ) hz *= 2;
            gameNotes[noteCount++] = {(uint32_t)t, (uint32_t)ms, hz, 0, WAITING, false};
        }
        t += ms;
        if (t > MAX_SECONDS * 1000) break;
    }
    songEnd = (uint32_t)t + 600;

    // 2. Give each different pitch a pad: rank the pitches, spread them over the pads
    float pitches[MAX_NOTES];
    int unique = 0;
    for (int i = 0; i < noteCount; i++) {
        bool seen = false;
        for (int j = 0; j < unique; j++) if (fabsf(pitches[j] - gameNotes[i].hz) < 1) seen = true;
        if (!seen) pitches[unique++] = gameNotes[i].hz;
    }
    for (int a = 0; a < unique; a++)                // sort low to high
        for (int b = a + 1; b < unique; b++)
            if (pitches[b] < pitches[a]) { float x = pitches[a]; pitches[a] = pitches[b]; pitches[b] = x; }

    int pads = difficulty == 0 ? 4 : difficulty == 1 ? 8 : 16;
    if (unique < pads) pads = unique;
    for (int i = 0; i < noteCount; i++) {
        int rank = 0;
        while (rank < unique && fabsf(pitches[rank] - gameNotes[i].hz) >= 1) rank++;
        int zone = unique > 1 ? rank * pads / unique : 0;
        gameNotes[i].pad = PADS_LOW_TO_HIGH[zone];
    }
}
