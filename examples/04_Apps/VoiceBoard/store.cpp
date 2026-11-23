#include "store.h"

#include <FFat.h>

#include "config.h"

namespace Store {

static const char *kDir = "/vb";
static const char *kTombFile = "/vb/tomb.bin";
static const char *kImportFile = "/vb/imp.tmp";
static const uint16_t kMaxMessages = 200;
static const uint16_t kMaxTombstones = 256;

// ---------------------------------------------------------------- locking

static SemaphoreHandle_t g_mutex = nullptr;

struct Lock {
    Lock() { if (g_mutex) xSemaphoreTakeRecursive(g_mutex, portMAX_DELAY); }
    ~Lock() { if (g_mutex) xSemaphoreGiveRecursive(g_mutex); }
};

// --------------------------------------------------------------- IMA ADPCM
//
// The standard 4-bit codec. Chosen over anything cleverer because it is a dozen
// lines, has no licence, decodes in a single pass with no lookahead, and is good
// enough for speech at 14.7 kHz — the speaker on this board is the limiting part.

static const int8_t kIndexTable[16] = {-1, -1, -1, -1, 2, 4, 6, 8,
                                       -1, -1, -1, -1, 2, 4, 6, 8};

static const int16_t kStepTable[89] = {
    7,     8,     9,     10,    11,    12,    13,    14,    16,    17,    19,
    21,    23,    25,    28,    31,    34,    37,    41,    45,    50,    55,
    60,    66,    73,    80,    88,    97,    107,   118,   130,   143,   157,
    173,   190,   209,   230,   253,   279,   307,   337,   371,   408,   449,
    494,   544,   598,   658,   724,   796,   876,   963,   1060,  1166,  1282,
    1411,  1552,  1707,  1878,  2066,  2272,  2499,  2749,  3024,  3327,  3660,
    4026,  4428,  4871,  5358,  5894,  6484,  7132,  7845,  8630,  9493,  10442,
    11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794,
    32767};

struct AdpcmState {
    int32_t pred;
    int32_t index;
};

static uint8_t encodeSample(AdpcmState &s, int32_t sample) {
    int32_t step = kStepTable[s.index];
    int32_t diff = sample - s.pred;
    uint8_t code = 0;
    if (diff < 0) {
        code = 8;
        diff = -diff;
    }
    int32_t vpdiff = step >> 3;
    if (diff >= step) {
        code |= 4;
        diff -= step;
        vpdiff += step;
    }
    step >>= 1;
    if (diff >= step) {
        code |= 2;
        diff -= step;
        vpdiff += step;
    }
    step >>= 1;
    if (diff >= step) {
        code |= 1;
        vpdiff += step;
    }
    s.pred += (code & 8) ? -vpdiff : vpdiff;
    s.pred = constrain(s.pred, -32768, 32767);
    s.index = constrain(s.index + kIndexTable[code], 0, 88);
    return code;
}

static int32_t decodeSample(AdpcmState &s, uint8_t code) {
    int32_t step = kStepTable[s.index];
    int32_t vpdiff = step >> 3;
    if (code & 4) vpdiff += step;
    if (code & 2) vpdiff += step >> 1;
    if (code & 1) vpdiff += step >> 2;
    s.pred += (code & 8) ? -vpdiff : vpdiff;
    s.pred = constrain(s.pred, -32768, 32767);
    s.index = constrain(s.index + kIndexTable[code], 0, 88);
    return s.pred;
}

// ------------------------------------------------------------------ files

struct __attribute__((packed)) Header {
    uint8_t magic[2];  // 'V','B'
    uint8_t version;   // 2
    uint8_t to;
    uint16_t heard;
    uint16_t reserved;
    uint32_t id;
    uint32_t order;
    uint32_t samples;  // at the stored rate, i.e. capture frames / kStoreDecimate
    int16_t pred;
    uint8_t index;
    uint8_t reserved2;
};
static_assert(sizeof(Header) == 24, "header layout");

static Msg g_msgs[kMaxMessages];
static uint16_t g_count = 0;
static uint32_t g_tomb[kMaxTombstones];
static uint16_t g_tombCount = 0;
static uint32_t g_clock = 1;  // next Lamport stamp
static uint32_t g_changes = 0;
static bool g_mounted = false;

// Import in progress.
static File g_imp;
static uint32_t g_impId = 0;
static uint32_t g_impSize = 0;
static uint32_t g_impGot = 0;

static void pathFor(uint32_t id, char *out, size_t n) {
    snprintf(out, n, "%s/%08lX.msg", kDir, (unsigned long)id);
}

static bool before(const Msg &a, const Msg &b) {
    return a.order != b.order ? a.order < b.order : a.id < b.id;
}

static void insertSorted(const Msg &m) {
    if (g_count >= kMaxMessages) return;
    uint16_t i = g_count;
    while (i > 0 && before(m, g_msgs[i - 1])) {
        g_msgs[i] = g_msgs[i - 1];
        i--;
    }
    g_msgs[i] = m;
    g_count++;
    if (m.order >= g_clock) g_clock = m.order + 1;
}

static int indexOf(uint32_t id) {
    for (uint16_t i = 0; i < g_count; i++) {
        if (g_msgs[i].id == id) return i;
    }
    return -1;
}

static bool tombed(uint32_t id) {
    for (uint16_t i = 0; i < g_tombCount; i++) {
        if (g_tomb[i] == id) return true;
    }
    return false;
}

static void saveTombs() {
    File f = FFat.open(kTombFile, FILE_WRITE);
    if (!f) return;
    f.write((const uint8_t *)g_tomb, g_tombCount * sizeof(uint32_t));
    f.close();
}

static void addTomb(uint32_t id) {
    if (tombed(id)) return;
    if (g_tombCount >= kMaxTombstones) {
        // Forget the oldest. A board that has been off long enough to have missed
        // this many deletes can resurrect a very old message, which is a small price
        // for a bounded list.
        memmove(g_tomb, g_tomb + 1, (kMaxTombstones - 1) * sizeof(uint32_t));
        g_tombCount = kMaxTombstones - 1;
    }
    g_tomb[g_tombCount++] = id;
    saveTombs();
}

bool begin() {
    if (!g_mutex) g_mutex = xSemaphoreCreateRecursiveMutex();
    Lock lk;

    // formatOnFail: a first boot on a blank partition formats it, and a corrupt
    // one comes back usable rather than leaving the board mute forever.
    if (!FFat.begin(true)) {
        Serial.println("  FFat: mount failed even after formatting");
        return false;
    }
    g_mounted = true;
    if (!FFat.exists(kDir)) FFat.mkdir(kDir);

    g_count = 0;
    g_tombCount = 0;
    g_clock = 1;

    File t = FFat.open(kTombFile, FILE_READ);
    if (t) {
        g_tombCount = (uint16_t)(t.read((uint8_t *)g_tomb, sizeof(g_tomb)) / sizeof(uint32_t));
        t.close();
    }
    FFat.remove(kImportFile);  // a transfer cut off by a power loss

    File dir = FFat.open(kDir);
    if (!dir || !dir.isDirectory()) return true;

    // Collect names first: deleting while iterating a directory is not safe.
    String stale[16];
    uint8_t nStale = 0;
    for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
        String name = String(f.path());
        if (f.isDirectory() || name.endsWith("tomb.bin") || name.endsWith("imp.tmp")) {
            f.close();
            continue;
        }
        Header h;
        bool ok = f.read((uint8_t *)&h, sizeof(h)) == sizeof(h) && h.magic[0] == 'V' &&
                  h.magic[1] == 'B' && h.version == 2 && !tombed(h.id);
        if (ok) {
            Msg m;
            m.id = h.id;
            m.order = h.order;
            m.to = h.to;
            m.heard = h.heard;
            m.bytes = (uint32_t)f.size();
            insertSorted(m);
        } else if (nStale < 16) {
            stale[nStale++] = name;
        }
        f.close();
    }
    dir.close();
    for (uint8_t i = 0; i < nStale; i++) FFat.remove(stale[i]);
    return true;
}

uint16_t count() { return g_count; }
const Msg *at(uint16_t i) { return i < g_count ? &g_msgs[i] : nullptr; }

const Msg *find(uint32_t id) {
    Lock lk;
    int i = indexOf(id);
    return i < 0 ? nullptr : &g_msgs[i];
}

static bool isUnreadFor(const Msg &m, uint8_t person) {
    if (m.to != person && m.to != kToAll) return false;
    return !((m.heard >> person) & 1);
}

uint16_t unreadFor(uint8_t person) {
    Lock lk;
    uint16_t n = 0;
    for (uint16_t i = 0; i < g_count; i++) {
        if (isUnreadFor(g_msgs[i], person)) n++;
    }
    return n;
}

uint32_t oldestUnreadFor(uint8_t person) {
    Lock lk;
    for (uint16_t i = 0; i < g_count; i++) {
        if (isUnreadFor(g_msgs[i], person)) return g_msgs[i].id;
    }
    return 0;
}

uint32_t newestForAll() {
    Lock lk;
    for (int i = (int)g_count - 1; i >= 0; i--) {
        if (g_msgs[i].to == kToAll) return g_msgs[i].id;
    }
    return 0;
}

uint32_t newestHeardBy(uint8_t person) {
    Lock lk;
    for (int i = (int)g_count - 1; i >= 0; i--) {
        const Msg &m = g_msgs[i];
        if ((m.to == person || m.to == kToAll) && ((m.heard >> person) & 1)) {
            return m.id;
        }
    }
    return 0;
}

static void removeAt(int i) {
    char path[40];
    pathFor(g_msgs[i].id, path, sizeof(path));
    FFat.remove(path);
    for (int j = i; j + 1 < (int)g_count; j++) g_msgs[j] = g_msgs[j + 1];
    g_count--;
}

bool remove(uint32_t id) {
    Lock lk;
    int i = indexOf(id);
    if (i >= 0) removeAt(i);
    bool had = i >= 0 || !tombed(id);
    addTomb(id);
    g_changes++;
    return had;
}

bool applyDelete(uint32_t id) {
    Lock lk;
    int i = indexOf(id);
    bool changed = false;
    if (i >= 0) {
        removeAt(i);
        changed = true;
    }
    if (!tombed(id)) {
        addTomb(id);
        changed = true;
    }
    if (changed) g_changes++;
    return changed;
}

bool isTombstoned(uint32_t id) {
    Lock lk;
    return tombed(id);
}

// A message is "done" once the person it was for has heard it. A broadcast has
// no single recipient, so one listener is taken as enough — otherwise the first
// announcement nobody bothered to open would sit in flash forever.
static bool isDone(const Msg &m) {
    if (m.to == kToAll) return m.heard != 0;
    return (m.heard >> m.to) & 1;
}

// Drops one message to make room: the oldest finished one, or failing that the
// oldest of all. The delete is a tombstone, so it reaches every other board too.
static bool evictOne() {
    for (uint16_t i = 0; i < g_count; i++) {
        if (isDone(g_msgs[i])) {
            Serial.printf("[store] full — dropping heard message %08lX\n",
                          (unsigned long)g_msgs[i].id);
            uint32_t id = g_msgs[i].id;
            return remove(id);
        }
    }
    if (g_count > 0) {
        Serial.printf("[store] full — dropping UNHEARD message %08lX\n",
                      (unsigned long)g_msgs[0].id);
        uint32_t id = g_msgs[0].id;
        return remove(id);
    }
    return false;
}

uint32_t save(uint8_t to, const int16_t *pcm, uint32_t frames) {
    Lock lk;
    if (!g_mounted || !pcm || frames < kStoreDecimate) return 0;

    uint32_t samples = frames / kStoreDecimate;
    uint32_t dataBytes = (samples + 1) / 2;
    uint32_t need = sizeof(Header) + dataBytes;

    while ((FFat.freeBytes() < need + kReserveBytes || g_count >= kMaxMessages) &&
           evictOne()) {
    }
    if (FFat.freeBytes() < need) {
        Serial.println("[store] no room even after eviction");
        return 0;
    }

    uint32_t id;
    do {
        id = esp_random();
    } while (id == 0 || indexOf(id) >= 0 || tombed(id));

    char path[40];
    pathFor(id, path, sizeof(path));
    File f = FFat.open(path, FILE_WRITE);
    if (!f) {
        Serial.println("[store] could not create file");
        return 0;
    }

    // Boxcar-average each group of kStoreDecimate samples. It is a crude
    // low-pass, but it is the right one: it puts a null at the new sample rate
    // and speech has almost nothing up there to lose.
    auto decimated = [&](uint32_t i) -> int32_t {
        int32_t sum = 0;
        for (uint8_t k = 0; k < kStoreDecimate; k++) sum += pcm[i * kStoreDecimate + k];
        return sum / kStoreDecimate;
    };

    AdpcmState st = {decimated(0), 0};

    Header h = {};
    h.magic[0] = 'V';
    h.magic[1] = 'B';
    h.version = 2;
    h.to = to;
    h.heard = 0;
    h.id = id;
    h.order = g_clock;
    h.samples = samples;
    h.pred = (int16_t)st.pred;
    h.index = 0;
    f.write((const uint8_t *)&h, sizeof(h));

    uint8_t buf[256];
    size_t bi = 0;
    uint8_t pending = 0;
    bool haveLow = false;
    bool ok = true;
    for (uint32_t i = 0; i < samples; i++) {
        uint8_t code = encodeSample(st, decimated(i));
        if (!haveLow) {
            pending = code;
            haveLow = true;
        } else {
            buf[bi++] = (uint8_t)(pending | (code << 4));
            haveLow = false;
            if (bi == sizeof(buf)) {
                ok &= f.write(buf, bi) == bi;
                bi = 0;
            }
        }
    }
    if (haveLow) buf[bi++] = pending;
    if (bi) ok &= f.write(buf, bi) == bi;

    f.close();
    // Not f.size(): on FAT that reads 0 until the file has been closed.
    uint32_t bytes = need;

    if (!ok) {
        FFat.remove(path);
        Serial.println("[store] write failed");
        return 0;
    }

    Msg m = {id, h.order, to, 0, bytes};
    insertSorted(m);
    g_changes++;
    return id;
}

uint32_t load(uint32_t id, int16_t *out, uint32_t capacity) {
    Lock lk;
    if (!g_mounted || !out) return 0;

    char path[40];
    pathFor(id, path, sizeof(path));
    File f = FFat.open(path, FILE_READ);
    if (!f) return 0;

    Header h;
    if (f.read((uint8_t *)&h, sizeof(h)) != sizeof(h) || h.magic[0] != 'V' ||
        h.magic[1] != 'B') {
        f.close();
        return 0;
    }

    uint32_t samples = h.samples;
    if (samples * kStoreDecimate > capacity) samples = capacity / kStoreDecimate;

    AdpcmState st = {h.pred, h.index};
    int32_t prev = st.pred;
    uint32_t written = 0;

    // The encoder codes sample 0 against a predictor that already equals it, so
    // decoding every code lines up one-to-one with the stored samples. Each
    // decoded sample after the first emits kStoreDecimate output frames,
    // interpolated from the one before it; the last is flushed as a short hold.
    uint8_t buf[256];
    uint32_t done = 0;
    bool first = true;
    while (done < samples) {
        int n = f.read(buf, sizeof(buf));
        if (n <= 0) break;
        for (int b = 0; b < n && done < samples; b++) {
            for (uint8_t half = 0; half < 2 && done < samples; half++) {
                uint8_t code = half ? (buf[b] >> 4) : (buf[b] & 0x0F);
                int32_t cur = decodeSample(st, code);
                if (first) {
                    prev = cur;
                    first = false;
                } else {
                    for (uint8_t k = 0; k < kStoreDecimate; k++) {
                        out[written++] =
                            (int16_t)(prev + (cur - prev) * k / kStoreDecimate);
                    }
                    prev = cur;
                }
                done++;
            }
        }
    }
    for (uint8_t k = 0; k < kStoreDecimate && written < capacity; k++) {
        out[written++] = (int16_t)prev;
    }
    f.close();
    return written;
}

static void persistHeard(uint32_t id, uint16_t heard) {
    char path[40];
    pathFor(id, path, sizeof(path));
    File f = FFat.open(path, "r+");
    if (f) {
        f.seek(offsetof(Header, heard));
        f.write((const uint8_t *)&heard, 2);
        f.close();
    }
}

void markHeard(uint32_t id, uint8_t person) {
    if (person >= kPeople) return;
    Lock lk;
    int i = indexOf(id);
    if (i < 0 || ((g_msgs[i].heard >> person) & 1)) return;
    g_msgs[i].heard |= (uint16_t)(1u << person);
    persistHeard(id, g_msgs[i].heard);  // or the unread light returns after a power cut
    g_changes++;
}

bool mergeHeard(uint32_t id, uint16_t heard) {
    Lock lk;
    int i = indexOf(id);
    if (i < 0) return false;
    uint16_t merged = g_msgs[i].heard | (heard & 0x0FFF);
    if (merged == g_msgs[i].heard) return false;
    g_msgs[i].heard = merged;
    persistHeard(id, merged);
    g_changes++;
    return true;
}

// ------------------------------------------------------------------ sync

uint32_t changeCounter() { return g_changes; }

static uint32_t mix(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7FEB352Du;
    x ^= x >> 15;
    x *= 0x846CA68Bu;
    x ^= x >> 16;
    return x;
}

// A sum, not a chain, so it does not depend on the order messages happen to be held
// in. A tombstone and a live message with the same id hash differently.
uint32_t stateHash() {
    Lock lk;
    uint32_t h = 0;
    for (uint16_t i = 0; i < g_count; i++) {
        h += mix(g_msgs[i].id ^ mix(g_msgs[i].heard + 0x1000u));
    }
    for (uint16_t i = 0; i < g_tombCount; i++) {
        h += mix(g_tomb[i] ^ 0xA5A5A5A5u);
    }
    return h;
}

uint16_t snapshot(Record *out, uint16_t max) {
    Lock lk;
    uint16_t n = 0;
    for (uint16_t i = 0; i < g_count && n < max; i++) {
        out[n++] = {g_msgs[i].id, g_msgs[i].order, g_msgs[i].heard, g_msgs[i].to, 0};
    }
    for (uint16_t i = 0; i < g_tombCount && n < max; i++) {
        out[n++] = {g_tomb[i], 0, 0, 0, 1};
    }
    return n;
}

bool importBegin(uint32_t id, uint32_t size) {
    Lock lk;
    importAbort();
    if (!g_mounted || id == 0 || indexOf(id) >= 0 || tombed(id)) return false;
    if (size <= sizeof(Header) || size > 400 * 1024) return false;
    if (FFat.freeBytes() < size + kReserveBytes) {
        // Make room the same way a local recording would.
        while (FFat.freeBytes() < size + kReserveBytes && evictOne()) {
        }
        if (FFat.freeBytes() < size) return false;
    }
    g_imp = FFat.open(kImportFile, FILE_WRITE);
    if (!g_imp) return false;
    g_impId = id;
    g_impSize = size;
    g_impGot = 0;
    return true;
}

bool importWrite(uint32_t offset, const uint8_t *data, size_t n) {
    Lock lk;
    if (!g_imp) return false;
    // A retransmitted chunk (our acknowledgement was lost) arrives again; chunks are
    // strictly sequential, so anything already written is simply skipped.
    if (offset + n <= g_impGot) return true;
    if (offset != g_impGot) return false;
    if (g_imp.write(data, n) != n) return false;
    g_impGot += n;
    return true;
}

void importAbort() {
    Lock lk;
    if (g_imp) {
        g_imp.close();
        FFat.remove(kImportFile);
    }
    g_impId = 0;
    g_impGot = 0;
}

bool importEnd() {
    Lock lk;
    if (!g_imp) return false;
    uint32_t id = g_impId;
    bool complete = g_impGot == g_impSize;
    g_imp.close();

    bool ok = false;
    if (complete) {
        File f = FFat.open(kImportFile, FILE_READ);
        Header h;
        if (f && f.read((uint8_t *)&h, sizeof(h)) == sizeof(h) && h.magic[0] == 'V' &&
            h.magic[1] == 'B' && h.version == 2 && h.id == id &&
            f.size() == g_impSize && indexOf(id) < 0 && !tombed(id)) {
            f.close();
            char path[40];
            pathFor(id, path, sizeof(path));
            if (FFat.rename(kImportFile, path)) {
                Msg m = {h.id, h.order, h.to, h.heard, g_impSize};
                insertSorted(m);
                g_changes++;
                ok = true;
            }
        } else if (f) {
            f.close();
        }
    }
    if (!ok) FFat.remove(kImportFile);
    g_impId = 0;
    g_impGot = 0;
    return ok;
}

uint32_t exportOpen(uint32_t id, File &out) {
    Lock lk;
    if (indexOf(id) < 0) return 0;
    char path[40];
    pathFor(id, path, sizeof(path));
    out = FFat.open(path, FILE_READ);
    return out ? (uint32_t)out.size() : 0;
}

uint32_t freeBytes() { return g_mounted ? FFat.freeBytes() : 0; }
uint32_t totalBytes() { return g_mounted ? FFat.totalBytes() : 0; }

float freeSeconds() {
    // 4 bits per stored sample at kSampleRate / kStoreDecimate.
    float bytesPerSecond = (float)kSampleRate / kStoreDecimate / 2.0f;
    return freeBytes() / bytesPerSecond;
}

void print() {
    Lock lk;
    Serial.printf("  %u messages, %u tombstones, hash %08lX, %.1f MB free of %.1f MB (~%.0f s)\n",
                  g_count, g_tombCount, (unsigned long)stateHash(), freeBytes() / 1048576.0f,
                  totalBytes() / 1048576.0f, freeSeconds());
    for (uint16_t i = 0; i < g_count; i++) {
        const Msg &m = g_msgs[i];
        char to[8];
        if (m.to == kToAll) {
            snprintf(to, sizeof(to), "ALL");
        } else {
            snprintf(to, sizeof(to), "P%u", m.to + 1);
        }
        Serial.printf("    %08lX #%-4lu -> %-3s  heard 0x%03X  %5.1f s  %u KB\n",
                      (unsigned long)m.id, (unsigned long)m.order, to, m.heard,
                      (float)(m.bytes - sizeof(Header)) * 2.0f * kStoreDecimate / kSampleRate,
                      (unsigned)(m.bytes / 1024));
    }
}

void wipe() {
    Lock lk;
    while (g_count > 0) {
        uint32_t id = g_msgs[0].id;
        removeAt(0);
        addTomb(id);
    }
    g_changes++;
}

}  // namespace Store
