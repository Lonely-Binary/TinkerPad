// lesson.h - reads a jianpu song and works out where to play each note
//
// The keypad is a ukulele fretboard, as you see it when you look down at the
// ukulele while playing:
//
//   row 0 (top)     G string:  open  fret1  fret2  fret3
//   row 1           C string:  open  fret1  fret2  fret3
//   row 2           E string:  open  fret1  fret2  fret3
//   row 3 (bottom)  A string:  open  fret1  fret2  fret3
//
// Notes are numbered like MIDI: 60 = C4 (middle C), 61 = C#4 ... 72 = C5.

#pragma once
#include <string.h>
#include <stdlib.h>
#include "songs.h"

#define MAX_NOTES    160
#define MAX_PHRASES  20

// The note of each open string (G4 C4 E4 A4 - standard "re-entrant" tuning)
const int OPEN_STRING[4] = {67, 60, 64, 69};
const char *STRING_NAMES = "GCEA";

struct Note {
    int   midi;         // -1 = rest
    float beats;
    int   pad;          // where to press it (-1 for a rest)
};

Note notes[MAX_NOTES];
int  noteCount = 0;
int  phraseStart[MAX_PHRASES + 1];  // phrase p = notes phraseStart[p] .. phraseStart[p+1]-1
int  phraseCount = 0;

// The note a pad plays
int noteOfPad(int pad) {
    return OPEN_STRING[pad / 4] + pad % 4;
}

// Where to play a note: the string where it needs the lowest fret
// (open strings first - that is how beginners play). -1 if it does not fit.
int padForNote(int midi) {
    int bestPad = -1, bestFret = 99;
    for (int string = 0; string < 4; string++) {
        int fret = midi - OPEN_STRING[string];
        if (fret >= 0 && fret <= 3 && fret < bestFret) {
            bestFret = fret;
            bestPad = string * 4 + fret;
        }
    }
    return bestPad;
}

// Turn the jianpu text into notes and phrases. Returns false if a note does
// not fit on the fretboard (then `problem` says which one).
bool loadSong(const Song &song, char *problem) {
    const int SCALE[7] = {0, 2, 4, 5, 7, 9, 11};   // do re mi fa sol la ti
    int doNote = song.key[0] == 'F' ? 65 : 60;      // which note "1" is
    noteCount = 0;
    phraseCount = 0;
    phraseStart[0] = 0;
    int bars = 0;

    const char *p = song.notes;
    char token[12];
    while (*p) {
        while (*p == ' ') p++;                      // read one word
        int n = 0;
        while (*p && *p != ' ' && n < 11) token[n++] = *p++;
        token[n] = 0;
        if (n == 0) break;

        if (token[0] == '|') {                      // bar line
            bars++;
            if (bars % song.barsPerPhrase == 0 && phraseCount < MAX_PHRASES && noteCount > phraseStart[phraseCount]) {
                phraseStart[++phraseCount] = noteCount;
            }
            continue;
        }
        if (token[0] == '-') {                      // hold one more beat
            if (noteCount > 0) notes[noteCount - 1].beats += 1;
            continue;
        }

        int i = 0, sharp = 0;
        if (token[i] == '#') { sharp = 1; i++; }
        if (token[i] == 'b') { sharp = -1; i++; }
        int digit = token[i++] - '0';
        if (digit < 0 || digit > 7) continue;

        int midi = digit == 0 ? -1 : doNote + SCALE[digit - 1] + sharp;
        float beats = 1;
        for (; token[i]; i++) {
            if (token[i] == ',') midi -= 12;       // dot below
            if (token[i] == '\'') midi += 12;      // dot above
            if (token[i] == '_') beats = 0.5;
            if (token[i] == '.') beats *= 1.5;
        }
        if (digit == 0) midi = -1;

        int pad = midi < 0 ? -1 : padForNote(midi);
        if (midi >= 0 && pad < 0) {
            strcpy(problem, token);
            return false;
        }
        if (noteCount < MAX_NOTES) notes[noteCount++] = {midi, beats, pad};
    }
    if (noteCount > phraseStart[phraseCount]) phraseStart[++phraseCount] = noteCount;
    return true;
}

// The next note at or after `i` that is not a rest (or `end` if none)
int nextPlayed(int i, int end) {
    while (i < end && notes[i].midi < 0) i++;
    return i;
}
