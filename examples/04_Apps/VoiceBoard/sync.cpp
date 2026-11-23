#include "sync.h"

#include <AsyncUDP.h>
#include <WiFi.h>
#include <esp_mac.h>
#include <esp_now.h>

#include "config.h"
#include "portal.h"
#include "store.h"

namespace Sync {

static const uint16_t kUdpPort = 47800;
static const uint8_t kChannel = 1;
static const uint8_t kMaxPeers = 8;
static const uint8_t kChunk = 224;  // message bytes per packet
static const uint8_t kTries = 12;
static const uint32_t kHelloMs = 2000;
static const uint32_t kOnlineMs = 8000;
static const uint32_t kForgetMs = 60000;
static const uint16_t kMaxRecs = 300;
static const uint8_t kRecsPerPacket = 19;

enum : uint8_t {
    T_ACK = 1,
    T_HELLO = 2,
    T_LISTREQ = 3,
    T_LIST = 4,
    T_NEED = 5,
    T_PUSH = 6,
    T_DATA = 7,
    T_DONE = 8,
    T_HEARD = 9,
    T_DEL = 10,
};

static const uint8_t kBroadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// Every frame starts with a type and a 16-bit sequence number, which is what an
// acknowledgement echoes and what lets a retransmission be recognised.
struct __attribute__((packed)) Hdr {
    uint8_t type;
    uint16_t seq;
};

// Hello: who I am and what my state looks like. The MAC is in the payload so a UDP
// hello identifies its sender the same way a radio one does.
struct __attribute__((packed)) Hello {
    uint8_t mac[6];
    uint32_t hash;
    uint16_t count;
};

struct Rx {
    uint8_t mac[6];
    uint32_t ip;
    uint8_t len;
    uint8_t data[250];
};

struct Peer {
    bool used;
    uint8_t mac[6];
    uint32_t ip;
    uint32_t lastSeen;
    uint32_t hash;
    uint16_t count;
    uint32_t lastSession;
    uint32_t backoffUntil;
    uint16_t lastRxSeq;
    bool rxSeqValid;
};

static uint8_t g_mac[6];
static bool g_wifi = false;
static bool g_udpUp = false;
static AsyncUDP g_udp;
static Peer g_peer[kMaxPeers];
static uint32_t g_ackMs = 25;

static QueueHandle_t g_q = nullptr;
static SemaphoreHandle_t g_ackSem = nullptr;
static volatile uint16_t g_ackSeq = 0;
static uint16_t g_txSeq = 0;

// A session in progress.
static bool g_expectList = false;
static Store::Record g_rlist[kMaxRecs];
static uint16_t g_rlistN = 0;
static uint32_t g_rlistGot = 0;
static uint8_t g_rlistTotal = 0;
static Store::Record g_lsnap[kMaxRecs];
static uint32_t g_curImp = 0;
static uint32_t g_doneId = 0;
static uint32_t g_waitId = 0;

static String macStr(const uint8_t *m) {
    char b[18];
    snprintf(b, sizeof(b), "%02X:%02X:%02X:%02X:%02X:%02X", m[0], m[1], m[2], m[3], m[4], m[5]);
    return String(b);
}

// ------------------------------------------------------------------- peers

static int peerIndex(const uint8_t *mac) {
    for (uint8_t i = 0; i < kMaxPeers; i++) {
        if (g_peer[i].used && memcmp(g_peer[i].mac, mac, 6) == 0) return i;
    }
    return -1;
}

static int peerByIp(uint32_t ip) {
    for (uint8_t i = 0; i < kMaxPeers; i++) {
        if (g_peer[i].used && g_peer[i].ip == ip) return i;
    }
    return -1;
}

static void addRadioPeer(const uint8_t *mac) {
    if (g_wifi || esp_now_is_peer_exist(mac)) return;
    esp_now_peer_info_t p = {};
    memcpy(p.peer_addr, mac, 6);
    p.channel = kChannel;
    p.encrypt = false;
    esp_now_add_peer(&p);
}

static int upsertPeer(const uint8_t *mac, uint32_t ip) {
    int i = peerIndex(mac);
    if (i < 0) {
        uint32_t now = millis();
        for (uint8_t k = 0; k < kMaxPeers; k++) {
            if (!g_peer[k].used || now - g_peer[k].lastSeen > kForgetMs) {
                i = k;
                break;
            }
        }
        if (i < 0) return -1;
        memset(&g_peer[i], 0, sizeof(Peer));
        memcpy(g_peer[i].mac, mac, 6);
        g_peer[i].used = true;
        addRadioPeer(mac);
        Serial.printf("[sync] found %s\n", macStr(mac).c_str());
    }
    if (ip) g_peer[i].ip = ip;
    return i;
}

// --------------------------------------------------------------- transport

static bool rawSend(const uint8_t *mac, const uint8_t *buf, size_t len) {
    if (!g_wifi) return esp_now_send(mac, buf, len) == ESP_OK;
    int i = peerIndex(mac);
    if (i < 0 || !g_udpUp) return false;
    return g_udp.writeTo((uint8_t *)buf, len, IPAddress(g_peer[i].ip), kUdpPort) == len;
}

static void rawBroadcast(const uint8_t *buf, size_t len) {
    if (!g_wifi) {
        esp_now_send(kBroadcast, buf, len);
    } else if (g_udpUp) {
        g_udp.broadcastTo((uint8_t *)buf, len, kUdpPort);
    }
}

// The one receive path for both transports.
static void onFrame(const uint8_t *mac, uint32_t ip, const uint8_t *d, size_t len) {
    if (len < sizeof(Hdr) || len > 250) return;
    Hdr h;
    memcpy(&h, d, sizeof(h));

    if (h.type == T_ACK) {
        g_ackSeq = h.seq;
        xSemaphoreGive(g_ackSem);
        return;
    }

    Rx r;
    memcpy(r.mac, mac, 6);
    r.ip = ip;
    r.len = (uint8_t)len;
    memcpy(r.data, d, len);

    if (h.type == T_HELLO) {
        xQueueSend(g_q, &r, 0);
        return;
    }

    // Everything else is acknowledged, and only from a board we have heard a hello
    // from — it is how we know where to send the acknowledgement.
    int pi = peerIndex(mac);
    if (pi < 0) return;
    Hdr ack = {T_ACK, h.seq};
    if (g_peer[pi].rxSeqValid && g_peer[pi].lastRxSeq == h.seq) {
        rawSend(mac, (const uint8_t *)&ack, sizeof(ack));  // a resend: ack again, act once
        return;
    }
    if (xQueueSend(g_q, &r, 0) != pdTRUE) return;  // full: stay silent, the sender retries
    g_peer[pi].lastRxSeq = h.seq;
    g_peer[pi].rxSeqValid = true;
    rawSend(mac, (const uint8_t *)&ack, sizeof(ack));
}

static void onRadio(const esp_now_recv_info_t *info, const uint8_t *d, int len) {
    if (len > 0) onFrame(info->src_addr, 0, d, (size_t)len);
}

static void onUdp(AsyncUDPPacket &p) {
    const uint8_t *d = p.data();
    size_t len = p.length();
    uint32_t ip = (uint32_t)p.remoteIP();
    if (len < sizeof(Hdr)) return;

    uint8_t mac[6];
    if (d[0] == T_HELLO && len >= sizeof(Hdr) + sizeof(Hello)) {
        memcpy(mac, d + sizeof(Hdr), 6);
        if (memcmp(mac, g_mac, 6) == 0) return;  // our own broadcast, looped back
    } else {
        int pi = peerByIp(ip);
        if (pi < 0) return;
        memcpy(mac, g_peer[pi].mac, 6);
    }
    onFrame(mac, ip, d, len);
}

// Sends and waits for the acknowledgement, retrying. Strictly one frame in flight,
// which at ~45 KB/s over ESP-NOW is plenty and keeps the whole thing simple.
static bool rsend(const uint8_t *mac, uint8_t type, const void *payload, size_t len) {
    if (len > 244) return false;
    uint8_t buf[250];
    uint16_t s = ++g_txSeq;
    if (s == 0) s = ++g_txSeq;
    Hdr h = {type, s};
    memcpy(buf, &h, sizeof(h));
    if (len) memcpy(buf + sizeof(h), payload, len);

    for (uint8_t t = 0; t < kTries; t++) {
        while (xSemaphoreTake(g_ackSem, 0) == pdTRUE) {
        }
        if (!rawSend(mac, buf, sizeof(h) + len)) {
            vTaskDelay(pdMS_TO_TICKS(3));
            continue;
        }
        uint32_t t0 = millis();
        while (millis() - t0 < g_ackMs) {
            if (xSemaphoreTake(g_ackSem, pdMS_TO_TICKS(g_ackMs)) != pdTRUE) break;
            if (g_ackSeq == s) return true;
        }
    }
    return false;
}

// ------------------------------------------------------------------ session

static void sendList(const uint8_t *mac) {
    uint16_t n = Store::snapshot(g_lsnap, kMaxRecs);
    uint8_t total = (uint8_t)((n + kRecsPerPacket - 1) / kRecsPerPacket);
    if (total == 0) total = 1;
    for (uint8_t i = 0; i < total; i++) {
        uint8_t pl[3 + kRecsPerPacket * sizeof(Store::Record)];
        int from = (int)i * kRecsPerPacket;
        uint8_t cnt = (uint8_t)constrain((int)n - from, 0, (int)kRecsPerPacket);
        pl[0] = total;
        pl[1] = i;
        pl[2] = cnt;
        if (cnt) memcpy(pl + 3, &g_lsnap[from], cnt * sizeof(Store::Record));
        if (!rsend(mac, T_LIST, pl, 3 + cnt * sizeof(Store::Record))) return;
    }
}

static bool streamMessage(const uint8_t *mac, uint32_t id) {
    File f;
    uint32_t size = Store::exportOpen(id, f);
    if (!size) return false;

    uint8_t pl[8];
    memcpy(pl, &id, 4);
    memcpy(pl + 4, &size, 4);
    if (!rsend(mac, T_PUSH, pl, 8)) {
        f.close();
        return false;
    }

    uint8_t chunk[8 + kChunk];
    uint32_t off = 0;
    bool ok = true;
    while (off < size) {
        int n = f.read(chunk + 8, kChunk);
        if (n <= 0) {
            ok = false;
            break;
        }
        memcpy(chunk, &id, 4);
        memcpy(chunk + 4, &off, 4);
        if (!rsend(mac, T_DATA, chunk, 8 + n)) {
            ok = false;
            break;
        }
        off += n;
    }
    f.close();
    return ok && rsend(mac, T_DONE, &id, 4);
}

static void handle(const Rx &r) {
    uint8_t type = r.data[0];
    const uint8_t *p = r.data + sizeof(Hdr);
    size_t n = r.len - sizeof(Hdr);

    if (type == T_HELLO) {
        if (n < sizeof(Hello)) return;
        Hello hl;
        memcpy(&hl, p, sizeof(hl));
        if (memcmp(hl.mac, g_mac, 6) == 0) return;
        int pi = upsertPeer(hl.mac, r.ip);
        if (pi < 0) return;
        g_peer[pi].hash = hl.hash;
        g_peer[pi].count = hl.count;
        g_peer[pi].lastSeen = millis();
        return;
    }

    if (peerIndex(r.mac) < 0) return;

    switch (type) {
        case T_LISTREQ:
            sendList(r.mac);
            break;

        case T_LIST:
            if (g_expectList && n >= 3) {
                uint8_t total = p[0], idx = p[1], cnt = p[2];
                if (total == 0 || total > 16 || idx >= total || cnt > kRecsPerPacket ||
                    n < 3u + cnt * sizeof(Store::Record)) {
                    break;
                }
                uint16_t at = (uint16_t)idx * kRecsPerPacket;
                if (at + cnt <= kMaxRecs) {
                    memcpy(&g_rlist[at], p + 3, cnt * sizeof(Store::Record));
                    g_rlistTotal = total;
                    g_rlistGot |= 1u << idx;
                    if (at + cnt > g_rlistN) g_rlistN = at + cnt;
                }
            }
            break;

        case T_NEED:
            if (n >= 4) {
                uint32_t id;
                memcpy(&id, p, 4);
                streamMessage(r.mac, id);
            }
            break;

        case T_PUSH:
            if (n >= 8) {
                uint32_t id, size;
                memcpy(&id, p, 4);
                memcpy(&size, p + 4, 4);
                g_curImp = Store::importBegin(id, size) ? id : 0;
            }
            break;

        case T_DATA:
            if (n > 8) {
                uint32_t id, off;
                memcpy(&id, p, 4);
                memcpy(&off, p + 4, 4);
                if (id == g_curImp && !Store::importWrite(off, p + 8, n - 8)) {
                    Store::importAbort();
                    g_curImp = 0;
                }
            }
            break;

        case T_DONE:
            if (n >= 4) {
                uint32_t id;
                memcpy(&id, p, 4);
                if (id == g_curImp) {
                    bool ok = Store::importEnd();
                    g_doneId = id;
                    g_curImp = 0;
                    if (ok) Serial.printf("[sync] received %08lX\n", (unsigned long)id);
                }
            }
            break;

        case T_HEARD:
            if (n >= 1) {
                uint8_t cnt = p[0];
                for (uint8_t i = 0; i < cnt && 1u + 6u * (i + 1) <= n; i++) {
                    uint32_t id;
                    uint16_t heard;
                    memcpy(&id, p + 1 + 6 * i, 4);
                    memcpy(&heard, p + 5 + 6 * i, 2);
                    Store::mergeHeard(id, heard);
                }
            }
            break;

        case T_DEL:
            if (n >= 1) {
                uint8_t cnt = p[0];
                for (uint8_t i = 0; i < cnt && 1u + 4u * (i + 1) <= n; i++) {
                    uint32_t id;
                    memcpy(&id, p + 1 + 4 * i, 4);
                    Store::applyDelete(id);
                }
            }
            break;
    }
}

static bool popRx(Rx &r, uint32_t ms) { return xQueueReceive(g_q, &r, pdMS_TO_TICKS(ms)) == pdTRUE; }

// Handles whatever arrives until `done` or until nothing has arrived for idleMs.
static bool pump(uint32_t idleMs, bool (*done)()) {
    uint32_t last = millis();
    Rx r;
    while (millis() - last < idleMs) {
        if (done()) return true;
        if (popRx(r, 40)) {
            handle(r);
            last = millis();
        }
    }
    return done();
}

static bool listComplete() {
    if (g_rlistTotal == 0) return false;
    uint32_t want = (1u << g_rlistTotal) - 1;
    return (g_rlistGot & want) == want;
}

static bool importFinished() { return g_doneId == g_waitId; }

static const Store::Record *inList(const Store::Record *list, uint16_t n, uint32_t id) {
    for (uint16_t i = 0; i < n; i++) {
        if (list[i].id == id) return &list[i];
    }
    return nullptr;
}

static void runSession(uint8_t pi) {
    uint8_t mac[6];
    memcpy(mac, g_peer[pi].mac, 6);
    uint32_t t0 = millis();
    uint32_t changesBefore = Store::changeCounter();
    g_peer[pi].lastSession = t0;

    g_rlistN = 0;
    g_rlistGot = 0;
    g_rlistTotal = 0;
    g_expectList = true;
    bool ok = rsend(mac, T_LISTREQ, nullptr, 0) && pump(2500, listComplete);
    g_expectList = false;
    if (!ok) {
        Serial.printf("[sync] %s: no list, will retry\n", macStr(mac).c_str());
        g_peer[pi].backoffUntil = millis() + 4000;
        return;
    }

    uint16_t nLocal = Store::snapshot(g_lsnap, kMaxRecs);

    uint32_t need[3], push[3];
    uint8_t nNeed = 0, nPush = 0;
    uint8_t delBuf[1 + 4 * 30];
    uint8_t delN = 0;
    uint8_t heardBuf[1 + 6 * 30];
    uint8_t heardN = 0;

    // What they have that we do not, and what we know that they do not.
    for (uint16_t i = 0; i < g_rlistN; i++) {
        const Store::Record &r = g_rlist[i];
        if (r.deleted) {
            Store::applyDelete(r.id);
            continue;
        }
        const Store::Record *l = inList(g_lsnap, nLocal, r.id);
        if (l && !l->deleted) {
            uint16_t merged = l->heard | r.heard;
            if (merged != l->heard) Store::mergeHeard(r.id, r.heard);
            if (merged != r.heard && heardN < 30) {
                memcpy(heardBuf + 1 + 6 * heardN, &r.id, 4);
                memcpy(heardBuf + 5 + 6 * heardN, &merged, 2);
                heardN++;
            }
        } else if (Store::isTombstoned(r.id)) {
            if (delN < 30) {
                memcpy(delBuf + 1 + 4 * delN, &r.id, 4);
                delN++;
            }
        } else if (nNeed < 3) {
            need[nNeed++] = r.id;
        }
    }
    for (uint16_t i = 0; i < nLocal; i++) {
        const Store::Record &l = g_lsnap[i];
        if (inList(g_rlist, g_rlistN, l.id)) continue;
        if (l.deleted) {
            // They never had it, but they still need the tombstone, or a third board
            // could hand it to them later and the fingerprints would never agree.
            if (delN < 30) {
                memcpy(delBuf + 1 + 4 * delN, &l.id, 4);
                delN++;
            }
        } else if (nPush < 3) {
            push[nPush++] = l.id;
        }
    }

    if (delN) {
        delBuf[0] = delN;
        rsend(mac, T_DEL, delBuf, 1 + 4 * delN);
    }
    if (heardN) {
        heardBuf[0] = heardN;
        rsend(mac, T_HEARD, heardBuf, 1 + 6 * heardN);
    }
    for (uint8_t i = 0; i < nNeed; i++) {
        g_waitId = need[i];
        g_doneId = 0;
        if (!rsend(mac, T_NEED, &need[i], 4)) break;
        pump(2500, importFinished);
        if (g_curImp) {  // cut off part way
            Store::importAbort();
            g_curImp = 0;
        }
    }
    for (uint8_t i = 0; i < nPush; i++) {
        if (!streamMessage(mac, push[i])) break;
        Serial.printf("[sync] sent %08lX\n", (unsigned long)push[i]);
    }

    Serial.printf("[sync] %s: pulled %u, pushed %u, heard %u, deleted %u  (%u ms)\n",
                  macStr(mac).c_str(), nNeed, nPush, heardN, delN, (unsigned)(millis() - t0));

    // If nothing at all changed and the fingerprints still differ, there is something
    // neither side can resolve (a full disk, say). Do not hammer the radio over it.
    if (Store::changeCounter() == changesBefore && nNeed == 0 && nPush == 0) {
        g_peer[pi].backoffUntil = millis() + 10000;
    }
}

// --------------------------------------------------------------------- task

static void sendHello(uint32_t hash) {
    uint8_t buf[sizeof(Hdr) + sizeof(Hello)];
    Hdr h = {T_HELLO, 0};
    Hello hl;
    memcpy(hl.mac, g_mac, 6);
    hl.hash = hash;
    hl.count = Store::count();
    memcpy(buf, &h, sizeof(h));
    memcpy(buf + sizeof(h), &hl, sizeof(hl));
    rawBroadcast(buf, sizeof(buf));
}

static void syncTask(void *) {
    uint32_t lastHello = 0, lastChange = 0xFFFFFFFF;
    for (;;) {
        // In Wi-Fi mode there is nothing to do until the router has given us an address.
        if (g_wifi && !g_udpUp) {
            if (Portal::wifiConnected() && g_udp.listen(kUdpPort)) {
                g_udp.onPacket(onUdp);
                g_udpUp = true;
                Serial.printf("[sync] listening on UDP %u\n", kUdpPort);
            } else {
                vTaskDelay(pdMS_TO_TICKS(500));
                continue;
            }
        }

        Rx r;
        while (popRx(r, 15)) handle(r);

        uint32_t now = millis();
        uint32_t changes = Store::changeCounter();
        uint32_t mine = Store::stateHash();
        bool changed = changes != lastChange;
        if (now - lastHello >= kHelloMs || (changed && now - lastHello >= 250)) {
            lastChange = changes;
            lastHello = now;
            sendHello(mine);
        }

        for (uint8_t i = 0; i < kMaxPeers; i++) {
            Peer &pr = g_peer[i];
            if (!pr.used) continue;
            bool online = now - pr.lastSeen < kOnlineMs;
            bool iLead = memcmp(g_mac, pr.mac, 6) < 0;
            if (online && iLead && pr.hash != mine && now - pr.lastSession > 1500 &&
                (int32_t)(now - pr.backoffUntil) > 0) {
                runSession(i);
                break;  // the state changed; start over
            }
        }
    }
}

// ----------------------------------------------------------------- public API

bool begin() {
    g_q = xQueueCreate(24, sizeof(Rx));
    g_ackSem = xSemaphoreCreateBinary();
    if (!g_q || !g_ackSem) return false;

    esp_read_mac(g_mac, ESP_MAC_WIFI_STA);
    g_wifi = Portal::mode() == Portal::MODE_WIFI;
    g_ackMs = g_wifi ? 80 : 25;  // a router adds a few milliseconds each way
    g_txSeq = (uint16_t)esp_random();
    memset(g_peer, 0, sizeof(g_peer));

    if (!g_wifi) {
        if (esp_now_init() != ESP_OK) {
            Serial.println("  ESP-NOW failed to start");
            return false;
        }
        esp_now_register_recv_cb(onRadio);
        esp_now_peer_info_t p = {};
        memcpy(p.peer_addr, kBroadcast, 6);
        p.channel = kChannel;
        esp_now_add_peer(&p);
    }

    xTaskCreatePinnedToCore(syncTask, "sync", 12288, nullptr, 2, nullptr, 0);
    Serial.printf("  my MAC %s, syncing over %s\n", macStr(g_mac).c_str(),
                  g_wifi ? "WiFi (UDP broadcast)" : "ESP-NOW");
    return true;
}

uint8_t peersOnline() {
    uint8_t n = 0;
    uint32_t now = millis();
    for (uint8_t i = 0; i < kMaxPeers; i++) {
        if (g_peer[i].used && now - g_peer[i].lastSeen < kOnlineMs) n++;
    }
    return n;
}

void print() {
    Serial.printf("  sync over %s, my hash %08lX\n", g_wifi ? "WiFi" : "ESP-NOW",
                  (unsigned long)Store::stateHash());
    uint32_t now = millis();
    for (uint8_t i = 0; i < kMaxPeers; i++) {
        const Peer &p = g_peer[i];
        if (!p.used) continue;
        bool on = now - p.lastSeen < kOnlineMs;
        Serial.printf("    %s  %s  hash %08lX  %u msgs  %s%s\n", macStr(p.mac).c_str(),
                      on ? "online " : "offline", (unsigned long)p.hash, p.count,
                      g_wifi ? IPAddress(p.ip).toString().c_str() : "",
                      memcmp(g_mac, p.mac, 6) < 0 ? "  (I lead)" : "");
    }
}

}  // namespace Sync
