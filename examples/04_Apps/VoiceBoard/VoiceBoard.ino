// TinkerPad — VoiceBoard
//
// A family message board. Twelve people, one key each. Tap MIC, tap the key of the
// person the message is for, and talk. They hear it by tapping their own key.
//
//   [ 1 ][ 2 ][ 3 ][ 4 ]
//   [ 5 ][ 6 ][ 7 ][ 8 ]       one key per person, each in its own colour
//   [ 9 ][10 ][11 ][12 ]
//   [MIC][REPLAY][DELETE][ ALL ]
//
//   MIC, then a person's key  record a message for them. Talk; stop by tapping any
//                             key, or just stop talking and it sends itself
//   MIC, then ALL             record an announcement for everyone
//   a person's key (tap)      play their oldest unheard message
//   ALL (tap)                 play the latest announcement
//   REPLAY (tap)              play the last message again
//   DELETE (hold 1 s)         delete the last message that was played
//
// Everything is a tap except DELETE. Nothing has to be held while talking, which is
// what made the first version hard for small children: it wanted two keys held down
// and a voice at the same time.
//
// A key that is blinking has messages waiting: one blink per message, up to five.
//
// Messages live in flash, not RAM, so they survive being unplugged. The board has
// no way to tell who is pressing a key, so there is no privacy beyond politeness —
// which is also why DELETE has to be held rather than tapped.
//
// Board: TinkerPad (ESP32-S3 N16R8).
//   PSRAM=opi, FlashSize=16M, PartitionScheme=app3M_fat9M_16MB
//   CDCOnBoot=cdc on a usbmodem* port, default on a wchusbserial* one.
//
// The key/LED/position maps are constants (layout.h): the PCB is final, so there
// is nothing to calibrate.

#include <Wire.h>
#include <esp_heap_caps.h>

#include "config.h"
#include "layout.h"
#include "ledui.h"
#include "mic.h"
#include "mixer.h"
#include "pads.h"
#include "pins.h"
#include "store.h"
#include "portal.h"
#include "sync.h"

static Pads pads;

struct KeyState {
    bool down = false;
    uint32_t downMs = 0;
    bool deleted = false;
};

static KeyState g_keys[Pads::COUNT];
// MIC has been tapped and the board is waiting to be told who the message is for.
static bool g_armed = false;
static uint32_t g_armedMs = 0;

// Recording bookkeeping, for ending a message by itself.
static uint32_t g_recStartMs = 0;
static uint32_t g_lastVoiceMs = 0;
static bool g_heardVoice = false;
static uint32_t g_lastGhostWarnMs = 0;
static uint32_t g_lastLedMs = 0;
static uint32_t g_lastRefreshMs = 0;
static bool g_ready = false;

// Two message buffers in PSRAM. One may still be playing while the next message
// is decoded into the other, and a capture always goes into whichever is idle, so
// nothing ever writes into memory the mixer is reading.
static int16_t *g_buf[2] = {nullptr, nullptr};
static uint32_t g_bufFrames = 0;
static uint8_t g_playIdx = 0;

static int8_t g_recTo = -1;   // recipient: 0..11, kToAll, or -1 when not recording
static int8_t g_recPos = -1;  // the key to light
static uint8_t g_recBuf = 0;

static uint32_t g_lastSeq = 0;  // for REPLAY and DELETE
static int8_t g_lastPos = -1;

// Short feedback sounds, generated at boot.
static int16_t *g_chimeSent = nullptr;
static uint32_t g_chimeSentFrames = 0;
static int16_t *g_chimeReady = nullptr;
static uint32_t g_chimeReadyFrames = 0;
static int16_t *g_chimeNo = nullptr;
static uint32_t g_chimeNoFrames = 0;

// ------------------------------------------------------------------ chimes

static int16_t *makeTones(const float *hz, uint8_t notes, float noteSec, uint32_t &frames) {
    uint32_t per = (uint32_t)(noteSec * kSampleRate);
    frames = per * notes;
    int16_t *b = (int16_t *)malloc(frames * sizeof(int16_t));
    if (!b) {
        frames = 0;
        return nullptr;
    }
    float phase = 0.0f;
    for (uint8_t n = 0; n < notes; n++) {
        float w = 2.0f * PI * hz[n] / kSampleRate;
        for (uint32_t i = 0; i < per; i++) {
            // Attack and release so the note starts and ends without a click.
            float t = (float)i / per;
            float env = min(t * 20.0f, 1.0f) * min((1.0f - t) * 6.0f, 1.0f);
            b[n * per + i] = (int16_t)(sinf(phase) * env * 9000.0f);
            phase += w;
        }
    }
    return b;
}

static void buildChimes() {
    const float up[2] = {660.0f, 880.0f};
    const float down[2] = {300.0f, 220.0f};
    g_chimeSent = makeTones(up, 2, 0.09f, g_chimeSentFrames);
    g_chimeNo = makeTones(down, 2, 0.10f, g_chimeNoFrames);
    const float ready[1] = {1000.0f};
    g_chimeReady = makeTones(ready, 1, 0.08f, g_chimeReadyFrames);
}

static void chime(bool ok) {
    int16_t *b = ok ? g_chimeSent : g_chimeNo;
    uint32_t n = ok ? g_chimeSentFrames : g_chimeNoFrames;
    if (b && n) Mixer::trigger(b, n, 0.8f);
}

// ------------------------------------------------------------------- state

static void refreshLeds() {
    for (uint8_t p = 0; p < kPeople; p++) {
        uint16_t n = Store::unreadFor(p);
        LedUI::setUnread(p, n > 255 ? 255 : (uint8_t)n);
    }
    LedUI::setHaveBroadcast(Store::newestForAll() != 0);
    LedUI::setHaveLast(g_lastSeq != 0 && Store::find(g_lastSeq) != nullptr);
}

// ----------------------------------------------------------------- playback

// person is who is listening, or -1 when it is a replay or an announcement tap and
// nobody's unread state should change.
static void playMessage(uint32_t seq, int8_t pos, int8_t person) {
    uint8_t next = 1 - g_playIdx;
    uint32_t frames = Store::load(seq, g_buf[next], g_bufFrames);
    if (frames == 0) {
        Serial.printf("[play] #%lu could not be read\n", (unsigned long)seq);
        LedUI::ack(pos, false);
        chime(false);
        return;
    }

    Mixer::stopAll();
    Mixer::trigger(g_buf[next], frames, kVoiceGain);
    g_playIdx = next;

    uint32_t ms = (uint32_t)((uint64_t)frames * 1000 / kSampleRate);
    LedUI::setPlaying(pos, millis() + ms);

    g_lastSeq = seq;
    g_lastPos = pos;
    if (person >= 0) Store::markHeard(seq, (uint8_t)person);

    Serial.printf("[play] #%lu  %.1f s\n", (unsigned long)seq, ms / 1000.0f);
    refreshLeds();
}

static void playNextFor(uint8_t person) {
    uint32_t seq = Store::oldestUnreadFor(person);
    if (seq != 0) {
        playMessage(seq, person, (int8_t)person);
        return;
    }

    // Nothing new. A second tap should let you hear it again, even though the key
    // is no longer blinking — a child who missed it has no other way to ask. Only
    // when there is nothing at all does the key say "nothing here".
    seq = Store::newestHeardBy(person);
    if (seq != 0) {
        playMessage(seq, person, -1);
        return;
    }
    LedUI::ack(person, false);
    Serial.printf("[play] P%u: no messages\n", person + 1);
}

static void playLatestAnnouncement() {
    uint32_t seq = Store::newestForAll();
    if (seq == 0) {
        LedUI::ack(15, false);
        Serial.println("[play] no announcement");
        return;
    }
    playMessage(seq, 15, -1);
}

static void replayLast() {
    if (g_lastSeq == 0 || !Store::find(g_lastSeq)) {
        LedUI::ack(13, false);
        Serial.println("[replay] nothing to replay");
        return;
    }
    playMessage(g_lastSeq, g_lastPos, -1);
}

static void deleteLast() {
    if (g_lastSeq == 0 || !Store::find(g_lastSeq)) {
        LedUI::ack(14, false);
        chime(false);
        return;
    }
    Mixer::stopAll();
    LedUI::setPlaying(-1, 0);
    Store::remove(g_lastSeq);
    Serial.printf("[delete] #%lu removed\n", (unsigned long)g_lastSeq);
    g_lastSeq = 0;
    LedUI::ack(14, true);
    refreshLeds();
}

// --------------------------------------------------------------- recording

static void beginRecording(int8_t to, int8_t pos) {
    // A short blip says "go ahead and talk" — a child cannot be expected to guess
    // the instant the board starts listening. It is played BEFORE the mixer is
    // muted and the capture started, and the head of the recording is thrown away
    // (see endRecording), so it never ends up in the message.
    Mixer::stopAll();
    if (g_chimeReady) Mixer::trigger(g_chimeReady, g_chimeReadyFrames, 0.7f);
    delay(220);

    // The mic and speaker are centimetres apart, so anything the speaker plays
    // during a capture ends up in the recording. Silence it.
    Mixer::stopAll();
    Mixer::setMuted(true);
    LedUI::setPlaying(-1, 0);

    g_armed = false;
    LedUI::setArmed(false);

    g_recBuf = 1 - g_playIdx;
    g_recTo = to;
    g_recPos = pos;
    g_recStartMs = millis();
    g_lastVoiceMs = g_recStartMs;
    g_heardVoice = false;
    Mic::startCapture(g_buf[g_recBuf], g_bufFrames);
    LedUI::setRecording(pos, g_bufFrames);

    Serial.printf("[rec] -> %s, up to %.0f s\n",
                  to == (int8_t)kToAll ? "ALL" : "person", kMaxMessageSeconds);
}

// byTap: the person ended it with a key. The click of that key is in the tail of the
// recording, so a little is trimmed off. cancelled: nothing was said, so nothing is
// sent.
static void endRecording(bool byTap, bool cancelled = false) {
    if (g_recTo == -1 && g_recPos == -1) return;

    int8_t pos = g_recPos;
    uint8_t to = (uint8_t)g_recTo;
    uint32_t raw = Mic::stopCapture();
    float peak = Mic::lastPeakDbfs();
    uint32_t clipped = Mic::clipped();
    delay(5);  // let the mic task notice the stop before the buffer is touched

    g_recTo = -1;
    g_recPos = -1;
    LedUI::setRecording(-1, 0);

    // The first part of a capture is the mic's pre-roll plus the click of the key
    // that started it, and the last part (when a key ended it) is another click.
    // Neither is speech, so both are cut before the silence trimmer runs.
    uint32_t head = Mic::preRollFrames() + kSampleRate / 12;
    uint32_t tail = byTap ? kSampleRate / 6 : 0;
    if (raw > head + tail) {
        memmove(g_buf[g_recBuf], g_buf[g_recBuf] + head, (raw - head) * sizeof(int16_t));
        raw -= head + tail;
    } else {
        raw = 0;
    }

    uint32_t frames = cancelled ? 0 : Mic::polish(g_buf[g_recBuf], raw);
    Mixer::setMuted(false);

    if (cancelled) {
        Serial.println("[rec] nothing was said — cancelled");
        LedUI::ack(pos, false);
        chime(false);
        return;
    }

    uint32_t minFrames = (uint32_t)((uint64_t)kMinMessageMs * kSampleRate / 1000);
    if (frames < minFrames) {
        Serial.printf("[rec] too short or silent (%.0f ms) — not sent\n",
                      1000.0f * frames / kSampleRate);
        LedUI::ack(pos, false);
        chime(false);
        return;
    }

    uint32_t t0 = millis();
    uint32_t seq = Store::save(to, g_buf[g_recBuf], frames);
    if (seq == 0) {
        Serial.println("[rec] could not be saved");
        LedUI::ack(pos, false);
        chime(false);
        return;
    }

    Serial.printf("[rec] #%lu saved  %.1f s  peak %.1f dBFS  (%u ms to store)\n",
                  (unsigned long)seq, (float)frames / kSampleRate, peak,
                  (unsigned)(millis() - t0));
    if (clipped > 0) {
        Serial.printf("[rec] WARNING %u clipped samples — talk a little further "
                      "from the mic\n", (unsigned)clipped);
    } else if (peak < -30.0f) {
        Serial.println("[rec] quiet — hold the board closer next time");
    }

    LedUI::ack(pos, true);
    chime(true);
    refreshLeds();
}

// ---------------------------------------------------------------- gestures

static void setArmed(bool armed) {
    g_armed = armed;
    g_armedMs = millis();
    LedUI::setArmed(armed);
}

static void onPress(uint8_t bit) {
    Layout::Role role = Layout::roleForKey(bit);

    // While a message is being recorded, any key finishes it. A child who has
    // finished talking taps whatever is nearest, and that has to work.
    if (g_recTo != -1) {
        endRecording(true);
        return;
    }

    // MIC has been tapped: the next key says who it is for.
    if (g_armed) {
        if (role == Layout::ROLE_PERSON) {
            uint8_t p = Layout::personForKey(bit);
            if (p < kPeople) beginRecording((int8_t)p, (int8_t)p);
        } else if (role == Layout::ROLE_ALL) {
            beginRecording((int8_t)kToAll, 15);
        } else {
            setArmed(false);  // MIC again, or anything else, changes their mind
        }
        return;
    }

    switch (role) {
        case Layout::ROLE_MIC:
            setArmed(true);
            break;
        case Layout::ROLE_PERSON: {
            uint8_t p = Layout::personForKey(bit);
            if (p < kPeople) playNextFor(p);
            break;
        }
        case Layout::ROLE_ALL:
            playLatestAnnouncement();
            break;
        case Layout::ROLE_REPLAY:
            replayLast();
            break;
        case Layout::ROLE_DELETE:
            break;  // acts on the hold
    }
}

static void onHold(uint8_t bit, uint32_t heldMs) {
    Layout::Role role = Layout::roleForKey(bit);
    KeyState &st = g_keys[bit];

    if (role != Layout::ROLE_DELETE) return;
    if (g_armed || g_recTo != -1) return;
    if (st.deleted) return;
    LedUI::setDeleteHold(heldMs);
    if (heldMs >= kDeleteHoldMs) {
        st.deleted = true;
        deleteLast();
    }
}

static void onRelease(uint8_t bit) {
    if (Layout::roleForKey(bit) == Layout::ROLE_DELETE) LedUI::setDeleteHold(0);
    g_keys[bit].deleted = false;
}

// Ends a recording without anyone touching the board.
static void serviceRecording() {
    uint32_t now = millis();

    if (g_armed && now - g_armedMs > kArmedMs) setArmed(false);

    if (g_recTo == -1) return;

    if (Mic::level() > kVoiceLevel) {
        g_heardVoice = true;
        g_lastVoiceMs = now;
    }

    if (Mic::capturedFrames() >= g_bufFrames) {
        Serial.println("[rec] reached the time limit");
        endRecording(false);
    } else if (g_heardVoice && now - g_lastVoiceMs > kSilenceStopMs) {
        Serial.println("[rec] quiet — sending");
        endRecording(false);
    } else if (!g_heardVoice && now - g_recStartMs > kNoSpeechMs) {
        endRecording(false, true);
    }
}

static void servicePads() {
    uint16_t mask = pads.scan();
    uint32_t nowMs = millis();

    // No anti-ghosting diodes: some three-key states are ambiguous, and acting on
    // a new press then could light or play a key nobody touched.
    bool ghost = Pads::hasGhostRisk(mask);
    if (ghost && nowMs - g_lastGhostWarnMs > 1000) {
        g_lastGhostWarnMs = nowMs;
        Serial.printf("[ghost] 0x%04X is ambiguous — ignoring new presses\n", mask);
    }

    for (uint8_t bit = 0; bit < Pads::COUNT; bit++) {
        bool down = (mask >> bit) & 1;
        KeyState &st = g_keys[bit];

        if (down && !st.down) {
            st.down = true;
            st.downMs = nowMs;
            st.deleted = false;
            if (!ghost) onPress(bit);
        } else if (!down && st.down) {
            st.down = false;
            onRelease(bit);
        } else if (down) {
            onHold(bit, nowMs - st.downMs);
        }
    }

    serviceRecording();
}

// --------------------------------------------------------------- self-test

// Round-trips a synthetic two-tone "voice" through the real store path and reports
// how faithfully it came back. It exercises the decimator, the ADPCM codec, the
// FAT file and the interpolator end to end with no one needing to speak.
static void selfTest() {
    const uint32_t frames = kSampleRate * 2;
    int16_t *src = g_buf[0];
    int16_t *dst = g_buf[1];
    if (frames > g_bufFrames) return;

    Mixer::stopAll();
    for (uint32_t i = 0; i < frames; i++) {
        float t = (float)i / kSampleRate;
        float env = 0.5f + 0.5f * sinf(2.0f * PI * 1.5f * t);
        src[i] = (int16_t)(env * (9000.0f * sinf(2.0f * PI * 300.0f * t) +
                                  6000.0f * sinf(2.0f * PI * 1100.0f * t)));
    }

    uint32_t t0 = millis();
    uint32_t seq = Store::save(0, src, frames);
    uint32_t tSave = millis() - t0;
    if (seq == 0) {
        Serial.println("self-test: save FAILED");
        return;
    }

    t0 = millis();
    uint32_t got = Store::load(seq, dst, g_bufFrames);
    uint32_t tLoad = millis() - t0;
    Store::remove(seq);

    // Compare after the 1/3-rate stage's own group delay. The boxcar decimator
    // plus linear interpolation delays the signal by one stored sample.
    double sig = 0, err = 0;
    int best = 0;
    double bestErr = 1e30;
    for (int lag = 0; lag <= 6; lag++) {
        double e = 0;
        for (uint32_t i = 2000; i < frames - 2000; i++) {
            double d = (double)src[i] - (double)dst[i + lag];
            e += d * d;
        }
        if (e < bestErr) {
            bestErr = e;
            best = lag;
        }
    }
    for (uint32_t i = 2000; i < frames - 2000; i++) {
        double s = src[i];
        double d = s - (double)dst[i + best];
        sig += s * s;
        err += d * d;
    }
    Serial.printf("self-test: %u frames in, %u out, save %u ms, load %u ms\n",
                  (unsigned)frames, (unsigned)got, (unsigned)tSave, (unsigned)tLoad);
    Serial.printf("  SNR %.1f dB at lag %d (>20 dB is clean speech-grade)\n",
                  10.0 * log10(sig / max(err, 1.0)), best);
    refreshLeds();
}

// Puts a short synthetic message for person 1 into the store, so syncing can be
// checked without speaking into anything.
static void makeTestMessage() {
    const uint32_t frames = kSampleRate * 2;
    int16_t *b = g_buf[1 - g_playIdx];
    for (uint32_t i = 0; i < frames; i++) {
        float t = (float)i / kSampleRate;
        b[i] = (int16_t)(9000.0f * sinf(2.0f * PI * (300.0f + 200.0f * t) * t));
    }
    uint32_t id = Store::save(0, b, frames);
    Serial.printf("test message %08lX saved\n", (unsigned long)id);
    refreshLeds();
}

// ----------------------------------------------------------------- console

static void printHelp() {
    Serial.println();
    Serial.println("  s  status       l  list messages     e  codec self-test");
    Serial.println("  p  sync status  w  network status  a  close the hotspot now");
    Serial.println("  n  test message for P1  h  P1 hears oldest  d  delete newest  (debug)");
    Serial.println("  W  delete ALL messages (cannot be undone)");
    Serial.print("> ");
}

static void handleConsole(int c) {
    switch (c) {
        case 's':
            Serial.printf("  PSRAM free %u KB   buffers 2 x %.1f s\n",
                          (unsigned)(ESP.getFreePsram() / 1024),
                          (float)g_bufFrames / kSampleRate);
            Serial.printf("  mixer load %.0f%%  underruns %u\n", Mixer::load() * 100.0f,
                          (unsigned)Mixer::underruns());
            Layout::print();
            Store::print();
            Portal::print();
            Sync::print();
            break;
        case 'l': Store::print(); break;
        case 'p': Sync::print(); break;
        case 'w': Portal::print(); break;
        case 'a': Portal::closeHotspot(); break;
        case 'n': makeTestMessage(); break;
        // Debug stand-ins for a person's taps, so syncing can be tested hands-free.
        case 'h': {
            uint32_t id = Store::oldestUnreadFor(0);
            if (id) Store::markHeard(id, 0);
            Serial.printf("P1 heard %08lX\n", (unsigned long)id);
            refreshLeds();
            break;
        }
        case 'd': {
            uint16_t n = Store::count();
            if (n) {
                uint32_t id = Store::at(n - 1)->id;
                Store::remove(id);
                Serial.printf("deleted %08lX\n", (unsigned long)id);
            }
            refreshLeds();
            break;
        }
        case 'e': selfTest(); break;
        case 'W':
            Store::wipe();
            g_lastSeq = 0;
            refreshLeds();
            Serial.println("all messages deleted");
            break;
        default: printHelp(); return;
    }
    Serial.print("> ");
}

// ------------------------------------------------------------------- setup

void setup() {
    Serial.begin(115200);
    delay(600);

    Serial.println();
    Serial.println("=== TinkerPad — VoiceBoard — family message board ===");
    Serial.printf("PSRAM %u KB   Flash %u MB\n", (unsigned)(ESP.getPsramSize() / 1024),
                  (unsigned)(ESP.getFlashChipSize() / (1024 * 1024)));

    if (ESP.getPsramSize() == 0) {
        Serial.println("FATAL: no PSRAM. Enable OPI PSRAM in the board options.");
        return;
    }

// NO_LEDS: a build switch for a board whose LED chain crashes it at start-up (seen on
// one prototype). Everything else still works, just dark.
#ifndef NO_LEDS
    LedUI::begin();
    LedUI::bootSweep();
#endif

    g_bufFrames = (uint32_t)(kMaxMessageSeconds * kSampleRate);
    for (uint8_t i = 0; i < 2; i++) {
        g_buf[i] = (int16_t *)heap_caps_malloc(g_bufFrames * sizeof(int16_t),
                                               MALLOC_CAP_SPIRAM);
        if (!g_buf[i]) {
            Serial.println("FATAL: could not reserve the message buffers.");
            return;
        }
        memset(g_buf[i], 0, g_bufFrames * sizeof(int16_t));
    }
    Serial.printf("buffers: 2 x %.0f s (%u KB)\n", kMaxMessageSeconds,
                  (unsigned)(2 * g_bufFrames * sizeof(int16_t) / 1024));

    Serial.println("storage");
    if (!Store::begin()) {
        Serial.println("FATAL: no message storage. Is the partition scheme");
        Serial.println("app3M_fat9M_16MB?");
        return;
    }
    Store::print();

    Serial.println("network");
    Portal::begin();
    if (!Sync::begin()) Serial.println("  syncing is off on this board");

    Serial.println("audio");
    if (!Mixer::begin(kSampleRate)) {
        Serial.println("FATAL: the mixer did not start.");
        return;
    }
    if (!Mic::begin(kSampleRate)) {
        Serial.println("FATAL: the microphone did not start.");
        return;
    }
    buildChimes();

    Serial.println("keypad");
    if (pads.begin(ADDR_PCF8574, PIN_I2C_SDA, PIN_I2C_SCL)) {
        Serial.println("  PCF8574 at 0x20");
    } else {
        Serial.println("  PCF8574 not answering — is the dev board seated in the");
        Serial.println("  carrier? It re-probes twice a second.");
    }

    refreshLeds();
    g_ready = true;

    printHelp();
}

// The status pixel on the ESP32-S3 board says how the network is doing.
static void serviceNetLed() {
    LedUI::NetStatus s;
    if (Portal::mode() == Portal::MODE_WIFI && !Portal::wifiConnected()) {
        s = LedUI::NET_WIFI_CONNECTING;  // most urgent: it cannot sync at all
    } else if (Portal::hotspotOn()) {
        s = LedUI::NET_HOTSPOT;
    } else if (Portal::mode() == Portal::MODE_WIFI) {
        s = LedUI::NET_WIFI_OK;
    } else {
        s = LedUI::NET_ESPNOW;
    }
    LedUI::setNetStatus(s, Sync::peersOnline() > 0);
}

void loop() {
    if (!g_ready) {
        delay(500);
        return;
    }

    servicePads();
    Portal::loop();
    serviceNetLed();

    uint32_t now = millis();
    if (now - g_lastRefreshMs >= 2000) {
        g_lastRefreshMs = now;
        refreshLeds();
    }
    if (now - g_lastLedMs >= kLedFrameMs) {
        g_lastLedMs = now;
#ifndef NO_LEDS
        LedUI::render();
#endif
    }

    while (Serial.available()) {
        int c = Serial.read();
        if (c == '\r' || c == '\n' || c == ' ') continue;
        Serial.println((char)c);
        handleConsole(c);
    }
}
