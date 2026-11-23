#pragma once

#include <Arduino.h>
#include <FS.h>

// Persistent message storage on the FAT partition of the 16 MB flash.
//
// One file per message under /vb. A message is identified by a random 32-bit id, not
// by a counter: several boards create messages independently and later copy them to
// each other, so an id has to be unique without anyone coordinating. The order
// messages are heard in comes from a separate Lamport-style `order` number — each new
// message is stamped one higher than the highest the board has ever seen, so a
// message made after hearing another is always later, with no clock involved.
//
// Deleting leaves a tombstone (just the id). Without it, a board that was switched off
// during the delete would hand the message back to everyone the next time it joined.
//
// All of this is shared between the UI loop and the sync task, so every call takes a
// lock.

namespace Store {

struct Msg {
    uint32_t id;     // unique; 0 means "none"
    uint32_t order;  // Lamport stamp, ascending = older to newer
    uint8_t to;      // 0..11, or kToAll
    uint16_t heard;  // bit n set: person n has played it
    uint32_t bytes;  // on flash
};

// One line of a board's state, as exchanged during sync. 12 bytes on the wire.
struct __attribute__((packed)) Record {
    uint32_t id;
    uint32_t order;
    uint16_t heard;
    uint8_t to;
    uint8_t deleted;  // 1: tombstone, the other fields are not meaningful
};

bool begin();

uint16_t count();
const Msg *at(uint16_t i);  // oldest first
const Msg *find(uint32_t id);

// Unread for a person: addressed to them or to everyone, and their bit unset.
uint16_t unreadFor(uint8_t person);
uint32_t oldestUnreadFor(uint8_t person);  // 0 when none
uint32_t newestForAll();                   // 0 when none

// The latest message addressed to this person (directly or to everyone) that they
// have already heard. Lets a second tap on a key replay what they just listened to.
uint32_t newestHeardBy(uint8_t person);  // 0 when none

// Encodes and writes. Returns the new id, or 0 on failure. Makes room first by
// evicting the oldest already-heard messages (which deletes them everywhere).
uint32_t save(uint8_t to, const int16_t *pcm, uint32_t frames);

// Decodes into out (capacity in frames). Returns frames written, 0 on failure.
uint32_t load(uint32_t id, int16_t *out, uint32_t capacity);

void markHeard(uint32_t id, uint8_t person);
bool remove(uint32_t id);  // leaves a tombstone

// ------------------------------------------------------------------ sync

// Bumps on every change to the set of messages or who has heard them, from any
// source. The sync task watches it to know when to tell its peers.
uint32_t changeCounter();

// A fingerprint of the whole state. Two boards with the same value are identical.
uint32_t stateHash();

// Every live message and every tombstone, for the other side to compare against.
uint16_t snapshot(Record *out, uint16_t max);

bool isTombstoned(uint32_t id);
bool mergeHeard(uint32_t id, uint16_t heard);  // true when it changed anything
bool applyDelete(uint32_t id);                  // delete + tombstone, idempotent

// Receiving a message from another board, a chunk at a time.
bool importBegin(uint32_t id, uint32_t size);
bool importWrite(uint32_t offset, const uint8_t *data, size_t n);
bool importEnd();  // validates and adds it; false if it was rejected
void importAbort();

// Sending one. Returns the file size, or 0.
uint32_t exportOpen(uint32_t id, File &out);

uint32_t freeBytes();
uint32_t totalBytes();
float freeSeconds();

void print();
void wipe();

}  // namespace Store
