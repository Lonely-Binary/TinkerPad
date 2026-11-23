#include "portal.h"

#include <DNSServer.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_mac.h>
#include <esp_wifi.h>

#include "store.h"
#include "sync.h"

namespace Portal {

static const uint32_t kHotspotMs = 15UL * 60UL * 1000UL;

// ESP-NOW and the hotspot share one radio, so they must share one channel. In Wi-Fi
// mode the router decides the channel and the hotspot follows it.
static const uint8_t kChannel = 1;

static WebServer g_web(80);
static DNSServer g_dns;
static Mode g_mode = MODE_ESPNOW;
static String g_ssid, g_pass;
static bool g_apOn = false;
static uint32_t g_apUntil = 0;
static char g_apName[24];

static void load() {
    Preferences p;
    if (p.begin("vbnet", true)) {
        g_mode = (Mode)p.getUChar("mode", MODE_ESPNOW);
        g_ssid = p.getString("ssid", "");
        g_pass = p.getString("pass", "");
        p.end();
    }
    if (g_mode != MODE_WIFI) g_mode = MODE_ESPNOW;
}

static void save(Mode m, const String &ssid, const String &pass) {
    Preferences p;
    if (p.begin("vbnet", false)) {
        p.putUChar("mode", m);
        p.putString("ssid", ssid);
        p.putString("pass", pass);
        p.end();
    }
}

static String esc(const String &s) {
    String o;
    for (char c : s) {
        if (c == '<') o += "&lt;";
        else if (c == '>') o += "&gt;";
        else if (c == '&') o += "&amp;";
        else if (c == '"') o += "&quot;";
        else o += c;
    }
    return o;
}

static const char *kHead =
    "<!doctype html><html><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>VoiceBoard</title><style>"
    "body{font-family:-apple-system,sans-serif;margin:0;padding:16px;background:#f4f6f6;color:#1d2b2c}"
    "h1{font-size:22px;margin:4px 0 12px}"
    ".card{background:#fff;border-radius:12px;padding:16px;margin-bottom:14px;box-shadow:0 1px 3px #0001}"
    "label{display:block;margin:10px 0 4px;font-weight:600}"
    "select,input{width:100%;box-sizing:border-box;font-size:17px;padding:10px;border:1px solid #c9d3d3;border-radius:8px}"
    ".opt{display:flex;gap:10px;align-items:center;margin:8px 0;font-size:17px}.opt input{width:auto}"
    "button{width:100%;font-size:18px;padding:12px;margin-top:16px;border:0;border-radius:10px;background:#117A80;color:#fff}"
    ".muted{color:#5d6b6c;font-size:14px}"
    "</style></head><body>";

static void handleRoot() {
    // A scan takes a couple of seconds and briefly takes the radio off-channel, which
    // ESP-NOW rides out with its retries.
    int n = WiFi.scanNetworks();

    String h = kHead;
    h += "<h1>VoiceBoard settings</h1>";

    h += "<div class='card'><div class='muted'>Status</div>";
    h += "<div>This board: " + String(g_apName) + "</div>";
    h += "<div>Sync over: " + String(g_mode == MODE_WIFI ? "WiFi" : "ESP-NOW") + "</div>";
    if (g_mode == MODE_WIFI) {
        h += "<div>Wi-Fi: " + (WiFi.status() == WL_CONNECTED
                                 ? esc(g_ssid) + ", connected (" + WiFi.localIP().toString() + ")"
                                 : esc(g_ssid) + ", not connected") +
             "</div>";
    }
    h += "<div>Boards in sync range: " + String(Sync::peersOnline()) + "</div>";
    h += "<div>Messages: " + String(Store::count()) + "</div></div>";

    h += "<form class='card' method='post' action='/save'>";
    h += "<label>How boards find each other</label>";
    h += "<div class='opt'><input type='radio' name='mode' value='0' id='m0'";
    h += g_mode == MODE_ESPNOW ? " checked" : "";
    h += "><span>ESP-NOW (default, no router needed)</span></div>";
    h += "<div class='opt'><input type='radio' name='mode' value='1' id='m1'";
    h += g_mode == MODE_WIFI ? " checked" : "";
    h += "><span>Wi-Fi (boards on the same Wi-Fi network)</span></div>";

    h += "<label>Wi-Fi network</label><select name='ssid'>";
    bool listedCurrent = false;
    for (int i = 0; i < n; i++) {
        String s = WiFi.SSID(i);
        if (s.isEmpty()) continue;
        bool sel = s == g_ssid;
        listedCurrent |= sel;
        h += "<option value=\"" + esc(s) + "\"" + (sel ? " selected" : "") + ">" + esc(s) +
             " (" + String(WiFi.RSSI(i)) + " dBm)</option>";
    }
    if (!listedCurrent && g_ssid.length()) {
        h += "<option value=\"" + esc(g_ssid) + "\" selected>" + esc(g_ssid) + "</option>";
    }
    h += "</select>";
    h += "<label>Wi-Fi password</label><input type='password' name='pass' value=\"" + esc(g_pass) + "\">";
    h += "<div class='muted'>Only used when Wi-Fi is selected.</div>";
    h += "<button type='submit'>Save and restart</button></form>";

    h += "<div class='muted'>This settings hotspot closes 15 minutes after power-on. "
         "In Wi-Fi mode it stays open until the board connects, so a wrong password "
         "can always be fixed.</div>";
    h += "</body></html>";

    WiFi.scanDelete();
    g_web.send(200, "text/html; charset=utf-8", h);
}

static void handleSave() {
    Mode m = g_web.arg("mode") == "1" ? MODE_WIFI : MODE_ESPNOW;
    String ssid = g_web.arg("ssid");
    String pass = g_web.arg("pass");
    save(m, ssid, pass);

    String h = kHead;
    h += "<h1>Saved</h1><div class='card'>The board is restarting. ";
    if (m == MODE_WIFI) {
        h += "It will join the Wi-Fi network \"" + esc(ssid) + "\". Once connected, this hotspot "
             "closes within 15 minutes. If it cannot connect, the hotspot stays open: come "
             "back here to fix the settings.";
    } else {
        h += "It will sync with nearby boards over ESP-NOW.";
    }
    h += "</div></body></html>";
    g_web.send(200, "text/html; charset=utf-8", h);
    Serial.printf("[portal] saved: mode %s, ssid \"%s\" — restarting\n",
                  m == MODE_WIFI ? "WiFi" : "ESP-NOW", ssid.c_str());
    delay(800);
    ESP.restart();
}

void begin() {
    load();

    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(g_apName, sizeof(g_apName), "VoiceBoard-%02X%02X", mac[4], mac[5]);

    WiFi.persistent(false);
    WiFi.mode(WIFI_AP_STA);
    esp_wifi_set_ps(WIFI_PS_NONE);  // a receiver that dozes misses ESP-NOW frames
    WiFi.softAP(g_apName, nullptr, kChannel);
    g_apOn = true;
    g_apUntil = millis() + kHotspotMs;

    // Any name resolves to the board, so a phone's "sign in to network" page opens
    // the settings by itself.
    g_dns.start(53, "*", WiFi.softAPIP());
    g_web.on("/", HTTP_GET, handleRoot);
    g_web.on("/save", HTTP_POST, handleSave);
    g_web.onNotFound([]() {
        g_web.sendHeader("Location", "http://192.168.4.1/", true);
        g_web.send(302, "text/plain", "");
    });
    g_web.begin();

    if (g_mode == MODE_WIFI && g_ssid.length()) {
        WiFi.setAutoReconnect(true);
        WiFi.begin(g_ssid.c_str(), g_pass.c_str());
    } else {
        esp_wifi_set_channel(kChannel, WIFI_SECOND_CHAN_NONE);
    }

    Serial.printf("  hotspot \"%s\" for 15 min, settings at http://192.168.4.1\n", g_apName);
    Serial.printf("  mode %s%s%s\n", g_mode == MODE_WIFI ? "WiFi, joining \"" : "ESP-NOW",
                  g_mode == MODE_WIFI ? g_ssid.c_str() : "", g_mode == MODE_WIFI ? "\"" : "");
}

void closeHotspot() {
    if (!g_apOn) return;
    g_web.stop();
    g_dns.stop();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    if (g_mode == MODE_ESPNOW) esp_wifi_set_channel(kChannel, WIFI_SECOND_CHAN_NONE);
    g_apOn = false;
    Serial.println("[portal] hotspot closed");
}

void loop() {
    static bool wasConnected = false;
    bool connected = wifiConnected();
    if (g_mode == MODE_WIFI && connected != wasConnected) {
        wasConnected = connected;
        if (connected) {
            Serial.printf("[portal] WiFi connected, %s, channel %u\n",
                          WiFi.localIP().toString().c_str(), WiFi.channel());
        } else {
            Serial.println("[portal] WiFi lost");
        }
    }

    if (!g_apOn) return;
    g_dns.processNextRequest();
    g_web.handleClient();

    // Wi-Fi mode without a working connection keeps the hotspot, or there would be no
    // way left to fix a mistyped password.
    bool stuck = g_mode == MODE_WIFI && !connected;
    if ((int32_t)(millis() - g_apUntil) > 0 && !stuck) closeHotspot();
}

Mode mode() { return g_mode; }
bool wifiConnected() { return g_mode == MODE_WIFI && WiFi.status() == WL_CONNECTED; }
bool hotspotOn() { return g_apOn; }

void print() {
    Serial.printf("  network: %s", g_mode == MODE_WIFI ? "WiFi" : "ESP-NOW");
    if (g_mode == MODE_WIFI) {
        Serial.printf(", \"%s\" %s", g_ssid.c_str(),
                      wifiConnected() ? WiFi.localIP().toString().c_str() : "not connected");
    }
    Serial.printf(", hotspot %s", g_apOn ? g_apName : "off");
    if (g_apOn) Serial.printf(" (%lu s left)", (unsigned long)((int32_t)(g_apUntil - millis()) > 0 ? (g_apUntil - millis()) / 1000 : 0));
    Serial.println();
}

}  // namespace Portal
