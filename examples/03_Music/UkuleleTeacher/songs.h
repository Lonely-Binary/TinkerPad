// songs.h - the songs UkuleleTeacher can teach, written in jianpu (numbered musical notation)
//
// How to write a song (one line of text):
//   1 2 3 4 5 6 7   do re mi fa sol la ti     (one beat each)
//   5,              a dot below: one octave lower   (5, = low sol)
//   1'              a dot above: one octave higher  (1' = high do)
//   #4              sharp (half a step higher)
//   -               hold the note before it for one more beat
//   3_              half a beat (an eighth note)
//   3.              one and a half beats (a dotted note)
//   0               a rest (one beat of silence)
//   |               bar line - the teacher teaches a few bars at a time
//
// "key" says which note "1" is. The ukulele (first position, frets 0-3) can
// play from C4 to C5, so every song must stay inside that.
// All songs here are traditional or public-domain melodies.

#pragma once

struct Song {
    const char *name;
    const char *key;            // "C" or "F" (which note "1" is)
    int         bpm;            // beats per minute
    int         barsPerPhrase;  // how many bars to teach at a time
    const char *notes;
};

const Song SONGS[] = {
    {"Twinkle Twinkle Little Star", "C", 100, 2,
     "1 1 5 5 | 6 6 5 - | 4 4 3 3 | 2 2 1 - | 5 5 4 4 | 3 3 2 - | 5 5 4 4 | 3 3 2 - | "
     "1 1 5 5 | 6 6 5 - | 4 4 3 3 | 2 2 1 - |"},

    {"Ode to Joy", "C", 110, 2,
     "3 3 4 5 | 5 4 3 2 | 1 1 2 3 | 3. 2_ 2 - | 3 3 4 5 | 5 4 3 2 | 1 1 2 3 | 2. 1_ 1 - |"},

    {"Mary Had a Little Lamb", "C", 110, 2,
     "3 2 1 2 | 3 3 3 - | 2 2 2 - | 3 5 5 - | 3 2 1 2 | 3 3 3 3 | 2 2 3 2 | 1 - - - |"},

    {"London Bridge", "C", 110, 2,
     "5. 6_ 5 4 | 3 4 5 - | 2 3 4 - | 3 4 5 - | 5. 6_ 5 4 | 3 4 5 - | 2 - 5 - | 3 1 - - |"},

    {"The Little Painter", "C", 120, 2,
     "5 3 5 3 | 5 3 1 - | 2 4 3 2 | 5 - - - | 5 3 5 3 | 5 3 1 - | 2 4 3 2 | 1 - - - | "
     "2 2 4 4 | 3 1 5 - | 2 4 3 2 | 5 - - - | 5 3 5 3 | 5 3 1 - | 2 4 3 2 | 1 - - - |"},

    {"Happy New Year", "F", 110, 2,
     "1 1 1 5, | 3 3 3 1 | 1 3 5 5 | 4 3 2 - | 2 3 4 4 | 3 2 3 1 | 1 3 2 5, | 7, 2 1 - |"},

    {"Happy Birthday", "F", 100, 1,
     "5,_ 5,_ 6, 5, 1 7, - | 5,_ 5,_ 6, 5, 2 1 - | 5,_ 5,_ 5 3 1 7, 6, | 4_ 4_ 3 1 2 1 - |"},

    {"Jingle Bells", "C", 120, 2,
     "3 3 3 - | 3 3 3 - | 3 5 1 2 | 3 - - - | 4 4 4 4 | 4 3 3 3 | 3 2 2 3 | 2 - 5 - | "
     "3 3 3 - | 3 3 3 - | 3 5 1 2 | 3 - - - | 4 4 4 4 | 4 3 3 3 | 5 5 4 2 | 1 - - - |"},
};

const int SONG_COUNT = sizeof(SONGS) / sizeof(SONGS[0]);
