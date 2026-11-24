// MacroPad - a 16-key Bluetooth shortcut keyboard, set up from a web page
// Board: ESP32-S3 N16R8 (16 MB flash, 8 MB PSRAM)
// Setup guide: see ../../01_HardwareTest/README.md
//
// How to use
//   1. Power on. The board makes its own Wi-Fi network (AP mode):
//        name      TinkerPad-XXXX   (XXXX is different for every board)
//        password  12345678
//   2. Join it with a phone or computer. The settings page usually opens by
//      itself; if not, open http://192.168.4.1
//   3. Tap a key on the page and give it a shortcut:
//        computer -> just press the shortcut on your keyboard
//        phone    -> pick the modifiers and the key from the lists
//   4. Pair "TinkerPad" over Bluetooth on your Mac or PC. Each key now sends
//      its shortcut.
//
// STA mode: on the page, under Wi-Fi, enter your router's name and password.
// The board then joins your router and the page is at http://tinkerpad.local
// If it cannot join the router, it goes back to AP mode by itself.
// Hold key 1 (top-left) while powering on to force AP mode.
//
// Needs the "FastLED" library. Everything else comes with the ESP32 core.

#include <Wire.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <FastLED.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEHIDDevice.h>
#include <BLESecurity.h>
#include "web_page.h"

// ---------- Pins ----------
#define PIN_SDA       9
#define PIN_SCL       8
#define PCF8574_ADDR  0x20
#define PIN_LED       38
#define NUM_KEYS      16
#define BRIGHTNESS    40

#define DEFAULT_AP_PASSWORD  "12345678"

// ---------- One shortcut ----------
// modifiers: bit 0 Ctrl, bit 1 Shift, bit 2 Alt/Option, bit 3 Win/Command
// key:       HID key code (0 = none), e.g. 0x04 = A
// media:     HID consumer code (0 = none), e.g. 0xE9 = volume up
struct Shortcut {
    uint8_t  modifiers;
    uint8_t  key;
    uint16_t media;
};

Shortcut shortcuts[NUM_KEYS];
String platform = "mac";            // "mac" or "win" - only changes the labels on the page
String wifiMode = "ap";             // "ap" or "sta"
String wifiSsid, wifiPass, apPass;
String apName;
bool   staFailed = false;           // true when the router could not be joined

Preferences prefs;                  // settings saved in flash
WebServer   server(80);
DNSServer   dns;                    // makes the page open by itself in AP mode
CRGB        leds[NUM_KEYS];

// ---------- Bluetooth keyboard ----------

// Describes our two reports to the computer:
//   report 1 = keyboard (modifiers + up to 6 keys)
//   report 2 = media keys (volume, play/pause ...)
const uint8_t reportMap[] = {
    0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x85, 0x01,        // Keyboard, report ID 1
    0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00,        //   8 modifier bits
    0x25, 0x01, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02,
    0x95, 0x01, 0x75, 0x08, 0x81, 0x01,                    //   1 reserved byte
    0x95, 0x05, 0x75, 0x01, 0x05, 0x08, 0x19, 0x01,        //   5 LED bits (output)
    0x29, 0x05, 0x91, 0x02, 0x95, 0x01, 0x75, 0x03, 0x91, 0x01,
    0x95, 0x06, 0x75, 0x08, 0x15, 0x00, 0x26, 0xFF, 0x00,  //   6 key codes
    0x05, 0x07, 0x19, 0x00, 0x29, 0xFF, 0x81, 0x00,
    0xC0,
    0x05, 0x0C, 0x09, 0x01, 0xA1, 0x01, 0x85, 0x02,        // Media keys, report ID 2
    0x15, 0x00, 0x26, 0xFF, 0x03, 0x19, 0x00, 0x2A, 0xFF, 0x03,
    0x75, 0x10, 0x95, 0x01, 0x81, 0x00,
    0xC0,
};

BLEHIDDevice      *hid;
BLECharacteristic *keyboardReport;
BLECharacteristic *mediaReport;
bool bleConnected = false;

class ConnectionCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer *s) {
        bleConnected = true;
        Serial.println("Bluetooth connected");
    }
    void onDisconnect(BLEServer *s) {
        bleConnected = false;
        Serial.println("Bluetooth disconnected");
        BLEDevice::startAdvertising();      // let the computer find us again
    }
};

void startBluetooth() {
    BLEDevice::init("TinkerPad");

    // "Just works" pairing: no PIN, and the pairing is remembered
    BLESecurity *security = new BLESecurity();
    security->setCapability(ESP_IO_CAP_NONE);
    security->setAuthenticationMode(true, false, true);

    BLEServer *bleServer = BLEDevice::createServer();
    bleServer->setCallbacks(new ConnectionCallbacks());

    hid = new BLEHIDDevice(bleServer);
    hid->manufacturer()->setValue("Lonely Binary");
    hid->pnp(0x02, 0x303A, 0x4C50, 0x0100);
    hid->hidInfo(0x00, 0x01);
    hid->reportMap((uint8_t *)reportMap, sizeof(reportMap));
    keyboardReport = hid->inputReport(1);
    hid->outputReport(1);                   // caps-lock LEDs (we ignore them)
    mediaReport = hid->inputReport(2);
    hid->setBatteryLevel(100);
    hid->startServices();

    BLEAdvertising *adv = BLEDevice::getAdvertising();
    adv->setAppearance(0x03C1);             // looks like a keyboard
    adv->addServiceUUID(hid->hidService()->getUUID());
    adv->setScanResponse(true);
    BLEDevice::startAdvertising();
}

// Press (down = true) or release (down = false) a shortcut
void sendShortcut(const Shortcut &s, bool down) {
    if (!bleConnected) return;

    if (s.media) {
        uint8_t report[2] = {0, 0};
        if (down) {
            report[0] = s.media & 0xFF;
            report[1] = s.media >> 8;
        }
        mediaReport->setValue(report, 2);
        mediaReport->notify();
    } else {
        uint8_t report[8] = {0};            // modifiers, reserved, 6 keys
        if (down) {
            report[0] = s.modifiers;
            report[2] = s.key;
        }
        keyboardReport->setValue(report, 8);
        keyboardReport->notify();
    }
}

// ---------- Keys and LEDs ----------

// Returns the number (0-15) of a pressed key, or -1 if no key is pressed
int findKey() {
    for (int row = 0; row < 4; row++) {
        Wire.beginTransmission(PCF8574_ADDR);
        Wire.write(0xF0 | (0x0F & ~(1 << row)));    // pull this row low
        Wire.endTransmission();

        Wire.requestFrom(PCF8574_ADDR, 1);
        uint8_t pins = Wire.read();
        for (int col = 0; col < 4; col++) {
            if ((pins & (1 << (col + 4))) == 0) {
                return row * 4 + col;
            }
        }
    }
    return -1;
}

void showKey(int key) {
    FastLED.clear();
    if (key >= 0) {
        // Teal when the shortcut is sent, red when Bluetooth is not connected
        leds[key] = bleConnected ? CRGB(0x14, 0x89, 0x8F) : CRGB::Red;
    }
    FastLED.show();
}

// ---------- Saving settings ----------

void loadSettings() {
    prefs.begin("macropad", false);
    memset(shortcuts, 0, sizeof(shortcuts));
    prefs.getBytes("keys", shortcuts, sizeof(shortcuts));
    platform = prefs.getString("platform", "mac");
    wifiMode = prefs.getString("mode", "ap");
    wifiSsid = prefs.getString("ssid", "");
    wifiPass = prefs.getString("pass", "");
    apPass   = prefs.getString("appass", DEFAULT_AP_PASSWORD);
}

// ---------- Wi-Fi ----------

void startAP() {
    WiFi.mode(WIFI_AP);
    WiFi.softAP(apName.c_str(), apPass.c_str());
    dns.start(53, "*", WiFi.softAPIP());    // every web address leads to us
    Serial.printf("AP mode: join \"%s\" (password %s), open http://%s\n",
                  apName.c_str(), apPass.c_str(), WiFi.softAPIP().toString().c_str());
}

void startWiFi(bool forceAP) {
    // Network name ends with the last 4 digits of this board's MAC address
    uint64_t mac = ESP.getEfuseMac();
    char name[16];
    snprintf(name, sizeof(name), "TinkerPad-%02X%02X",
             (uint8_t)(mac >> 32), (uint8_t)(mac >> 40));
    apName = name;

    if (forceAP || wifiMode != "sta" || wifiSsid.length() == 0) {
        startAP();
    } else {
        Serial.printf("STA mode: joining \"%s\"", wifiSsid.c_str());
        WiFi.mode(WIFI_STA);
        WiFi.begin(wifiSsid.c_str(), wifiPass.c_str());
        for (int i = 0; i < 30 && WiFi.status() != WL_CONNECTED; i++) {
            delay(500);
            Serial.print(".");
        }
        Serial.println();
        if (WiFi.status() == WL_CONNECTED) {
            Serial.printf("Connected. Open http://tinkerpad.local or http://%s\n",
                          WiFi.localIP().toString().c_str());
        } else {
            Serial.println("Could not join the router - back to AP mode");
            staFailed = true;
            WiFi.disconnect(true);
            startAP();
        }
    }
    MDNS.begin("tinkerpad");
    MDNS.addService("http", "tcp", 80);
}

// ---------- Web server ----------

// Put a text inside JSON quotes safely (a " or \ in a Wi-Fi name would break it)
String quoted(String text) {
    text.replace("\\", "\\\\");
    text.replace("\"", "\\\"");
    return "\"" + text + "\"";
}

// Everything the page needs, as JSON
void handleConfig() {
    String json = "{";
    json += "\"platform\":" + quoted(platform) + ",";
    json += "\"mode\":" + quoted(wifiMode) + ",";
    json += "\"ssid\":" + quoted(wifiSsid) + ",";
    json += "\"apName\":" + quoted(apName) + ",";
    json += "\"apPass\":" + quoted(apPass) + ",";
    json += "\"staFailed\":" + String(staFailed ? "true" : "false") + ",";
    json += "\"ble\":" + String(bleConnected ? "true" : "false") + ",";
    json += "\"keys\":[";
    for (int i = 0; i < NUM_KEYS; i++) {
        if (i) json += ",";
        json += "{\"m\":" + String(shortcuts[i].modifiers) +
                ",\"k\":" + String(shortcuts[i].key) +
                ",\"c\":" + String(shortcuts[i].media) + "}";
    }
    json += "]}";
    server.send(200, "application/json", json);
}

void handleStatus() {
    server.send(200, "application/json",
                String("{\"ble\":") + (bleConnected ? "true" : "false") + "}");
}

// Read m, k, c from the request
Shortcut shortcutFromRequest() {
    Shortcut s;
    s.modifiers = server.arg("m").toInt();
    s.key       = server.arg("k").toInt();
    s.media     = server.arg("c").toInt();
    return s;
}

// Save one key: i = key number, m/k/c = the shortcut
void handleSaveKey() {
    int i = server.arg("i").toInt();
    if (i < 0 || i >= NUM_KEYS) {
        server.send(400, "text/plain", "bad key");
        return;
    }
    shortcuts[i] = shortcutFromRequest();
    prefs.putBytes("keys", shortcuts, sizeof(shortcuts));
    server.send(200, "text/plain", "ok");
}

// Send a shortcut once, so the user can try it before saving
void handleTest() {
    Shortcut s = shortcutFromRequest();
    sendShortcut(s, true);
    delay(30);
    sendShortcut(s, false);
    server.send(200, "text/plain", bleConnected ? "sent" : "not connected");
}

void handlePlatform() {
    platform = server.arg("p") == "win" ? "win" : "mac";
    prefs.putString("platform", platform);
    server.send(200, "text/plain", "ok");
}

// Save the Wi-Fi settings, then restart to use them
void handleWiFi() {
    String mode = server.arg("mode") == "sta" ? "sta" : "ap";
    if (mode == "sta" && server.arg("ssid").length() == 0) {
        server.send(400, "text/plain", "Enter the router name");
        return;
    }
    if (server.arg("appass").length() > 0 && server.arg("appass").length() < 8) {
        server.send(400, "text/plain", "The AP password needs at least 8 characters");
        return;
    }
    prefs.putString("mode", mode);
    if (mode == "sta") {
        prefs.putString("ssid", server.arg("ssid"));
        prefs.putString("pass", server.arg("pass"));
    }
    if (server.arg("appass").length() > 0) {
        prefs.putString("appass", server.arg("appass"));
    }
    server.send(200, "text/plain", "ok");
    delay(500);
    ESP.restart();
}

// Any other address: in AP mode send the visitor to our page
void handleNotFound() {
    if (WiFi.getMode() == WIFI_AP) {
        server.sendHeader("Location", "http://192.168.4.1/");
        server.send(302, "text/plain", "");
    } else {
        server.send(404, "text/plain", "Not found");
    }
}

void startWebServer() {
    server.on("/", HTTP_GET, []() { server.send(200, "text/html", WEB_PAGE); });
    server.on("/api/config", HTTP_GET, handleConfig);
    server.on("/api/status", HTTP_GET, handleStatus);
    server.on("/api/key", HTTP_POST, handleSaveKey);
    server.on("/api/test", HTTP_POST, handleTest);
    server.on("/api/platform", HTTP_POST, handlePlatform);
    server.on("/api/wifi", HTTP_POST, handleWiFi);
    server.onNotFound(handleNotFound);
    server.begin();
}

// ---------- Main ----------

void setup() {
    Serial.begin(115200);
    delay(600);

    Wire.begin(PIN_SDA, PIN_SCL);
    Wire.setTimeOut(5);

    FastLED.addLeds<WS2812B, PIN_LED, GRB>(leds, NUM_KEYS);
    FastLED.setBrightness(BRIGHTNESS);
    FastLED.clear(true);

    loadSettings();

    bool forceAP = (findKey() == 0);        // key 1 (top-left) held at power-on
    if (forceAP) {
        prefs.putString("mode", "ap");
        wifiMode = "ap";
        Serial.println("Key 1 held: AP mode");
    }

    startBluetooth();
    startWiFi(forceAP);
    startWebServer();
}

void loop() {
    static int heldKey = -1;            // the key we are holding down for the computer
    static int lastRead = -1;           // what findKey() said last time
    static unsigned long changeTime = 0;

    dns.processNextRequest();
    server.handleClient();

    // A mechanical key "bounces" (flickers) for a few ms. Only believe a
    // reading once it has stayed the same for 20 ms.
    int key = findKey();
    if (key != lastRead) {
        lastRead = key;
        changeTime = millis();
    }
    if (millis() - changeTime >= 20 && key != heldKey) {
        // Press on key down, release on key up (holding a key repeats it)
        if (heldKey >= 0) sendShortcut(shortcuts[heldKey], false);
        heldKey = key;
        if (heldKey >= 0) sendShortcut(shortcuts[heldKey], true);
        showKey(heldKey);
    }

    delay(5);
}
