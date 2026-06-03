#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <esp_sleep.h>
#include <Preferences.h>

#include <GxEPD2_BW.h>
#include <gdey/GxEPD2_270_GDEY027T91.h>
#include <Fonts/FreeMonoBold9pt7b.h>
#include <Fonts/FreeMonoBold24pt7b.h>
#include <Fonts/FreeMono9pt7b.h>

#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <mbedtls/sha256.h>

#include <SparkFun_ATECCX08a_Arduino_Library.h>

#include "badge_pins.h"

// ── TCA9534 (buttons) ─────────────────────────────────────────────────────────
#define TCA9534_ADDR   0x20
#define TCA9534_INPUT  0x00
#define TCA9534_CONFIG 0x03

// ── Button masks (confirmed physical layout) ──────────────────────────────────
#define BTN_LEFT   (1 << 0)  // PB1
#define BTN_DOWN   (1 << 1)  // PB2
#define BTN_UP     (1 << 2)  // PB3
#define BTN_RIGHT  (1 << 3)  // PB4
#define BTN_SELECT (1 << 4)  // PB5
#define BTN_CANCEL (1 << 5)  // PB6

// ── Display ───────────────────────────────────────────────────────────────────
GxEPD2_BW<GxEPD2_270_GDEY027T91, GxEPD2_270_GDEY027T91::HEIGHT> display(
    GxEPD2_270_GDEY027T91(PIN_EPD_CS, PIN_EPD_DC, PIN_EPD_RST, PIN_EPD_BUSY)
);

// ── Sleep ─────────────────────────────────────────────────────────────────────
#define SLEEP_AFTER_MS 60000

static uint32_t s_last_activity;

// ── Serial image receiver ─────────────────────────────────────────────────────
#define IMG_SIZE (264 * 176 / 8)   // 5808 bytes
static uint8_t img_buf[IMG_SIZE];

// ── ESPNow ────────────────────────────────────────────────────────────────────
enum MsgType : uint8_t { MSG_BEACON, MSG_PING, MSG_PONG, MSG_TEXT, MSG_CHALLENGE, MSG_CAP_TOKEN };

// Fields up to (but not including) pubkey are covered by the ECDSA signature
static const size_t BEACON_SIGN_LEN = 1 + 16 + 6 + 4 + 32;  // type+name+mac+counter+text = 59 bytes

struct BeaconMsg {
    MsgType  type;
    char     name[16];
    uint8_t  mac[6];
    uint32_t counter;
    char     text[32];
    // --- signed boundary above ---
    uint8_t  pubkey[64];  // sender's P-256 public key (all-zero = unsigned)
    uint8_t  sig[64];     // ECDSA-P256 signature over first BEACON_SIGN_LEN bytes
};
// Total: 187 bytes — under 250-byte ESPNow hard limit

struct PeerEntry {
    uint8_t  mac[6];
    char     name[16];
    int8_t   rssi;
    uint32_t last_seen;
    uint32_t first_seen;
    bool     verified;          // ECDSA signature verified
    bool     sig_present;       // peer sent a signature (false = old firmware)
    bool     blocked;           // user blocked or auto-blocked — pings silently ignored
    uint8_t  pubkey[64];        // their P-256 public key
    uint16_t ping_count;        // pings received in current rate-limit window (uint16 prevents wrap-bypass)
    uint32_t ping_window_start; // millis() when current window began
};

static const int MAX_PEERS       = 500;
static const int PING_RATE_LIMIT = 5;      // max pings from one peer per window
static const uint32_t PING_WINDOW_MS = 10000; // 10-second rolling window

static volatile bool g_recv_flag   = false;
static BeaconMsg     g_last_recv   = {};
static uint32_t      g_send_count  = 0;
static bool          g_espnow_up   = false;
static char          g_callsign[16] = {};
static PeerEntry*    g_peers        = nullptr;
static int           g_peer_count  = 0;
static int           g_espnow_tab   = 0;  // 0=BCN 1=PEERS 2=LOG 3=SCORE
static int           g_peers_cursor = 0;
static int           g_log_cursor   = 0;

// ── CTF game ─────────────────────────────────────────────────────────────────

struct CaptureRecord {
    uint8_t  mac[6];
    char     name[16];
    uint32_t timestamp;
    bool     verified;  // hardware-verified (ATECC-signed token)
};

static const int MAX_CAPTURES  = 50;
static CaptureRecord* g_captures     = nullptr;
static int            g_capture_count  = 0;   // badges we've captured
static int            g_captured_count = 0;   // times we were captured by others
static bool           g_cap_pending    = false;
static uint32_t       g_cap_sent_at    = 0;
static int            g_cap_peer_idx   = -1;
static uint8_t        g_cap_nonce[32]  = {};
static int            g_score_cursor   = 0;

// Partial e-paper refresh counter — full refresh every 10 updates to clear ghosting
static int            g_partial_count  = 0;

// Spinlock protecting shared state written in the ESP-NOW callback task
// and read in the Arduino main-loop task (dual-core ESP32-S3).
static portMUX_TYPE   g_espnow_mux    = portMUX_INITIALIZER_UNLOCKED;

// Auto-response: set in on_recv, handled in main loop
static volatile bool  g_challenge_flag = false;
static BeaconMsg      g_challenge_msg  = {};
static uint8_t        g_challenge_mac[6] = {};

// ── ATECC608B hardware crypto
static ATECCX08A g_atecc;
static bool      g_atecc_available = false;  // chip found at 0x60
static bool      g_atecc_ok        = false;  // provisioned + pubkey cached
static uint8_t   g_atecc_pubkey[PUBLIC_KEY_SIZE] = {};

// Queued ECDSA verification (done in main loop, not in recv callback)
static bool      g_needs_verify       = false;
static BeaconMsg g_pending_verify     = {};
static uint8_t   g_pending_ver_mac[6] = {};

// Callsign editor
static const char EDIT_CHARSET[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 -";
static const int  EDIT_CHARSET_LEN = sizeof(EDIT_CHARSET) - 1;
static char       g_edit_buf[16]  = {};
static int        g_edit_pos      = 0;
static int        g_edit_char_idx = 0;

// Unicast targeting + ping/pong
static int           g_target_peer_idx = -1;
static int           g_target_cursor   = 0;   // 0=PING 1=CAPTURE
static uint32_t      g_ping_sent_at    = 0;
static int32_t       g_last_rtt        = -1;  // ms, -1=no measurement yet
static bool          g_ping_pending    = false;
static volatile bool g_pong_flag       = false;
static uint8_t       g_pong_mac[6]     = {};

// ── App state ─────────────────────────────────────────────────────────────────
enum AppState {
    STATE_HOME,
    STATE_MENU,
    STATE_SYS_INFO,
    STATE_ESPNOW,
    STATE_ESPNOW_EDIT,
    STATE_ESPNOW_TARGET,
    STATE_ATECC_INTRO,  // first-time identity setup — show intro, wait for SELECT
    STATE_ATECC_DONE,   // provisioning complete — show result, any button → ESPNow
};

static AppState g_state        = STATE_MENU;
static int      g_cursor       = 0;
static bool     g_needs_redraw = true;
static uint8_t  g_last_btns    = 0;
static int      g_sysinfo_page = 0;   // 0=hardware 1=RNG
static uint8_t  g_sysinfo_rng[16] = {};

#define MENU_COUNT 2
static const char* MENU_LABELS[MENU_COUNT] = {
    "1. System Info",
    "2. ESPNow Beacon",
};

// ── Button IRQ ────────────────────────────────────────────────────────────────

static volatile bool g_btn_irq = false;

void IRAM_ATTR on_btn_irq() {
    g_btn_irq = true;
}

// ── Hardware init ─────────────────────────────────────────────────────────────

static void init_peripherals() {
    pinMode(PIN_PWR, OUTPUT);
    digitalWrite(PIN_PWR, HIGH);

    pinMode(PIN_SE_EN, OUTPUT);
    digitalWrite(PIN_SE_EN, HIGH);

    delay(50);  // let regulators settle

    Wire.begin(PIN_SDA, PIN_SCL);
    Wire.setClock(400000);  // 400 kHz fast-mode — TCA9534 and ATECC608B both support it

    Wire.beginTransmission(TCA9534_ADDR);
    Wire.write(TCA9534_CONFIG);
    Wire.write(0xFF);  // all pins = inputs
    Wire.endTransmission();

    SPI.begin(PIN_EPD_SCK, /*MISO*/-1, PIN_EPD_MOSI, PIN_EPD_CS);
    display.init(115200, true, 10, false);
    display.setRotation(1);  // landscape: 264 wide × 176 tall

    // TCA9534 asserts PIN_BTN_IRQ LOW on any button change — use it instead of polling
    pinMode(PIN_BTN_IRQ, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(PIN_BTN_IRQ), on_btn_irq, FALLING);
}

// ── Buttons ───────────────────────────────────────────────────────────────────

static uint8_t read_buttons() {
    Wire.beginTransmission(TCA9534_ADDR);
    Wire.write(TCA9534_INPUT);
    Wire.endTransmission();
    Wire.requestFrom(TCA9534_ADDR, 1);
    return (~Wire.read()) & 0x3F;  // invert active-LOW, mask to 6 buttons
}

// ── Deep sleep ────────────────────────────────────────────────────────────────

static void go_to_sleep() {
    read_buttons();  // clear pending TCA9534 INT

    Serial.println("[oniondao badge] sleeping — press any button to wake.");
    Serial.flush();

    display.hibernate();
    esp_sleep_enable_ext0_wakeup((gpio_num_t)PIN_BTN_IRQ, 0);
    esp_deep_sleep_start();
}

// ── Serial image receiver ─────────────────────────────────────────────────────

static void check_serial_image() {
    static uint8_t hdr[4] = {0};

    while (Serial.available()) {
        hdr[0] = hdr[1]; hdr[1] = hdr[2]; hdr[2] = hdr[3];
        hdr[3] = Serial.read();

        if (hdr[0]=='L' && hdr[1]=='O' && hdr[2]=='G' && hdr[3]==':') {
            Serial.printf("peers,%d\r\n", g_peer_count);
            Serial.println("mac,callsign,rssi,first_seen_ms,last_seen_ms");
            for (int i = 0; i < g_peer_count; i++) {
                Serial.printf("%02X:%02X:%02X:%02X:%02X:%02X,%s,%d,%lu,%lu\r\n",
                    g_peers[i].mac[0], g_peers[i].mac[1], g_peers[i].mac[2],
                    g_peers[i].mac[3], g_peers[i].mac[4], g_peers[i].mac[5],
                    g_peers[i].name, (int)g_peers[i].rssi,
                    (unsigned long)g_peers[i].first_seen, (unsigned long)g_peers[i].last_seen);
            }
            Serial.println("OK");
            memset(hdr, 0, sizeof(hdr));
            return;
        }

        if (hdr[0]=='I' && hdr[1]=='M' && hdr[2]=='G' && hdr[3]==':') {
            size_t received = 0;
            uint32_t deadline = millis() + 5000;
            while (received < IMG_SIZE) {
                if (millis() > deadline) { Serial.println("ERR:timeout"); return; }
                int b = Serial.read();
                if (b >= 0) img_buf[received++] = (uint8_t)b;
                else yield();
            }
            display.setFullWindow();
            display.firstPage();
            do {
                display.fillScreen(GxEPD_WHITE);
                display.drawBitmap(0, 0, img_buf, 264, 176, GxEPD_BLACK, GxEPD_WHITE);
            } while (display.nextPage());
            display.hibernate();

            {
                Preferences prefs;
                prefs.begin("badge", false);
                prefs.putBytes("homescreen", img_buf, IMG_SIZE);
                prefs.end();
            }

            // Sync state machine: home screen will now load from NVS
            g_state        = STATE_HOME;
            g_needs_redraw = true;

            Serial.println("OK");
            s_last_activity = millis();
            memset(hdr, 0, sizeof(hdr));
            return;
        }
    }
}

// ── Rendering helpers ─────────────────────────────────────────────────────────
// FreeMono9pt7b metrics: xAdvance=11px, yAdvance=18px
// Safe line length: 23 chars (23*11=253px within 264px display)
// Safe line spacing: 18px minimum to avoid vertical overlap

static void page_header(const char* title) {
    display.fillScreen(GxEPD_WHITE);
    display.setFont(&FreeMonoBold9pt7b);
    display.setTextColor(GxEPD_BLACK);
    display.setCursor(8, 22);
    display.print(title);
    display.drawFastHLine(0, 30, display.width(), GxEPD_BLACK);
}

// Guide header: title left, page number right
static void guide_header(const char* title, int page, int total) {
    display.fillScreen(GxEPD_WHITE);
    display.setFont(&FreeMonoBold9pt7b);
    display.setTextColor(GxEPD_BLACK);

    display.setCursor(8, 22);
    display.print(title);

    // Right-align "N/M" in the same header bar
    char pg[8];
    snprintf(pg, sizeof(pg), "%d/%d", page + 1, total);
    int16_t x1, y1;
    uint16_t tw, th;
    display.getTextBounds(pg, 0, 0, &x1, &y1, &tw, &th);
    display.setCursor(display.width() - (int16_t)tw - 8, 22);
    display.print(pg);

    display.drawFastHLine(0, 30, display.width(), GxEPD_BLACK);
}

static void page_footer(const char* hint) {
    display.drawFastHLine(0, 145, display.width(), GxEPD_BLACK);
    display.setFont(&FreeMono9pt7b);
    display.setTextColor(GxEPD_BLACK);
    display.setCursor(8, 160);
    display.print(hint);
}

// ── Draw: home screen ─────────────────────────────────────────────────────────

static void draw_home_screen() {
    {
        Preferences prefs;
        prefs.begin("badge", true);  // read-only
        size_t len = prefs.getBytesLength("homescreen");
        if (len == IMG_SIZE) {
            prefs.getBytes("homescreen", img_buf, IMG_SIZE);
            prefs.end();
            display.setFullWindow();
            display.firstPage();
            do {
                display.fillScreen(GxEPD_WHITE);
                display.drawBitmap(0, 0, img_buf, 264, 176, GxEPD_BLACK, GxEPD_WHITE);
            } while (display.nextPage());
            display.hibernate();
            return;
        }
        prefs.end();
    }

    // Fallback: text home screen (no image saved yet)
    display.setFullWindow();
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);

        display.setFont(&FreeMonoBold9pt7b);
        display.setTextColor(GxEPD_BLACK);
        display.setCursor(8, 22);
        display.print("ONIONDAO BADGE");
        display.drawFastHLine(0, 30, display.width(), GxEPD_BLACK);

        display.setFont(&FreeMono9pt7b);
        display.setCursor(8, 52);
        display.print("ESP32-S3  GxEPD2");
        display.setCursor(8, 72);
        display.print("264x176   SSD1680");
        display.setCursor(8, 92);
        display.print("ATECC608B  CC1101");

        display.drawFastHLine(0, 145, display.width(), GxEPD_BLACK);
        display.setCursor(8, 160);
        display.print("Press any button");
    } while (display.nextPage());
    display.hibernate();
}

// ── Draw: menu ────────────────────────────────────────────────────────────────

static void draw_menu() {
    // 8 items at 16px spacing, starting at y=38 (last item at y=38+7*16=150)
    display.setFullWindow();
    display.firstPage();
    do {
        page_header("ONIONDAO BADGE");
        display.setFont(&FreeMono9pt7b);
        for (int i = 0; i < MENU_COUNT; i++) {
            int16_t y = 38 + i * 16;
            if (i == g_cursor) {
                display.fillRect(0, y - 12, display.width(), 15, GxEPD_BLACK);
                display.setTextColor(GxEPD_WHITE);
            } else {
                display.setTextColor(GxEPD_BLACK);
            }
            display.setCursor(8, y);
            display.print(MENU_LABELS[i]);
        }
        display.drawFastHLine(0, 158, display.width(), GxEPD_BLACK);
        display.setFont(&FreeMono9pt7b);
        display.setTextColor(GxEPD_BLACK);
        display.setCursor(8, 173);
        display.print("UP/DN  SEL  CXL:home");
    } while (display.nextPage());
    display.hibernate();
}

// ── Draw: system info ─────────────────────────────────────────────────────────

static const char* i2c_device_name(uint8_t addr) {
    switch (addr) {
        case 0x20: return "TCA9534 (buttons)";
        case 0x60: return "ATECC608B (crypto)";
        default:   return "";
    }
}

static const char* relative_time(uint32_t ms_ago, char* buf, size_t bufsz) {
    uint32_t secs = ms_ago / 1000;
    if (secs < 60)        snprintf(buf, bufsz, "%us", (unsigned)secs);
    else if (secs < 3600) snprintf(buf, bufsz, "%um", (unsigned)(secs / 60));
    else                  snprintf(buf, bufsz, ">1h");
    return buf;
}

// ── ATECC608B provisioning ────────────────────────────────────────────────────

// Detect chip and load public key; sets g_atecc_available and g_atecc_ok.
static void draw_sys_info() {
    display.setFullWindow();
    display.firstPage();
    do {
        char title[24];
        snprintf(title, sizeof(title), "SYSTEM INFO  %d/2", g_sysinfo_page + 1);
        page_header(title);
        display.setFont(&FreeMono9pt7b);
        display.setTextColor(GxEPD_BLACK);

        char buf[36];
        int y = 50;  // 18px safe line spacing throughout

        if (g_sysinfo_page == 0) {
            // Page 1 — hardware
            snprintf(buf, sizeof(buf), "Chip: %s", ESP.getChipModel());
            display.setCursor(8, y); display.print(buf); y += 18;

            snprintf(buf, sizeof(buf), "Clock: %d MHz", (int)ESP.getCpuFreqMHz());
            display.setCursor(8, y); display.print(buf); y += 18;

            snprintf(buf, sizeof(buf), "Flash: %u MB",
                     (unsigned)(ESP.getFlashChipSize() / 1024 / 1024));
            display.setCursor(8, y); display.print(buf); y += 18;

            uint32_t psram = ESP.getPsramSize();
            snprintf(buf, sizeof(buf), "PSRAM: %u MB",
                     psram > 0 ? (unsigned)(psram / 1024 / 1024) : 0);
            display.setCursor(8, y); display.print(buf); y += 18;

            snprintf(buf, sizeof(buf), "Heap:  %u KB free",
                     (unsigned)(ESP.getFreeHeap() / 1024));
            display.setCursor(8, y); display.print(buf); y += 18;

            snprintf(buf, sizeof(buf), "MAC: %s", WiFi.macAddress().c_str());
            display.setCursor(8, y); display.print(buf);

        } else {
            // Page 2 — I2C devices + RNG
            display.setCursor(8, y); display.print("I2C devices:"); y += 18;

            uint8_t i2c_addrs[20]; int i2c_count = 0;
            for (uint8_t a = 1; a < 127 && i2c_count < 20; a++) {
                Wire.beginTransmission(a);
                if (Wire.endTransmission() == 0) i2c_addrs[i2c_count++] = a;
            }
            for (int i = 0; i < i2c_count && y < 120; i++, y += 18) {
                snprintf(buf, sizeof(buf), " 0x%02X %s",
                         i2c_addrs[i], i2c_device_name(i2c_addrs[i]));
                display.setCursor(8, y); display.print(buf);
            }
            if (i2c_count == 0) { display.setCursor(8, y); display.print(" none"); y += 18; }

            display.drawFastHLine(8, y + 4, display.width() - 16, GxEPD_BLACK); y += 14;
            display.setCursor(8, y); display.print("RNG:"); y += 18;
            for (int row = 0; row < 2 && y < 158; row++, y += 18) {
                snprintf(buf, sizeof(buf), "%02X%02X %02X%02X %02X%02X %02X%02X",
                         g_sysinfo_rng[row*8+0], g_sysinfo_rng[row*8+1],
                         g_sysinfo_rng[row*8+2], g_sysinfo_rng[row*8+3],
                         g_sysinfo_rng[row*8+4], g_sysinfo_rng[row*8+5],
                         g_sysinfo_rng[row*8+6], g_sysinfo_rng[row*8+7]);
                display.setCursor(8, y); display.print(buf);
            }
        }

        page_footer("L/R:page  CXL:back");
    } while (display.nextPage());
    display.hibernate();
}

// If chip is found but not yet provisioned, returns false so caller can
// route to STATE_ATECC_INTRO.
static bool check_atecc() {
    if (!g_atecc.begin(0x60, Wire, Serial)) return false;
    g_atecc_available = true;
    g_atecc.wakeUp();
    g_atecc.readConfigZone(false);
    bool config_ok = g_atecc.configLockStatus;
    bool slot_ok   = g_atecc.slot0LockStatus;
    if (config_ok && slot_ok) {
        g_atecc.generatePublicKey(0, false);
        memcpy(g_atecc_pubkey, g_atecc.publicKey64Bytes, PUBLIC_KEY_SIZE);
        g_atecc.sleep();
        g_atecc_ok = true;
        Serial.println("[atecc] provisioned, public key loaded");
        return true;
    }
    g_atecc.sleep();
    Serial.printf("[atecc] needs provisioning (cfg=%d slot0=%d)\n", config_ok, slot_ok);
    return false;
}

// Run one-time provisioning: write SparkFun config, lock, generate key, lock slot.
// Returns true on success.
static bool do_provision_atecc() {
    if (!g_atecc_available) return false;
    g_atecc.wakeUp();
    if (!g_atecc.configLockStatus) {
        if (!g_atecc.writeConfigSparkFun()) { g_atecc.sleep(); return false; }
        if (!g_atecc.lockConfig())          { g_atecc.sleep(); return false; }
        Serial.println("[atecc] config zone locked");
    }
    if (!g_atecc.slot0LockStatus) {
        if (!g_atecc.createNewKeyPair(0))  { g_atecc.sleep(); return false; }
        if (!g_atecc.lockDataSlot0())      { g_atecc.sleep(); return false; }
        Serial.println("[atecc] key generated and locked in slot 0");
    }
    g_atecc.generatePublicKey(0, false);
    memcpy(g_atecc_pubkey, g_atecc.publicKey64Bytes, PUBLIC_KEY_SIZE);
    g_atecc.sleep();
    g_atecc_ok = true;
    Serial.println("[atecc] provisioning complete");
    return true;
}

static void draw_atecc_intro() {
    display.setFullWindow();
    display.firstPage();
    do {
        page_header("IDENTITY SETUP");
        display.setFont(&FreeMono9pt7b);
        display.setTextColor(GxEPD_BLACK);
        display.setCursor(8, 50);
        display.print("Your badge needs a");
        display.setCursor(8, 68);
        display.print("unique hardware key.");
        display.setCursor(8, 86);
        display.print("This happens once and");
        display.setCursor(8, 104);
        display.print("cannot be undone.");
        display.drawFastHLine(8, 116, display.width() - 16, GxEPD_BLACK);
        display.setCursor(8, 132);
        display.print("Do not power off.");
        page_footer("SEL:begin CXL:skip");
    } while (display.nextPage());
    display.hibernate();
}

static void draw_atecc_done(bool success) {
    display.setFullWindow();
    display.firstPage();
    do {
        page_header(success ? "IDENTITY READY" : "SETUP FAILED");
        display.setFont(&FreeMono9pt7b);
        display.setTextColor(GxEPD_BLACK);
        if (success) {
            display.setCursor(8, 50);
            display.print("Your badge now has a");
            display.setCursor(8, 68);
            display.print("unique hardware key");
            display.setCursor(8, 86);
            display.print("that cannot be copied.");
            display.setCursor(8, 104);
            display.print("Beacons are signed.");
        } else {
            display.setCursor(8, 50);
            display.print("Provisioning failed.");
            display.setCursor(8, 68);
            display.print("Check that ATECC608B");
            display.setCursor(8, 86);
            display.print("is at I2C 0x60.");
            display.setCursor(8, 104);
            display.print("ESPNow still works");
            display.setCursor(8, 122);
            display.print("without signatures.");
        }
        page_footer("any button to continue");
    } while (display.nextPage());
    display.hibernate();
}

// ── ESPNow logic ──────────────────────────────────────────────────────────────

static void make_default_callsign(char* out, size_t len) {
    uint8_t mac[6];
    WiFi.macAddress(mac);
    snprintf(out, len, "NCB-%02X%02X%02X", mac[3], mac[4], mac[5]);
}

static void upsert_peer(const uint8_t* mac, const BeaconMsg& msg, int8_t rssi) {
    for (int i = 0; i < g_peer_count; i++) {
        if (memcmp(g_peers[i].mac, mac, 6) == 0) {
            memcpy(g_peers[i].name, msg.name, sizeof(g_peers[i].name));
            g_peers[i].name[sizeof(g_peers[i].name) - 1] = '\0';
            // rssi not updated here — on_promisc owns it after first discovery
            g_peers[i].last_seen = millis();
            return;
        }
    }
    if (g_peer_count < MAX_PEERS) {
        memcpy(g_peers[g_peer_count].mac,  mac,      6);
        memcpy(g_peers[g_peer_count].name, msg.name, sizeof(g_peers[0].name));
        g_peers[g_peer_count].name[sizeof(g_peers[0].name) - 1] = '\0';
        g_peers[g_peer_count].rssi       = rssi;
        g_peers[g_peer_count].last_seen  = millis();
        g_peers[g_peer_count].first_seen = millis();
        g_peer_count++;
    }
}

// Shared group keys — all badges must use the same values
static const uint8_t ESPNOW_PMK[] = "NullCity-Badge-1";  // 17 bytes; esp_now_set_pmk reads first 16
static const uint8_t ESPNOW_LMK[] = "Badge-LinkKey-01";  // 17 bytes; LMK uses first 16

// Promiscuous callback — captures RSSI for packets from known peers
static void on_promisc(void* buf, wifi_promiscuous_pkt_type_t type) {
    if (type != WIFI_PKT_MGMT) return;
    const wifi_promiscuous_pkt_t* pkt = (wifi_promiscuous_pkt_t*)buf;
    int8_t rssi = pkt->rx_ctrl.rssi;
    // Source MAC sits at byte offset 10 in 802.11 management frames
    const uint8_t* src_mac = pkt->payload + 10;
    for (int i = 0; i < g_peer_count; i++) {
        if (memcmp(g_peers[i].mac, src_mac, 6) == 0) {
            g_peers[i].rssi = rssi;
            break;
        }
    }
}

// Add peer with encryption. If peer already exists but was added without
// encryption (e.g. as a ping target), upgrade it to encrypted in-place.
static void ensure_unicast_peer(const uint8_t* mac) {
    esp_now_peer_info_t existing = {};
    if (esp_now_get_peer(mac, &existing) == ESP_OK) {
        if (!existing.encrypt) {
            existing.encrypt = true;
            memcpy(existing.lmk, ESPNOW_LMK, 16);
            esp_now_mod_peer(&existing);
        }
        return;
    }
    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, mac, 6);
    peer.channel = 0;
    peer.encrypt = true;
    memcpy(peer.lmk, ESPNOW_LMK, 16);
    esp_now_add_peer(&peer);
}

// Add peer WITHOUT encryption so the target receives the frame even if
// it has not registered us yet. ensure_unicast_peer() will upgrade to
// encrypted once bidirectional communication is established.
static void add_ping_peer(const uint8_t* mac) {
    if (esp_now_is_peer_exist(mac)) return;
    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, mac, 6);
    peer.channel = 0;
    peer.encrypt = false;
    esp_err_t err = esp_now_add_peer(&peer);
    if (err != ESP_OK)
        Serial.printf("[espnow] add_ping_peer failed: %d (table full?)\n", err);
}

static void on_recv(const esp_now_recv_info_t* recv_info, const uint8_t* data, int len) {
    const uint8_t* mac = recv_info->src_addr;
    if ((size_t)len == sizeof(BeaconMsg)) {
        BeaconMsg msg;
        memcpy(&msg, data, sizeof(BeaconMsg));
        upsert_peer(mac, msg, -50);
        memcpy((void*)&g_last_recv, &msg, sizeof(BeaconMsg));

        // If beacon is signed, queue ECDSA verification for main loop
        bool has_sig = false;
        for (int i = 0; i < PUBLIC_KEY_SIZE; i++) {
            if (msg.pubkey[i]) { has_sig = true; break; }
        }
        if (has_sig && g_atecc_ok && !g_needs_verify) {
            portENTER_CRITICAL_ISR(&g_espnow_mux);
            memcpy(&g_pending_verify, &msg, sizeof(BeaconMsg));
            memcpy(g_pending_ver_mac, mac, 6);
            g_needs_verify = true;
            portEXIT_CRITICAL_ISR(&g_espnow_mux);
        }
        // Mark peer as having sent a signature (regardless of verify result yet)
        for (int i = 0; i < g_peer_count; i++) {
            if (memcmp(g_peers[i].mac, mac, 6) == 0) {
                g_peers[i].sig_present = has_sig;
                break;
            }
        }


        // Handle ping with three layers of protection:
        if (msg.type == MSG_PING) {
            // 1. Known-peer check: ignore pings from MACs that never beaconed to us
            int pidx = -1;
            for (int i = 0; i < g_peer_count; i++) {
                if (memcmp(g_peers[i].mac, mac, 6) == 0) { pidx = i; break; }
            }
            if (pidx < 0) {
                Serial.printf("[espnow] ping from unknown MAC ignored\n");
            } else if (g_peers[pidx].blocked) {
                // 2. Manual or auto-blocked
                Serial.printf("[espnow] ping from blocked peer ignored\n");
            } else {
                // 3. Rate limit: max PING_RATE_LIMIT pings per PING_WINDOW_MS
                uint32_t now = millis();
                if (now - g_peers[pidx].ping_window_start > PING_WINDOW_MS) {
                    g_peers[pidx].ping_window_start = now;
                    g_peers[pidx].ping_count = 1;
                } else {
                    g_peers[pidx].ping_count++;
                }
                if (g_peers[pidx].ping_count > PING_RATE_LIMIT) {
                    g_peers[pidx].blocked = true;
                    Serial.printf("[espnow] rate-limited %s — auto-blocked\n",
                                  g_peers[pidx].name);
                    if (g_state == STATE_ESPNOW && g_espnow_tab == 1)
                        g_needs_redraw = true;
                } else {
                    // All clear — upgrade sender to encrypted peer and send pong
                    ensure_unicast_peer(mac);
                    portENTER_CRITICAL_ISR(&g_espnow_mux);
                    memcpy(g_pong_mac, mac, 6);
                    g_pong_flag = true;
                    portEXIT_CRITICAL_ISR(&g_espnow_mux);
                }
            }
        }

        // Handle pong — measure RTT
        if (msg.type == MSG_PONG && g_ping_pending) {
            g_last_rtt    = (int32_t)(millis() - g_ping_sent_at);
            g_ping_pending = false;
        }

        // Handle CTF challenge — queue auto-response for main loop
        if (msg.type == MSG_CHALLENGE && !g_challenge_flag) {
            portENTER_CRITICAL_ISR(&g_espnow_mux);
            memcpy(&g_challenge_msg, &msg, sizeof(BeaconMsg));
            memcpy(g_challenge_mac, mac, 6);
            g_challenge_flag = true;
            portEXIT_CRITICAL_ISR(&g_espnow_mux);
        }

        // Handle capture token — record if we were the attacker
        if (msg.type == MSG_CAP_TOKEN && g_cap_pending && g_cap_peer_idx >= 0) {
            if (memcmp(g_peers[g_cap_peer_idx].mac, mac, 6) == 0) {
                g_cap_pending = false;
                bool ver = false;
                if (g_atecc_ok) {
                    // Verify: target signed (nonce + our_mac)
                    uint8_t my_mac[6];
                    WiFi.macAddress(my_mac);
                    uint8_t sig_data[38];
                    memcpy(sig_data,      g_cap_nonce, 32);
                    memcpy(sig_data + 32, my_mac, 6);
                    uint8_t digest[32];
                    mbedtls_sha256(sig_data, 38, digest, 0);
                    g_atecc.wakeUp();
                    ver = g_atecc.verifySignature(digest, msg.sig, msg.pubkey);
                    g_atecc.sleep();
                }
                if (g_capture_count < MAX_CAPTURES) {
                    memcpy(g_captures[g_capture_count].mac, mac, 6);
                    strncpy(g_captures[g_capture_count].name, msg.name,
                            sizeof(g_captures[0].name));
                    g_captures[g_capture_count].name[sizeof(g_captures[0].name) - 1] = '\0';
                    g_captures[g_capture_count].timestamp = millis();
                    g_captures[g_capture_count].verified  = ver;
                    g_capture_count++;
                }
                Serial.printf("[ctf] captured %s (%s)\n",
                    msg.name, ver ? "verified" : "unverified");
                g_needs_redraw = true;
            }
        }

        g_recv_flag = true;
        Serial.printf("[espnow] recv %s from %s (%02X:%02X:%02X:%02X:%02X:%02X) #%lu\n",
            msg.type == MSG_PING ? "PING" : msg.type == MSG_PONG ? "PONG" :
            msg.type == MSG_TEXT ? "TEXT" :
            msg.type == MSG_CHALLENGE ? "CHAL" :
            msg.type == MSG_CAP_TOKEN ? "CAP" : "BCN",
            msg.name, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], msg.counter);
    }
}

static void send_beacon() {
    BeaconMsg msg = {};
    msg.type = MSG_BEACON;
    strncpy(msg.name, g_callsign, sizeof(msg.name));
    WiFi.macAddress(msg.mac);
    msg.counter = ++g_send_count;

    if (g_atecc_ok) {
        // Sign the first BEACON_SIGN_LEN bytes
        uint8_t digest[32];
        mbedtls_sha256((const uint8_t*)&msg, BEACON_SIGN_LEN, digest, 0);
        g_atecc.wakeUp();
        if (g_atecc.createSignature(digest, 0)) {
            memcpy(msg.pubkey, g_atecc_pubkey, PUBLIC_KEY_SIZE);
            memcpy(msg.sig,    g_atecc.signature, SIGNATURE_SIZE);
        }
        g_atecc.sleep();
    }

    uint8_t broadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    esp_now_send(broadcast, (uint8_t*)&msg, sizeof(msg));
    Serial.printf("[espnow] sent beacon #%lu as %s%s\n",
        msg.counter, g_callsign, g_atecc_ok ? " [signed]" : "");
}

static void shutdown_espnow() {
    if (!g_espnow_up) return;
    esp_wifi_set_promiscuous(false);
    esp_now_deinit();
    WiFi.mode(WIFI_OFF);
    g_espnow_up       = false;
    // g_peer_count preserved — peer table is the attendance log
    g_send_count      = 0;
    g_espnow_tab      = 0;
    g_peers_cursor    = 0;
    g_log_cursor      = 0;
    g_score_cursor    = 0;
    g_cap_pending     = false;
    g_cap_peer_idx    = -1;
    g_challenge_flag  = false;
    g_partial_count   = 0;
    g_ping_pending    = false;
    g_pong_flag       = false;
    g_last_rtt        = -1;
    g_target_peer_idx = -1;
    memset((void*)&g_last_recv, 0, sizeof(g_last_recv));
    Serial.println("[espnow] shut down");
}

static void init_espnow() {
    if (g_espnow_up) return;

    // Load or generate callsign
    {
        Preferences prefs;
        prefs.begin("badge", false);
        String cs = prefs.getString("callsign", "");
        if (cs.length() == 0) {
            WiFi.mode(WIFI_STA);  // needed to read MAC before esp_now_init
            make_default_callsign(g_callsign, sizeof(g_callsign));
            prefs.putString("callsign", g_callsign);
            Serial.printf("[espnow] generated callsign: %s\n", g_callsign);
        } else {
            strncpy(g_callsign, cs.c_str(), sizeof(g_callsign));
            g_callsign[sizeof(g_callsign) - 1] = '\0';
        }
        prefs.end();
    }

    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    if (esp_now_init() != ESP_OK) {
        Serial.println("[espnow] init failed");
        return;
    }
    esp_now_register_recv_cb(on_recv);
    esp_now_set_pmk(ESPNOW_PMK);

    // Broadcast peer — unencrypted (encryption not supported for broadcast)
    esp_now_peer_info_t peer = {};
    memset(peer.peer_addr, 0xFF, 6);
    peer.channel = 0;
    peer.encrypt = false;
    esp_now_add_peer(&peer);

    // Enable promiscuous mode to capture RSSI
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_promiscuous_rx_cb(on_promisc);

    g_espnow_up = true;
    Serial.printf("[espnow] ready as %s (encrypted unicast)\n", g_callsign);
}

// ── Draw: ESPNow sub-screens ──────────────────────────────────────────────────

static void espnow_tab_header(const char* title) {
    display.fillScreen(GxEPD_WHITE);
    display.setFont(&FreeMonoBold9pt7b);
    display.setTextColor(GxEPD_BLACK);

    // Tab bar: [BCN] [PEERS] [LOG] [SCORE]
    const char* tabs[] = {"BCN", "PEERS", "LOG", "SCORE"};
    int x = 0;
    for (int i = 0; i < 4; i++) {
        int tw = strlen(tabs[i]) * 11 + 4;
        if (i == g_espnow_tab) {
            display.fillRect(x, 0, tw, 18, GxEPD_BLACK);
            display.setTextColor(GxEPD_WHITE);
        } else {
            display.drawRect(x, 0, tw, 18, GxEPD_BLACK);
            display.setTextColor(GxEPD_BLACK);
        }
        display.setCursor(x + 2, 14);
        display.print(tabs[i]);
        x += tw + 2;
    }
    display.setTextColor(GxEPD_BLACK);
    display.drawFastHLine(0, 20, display.width(), GxEPD_BLACK);
}

static void draw_espnow_beacon() {
    if (++g_partial_count >= 10) {
        g_partial_count = 0;
        display.setFullWindow();
    } else {
        display.setPartialWindow(0, 0, display.width(), display.height());
    }
    display.firstPage();
    do {
        espnow_tab_header("BEACON");
        display.setFont(&FreeMono9pt7b);
        display.setTextColor(GxEPD_BLACK);

        char buf[36];
        snprintf(buf, sizeof(buf), "Me: %s", g_callsign);
        display.setCursor(8, 36);
        display.print(buf);

        snprintf(buf, sizeof(buf), "%s", WiFi.macAddress().c_str());
        display.setCursor(8, 52);
        display.print(buf);

        snprintf(buf, sizeof(buf), "Sent: %-4lu  Peers: %d", g_send_count, g_peer_count);
        display.setCursor(8, 68);
        display.print(buf);

        if (g_last_rtt >= 0) {
            snprintf(buf, sizeof(buf), "RTT: %d ms", (int)g_last_rtt);
            display.setCursor(8, 84);
            display.print(buf);
        } else if (g_ping_pending) {
            display.setCursor(8, 84);
            display.print("RTT: waiting...");
        }

        display.drawFastHLine(8, 93, display.width() - 16, GxEPD_BLACK);

        display.setCursor(8, 108);
        display.print("Last recv:");

        if (g_last_recv.mac[0] || g_last_recv.mac[1] || g_last_recv.mac[2]) {
            char peer_mac[18];
            snprintf(peer_mac, sizeof(peer_mac),
                "%02X:%02X:%02X:%02X:%02X:%02X",
                g_last_recv.mac[0], g_last_recv.mac[1], g_last_recv.mac[2],
                g_last_recv.mac[3], g_last_recv.mac[4], g_last_recv.mac[5]);
            display.setCursor(8, 124);
            display.print(g_last_recv.name);
            display.setCursor(8, 140);
            display.print(peer_mac);
        } else {
            display.setCursor(8, 124);
            display.print("(none yet)");
        }

        page_footer("SEL:snd UP:edt L/R CXL");
    } while (display.nextPage());
    display.hibernate();
}

static void draw_espnow_peers() {
    if (++g_partial_count >= 10) {
        g_partial_count = 0;
        display.setFullWindow();
    } else {
        display.setPartialWindow(0, 0, display.width(), display.height());
    }
    display.firstPage();
    do {
        espnow_tab_header("PEERS");
        display.setFont(&FreeMono9pt7b);
        display.setTextColor(GxEPD_BLACK);

        if (g_peer_count == 0) {
            display.setCursor(8, 50);
            display.print("No peers seen yet.");
            display.setCursor(8, 68);
            display.print("Go to BEACON and");
            display.setCursor(8, 86);
            display.print("press SEL to beacon.");
        } else {
            // 3 peers visible per screen, each slot is 40px tall
            int visible = min(g_peer_count, 3);
            int start   = min(g_peers_cursor, max(0, g_peer_count - visible));
            for (int i = 0; i < visible; i++) {
                int idx = start + i;
                if (idx >= g_peer_count) break;

                int y_slot = 22 + i * 40;
                int y_name = y_slot + 14;
                int y_bar  = y_slot + 20;
                int y_tier = y_slot + 34;

                if (i > 0)
                    display.drawFastHLine(0, y_slot, display.width(), GxEPD_BLACK);

                // Selection highlight on name row
                if (idx == g_peers_cursor) {
                    display.fillRect(0, y_name - 12, display.width(), 15, GxEPD_BLACK);
                    display.setTextColor(GxEPD_WHITE);
                } else {
                    display.setTextColor(GxEPD_BLACK);
                }

                // Callsign
                display.setCursor(8, y_name);
                display.print(g_peers[idx].name);

                // Right-side indicators: [B] blocked, [V]/[!] sig, [E] encrypted
                // Layout (right to left): [E] x=227, [V]/[!] x=194, [B] x=161
                esp_now_peer_info_t pi = {};
                bool encrypted = (esp_now_get_peer(g_peers[idx].mac, &pi) == ESP_OK && pi.encrypt);

                if (g_peers[idx].blocked) {
                    display.setCursor(161, y_name);
                    display.print("[B]");
                }
                if (g_peers[idx].sig_present) {
                    display.setCursor(194, y_name);
                    display.print(g_peers[idx].verified ? "[V]" : "[!]");
                }
                if (encrypted) {
                    display.setCursor(227, y_name);
                    display.print("[E]");
                }

                display.setTextColor(GxEPD_BLACK);

                // RSSI bar: 8 blocks × 6px wide × 10px tall, 1px gap between blocks
                // Formula: -90 dBm → 0 blocks, -30 dBm → 8 blocks
                int8_t rssi = g_peers[idx].rssi;
                int blocks = max(0, min(8, ((int)rssi + 90) * 8 / 60));
                for (int b = 0; b < 8; b++) {
                    int bx = 8 + b * 7;
                    if (b < blocks)
                        display.fillRect(bx, y_bar, 6, 10, GxEPD_BLACK);
                    else
                        display.drawRect(bx, y_bar, 6, 10, GxEPD_BLACK);
                }

                // Proximity tier + dBm value
                const char* tier = rssi > -50 ? "CLOSE" : rssi > -70 ? "NEAR " : "FAR  ";
                char tbuf[20];
                snprintf(tbuf, sizeof(tbuf), " %s %ddBm", tier, (int)rssi);
                display.setCursor(66, y_tier);
                display.print(tbuf);
            }
        }

        page_footer("SEL:tgt UP/DN L/R CXL");
    } while (display.nextPage());
    display.hibernate();
}

static void enter_espnow_edit() {
    strncpy(g_edit_buf, g_callsign, sizeof(g_edit_buf));
    g_edit_pos = 0;
    char c = g_edit_buf[0];
    g_edit_char_idx = 0;
    for (int i = 0; i < EDIT_CHARSET_LEN; i++) {
        if (EDIT_CHARSET[i] == c) { g_edit_char_idx = i; break; }
    }
}

static void save_callsign() {
    int len = strlen(g_edit_buf);
    while (len > 1 && g_edit_buf[len - 1] == ' ') g_edit_buf[--len] = '\0';
    strncpy(g_callsign, g_edit_buf, sizeof(g_callsign));
    g_callsign[sizeof(g_callsign) - 1] = '\0';
    Preferences prefs;
    prefs.begin("badge", false);
    prefs.putString("callsign", g_callsign);
    prefs.end();
    Serial.printf("[espnow] callsign saved: %s\n", g_callsign);
}

static void draw_espnow_edit() {
    display.setFullWindow();
    display.firstPage();
    do {
        page_header("EDIT CALLSIGN");
        display.setFont(&FreeMono9pt7b);
        display.setTextColor(GxEPD_BLACK);

        int len = max((int)strlen(g_edit_buf), g_edit_pos + 1);
        if (len > 12) len = 12;
        for (int i = 0; i < len; i++) {
            int x = 8 + i * 20;
            char ch = (i < (int)strlen(g_edit_buf)) ? g_edit_buf[i] : ' ';
            if (i == g_edit_pos) {
                display.fillRect(x - 2, 34, 18, 20, GxEPD_BLACK);
                display.setTextColor(GxEPD_WHITE);
            } else {
                display.setTextColor(GxEPD_BLACK);
            }
            display.setCursor(x, 50);
            display.print(ch);
        }
        display.setTextColor(GxEPD_BLACK);

        int prev_idx = (g_edit_char_idx - 1 + EDIT_CHARSET_LEN) % EDIT_CHARSET_LEN;
        int next_idx = (g_edit_char_idx + 1) % EDIT_CHARSET_LEN;

        display.drawFastHLine(8, 66, display.width() - 16, GxEPD_BLACK);
        display.setCursor(8, 84);
        display.print("UP  ["); display.print(EDIT_CHARSET[prev_idx]); display.print("]");
        display.setCursor(8, 102);
        display.setFont(&FreeMonoBold9pt7b);
        display.print("    ["); display.print(EDIT_CHARSET[g_edit_char_idx]); display.print("] <- current");
        display.setFont(&FreeMono9pt7b);
        display.setCursor(8, 118);
        display.print("DN  ["); display.print(EDIT_CHARSET[next_idx]); display.print("]");
        display.drawFastHLine(8, 128, display.width() - 16, GxEPD_BLACK);

        page_footer("RGT:nxt SEL:sav CXL");
    } while (display.nextPage());
    display.hibernate();
}

static void draw_espnow_target() {
    display.setFullWindow();
    display.firstPage();
    do {
        const char* peer_name = (g_target_peer_idx >= 0) ? g_peers[g_target_peer_idx].name : "?";
        char title[32];
        snprintf(title, sizeof(title), "TARGET: %s", peer_name);
        page_header(title);

        display.setFont(&FreeMono9pt7b);

        bool is_blocked = (g_target_peer_idx >= 0) && g_peers[g_target_peer_idx].blocked;
        const char* options[] = { "PING", "CAPTURE", is_blocked ? "UNBLOCK" : "BLOCK" };
        for (int i = 0; i < 3; i++) {
            int y = 54 + i * 28;
            if (i == g_target_cursor) {
                display.fillRect(0, y - 14, display.width(), 18, GxEPD_BLACK);
                display.setTextColor(GxEPD_WHITE);
            } else {
                display.setTextColor(GxEPD_BLACK);
            }
            display.setCursor(16, y);
            display.print(options[i]);
        }
        display.setTextColor(GxEPD_BLACK);

        page_footer("UP/DN SEL:go CXL");
    } while (display.nextPage());
    display.hibernate();
}

static void draw_espnow_log() {
    if (++g_partial_count >= 10) {
        g_partial_count = 0;
        display.setFullWindow();
    } else {
        display.setPartialWindow(0, 0, display.width(), display.height());
    }
    display.firstPage();
    do {
        espnow_tab_header("LOG");
        display.setFont(&FreeMono9pt7b);
        display.setTextColor(GxEPD_BLACK);

        if (g_peer_count == 0) {
            display.setCursor(8, 50);
            display.print("No peers logged yet.");
            display.setCursor(8, 68);
            display.print("Send a beacon first.");
        } else {
            uint32_t now = millis();
            int visible = min(g_peer_count - g_log_cursor, 4);
            for (int i = 0; i < visible; i++) {
                int idx = g_log_cursor + i;
                int y = 22 + i * 34;

                if (i > 0)
                    display.drawFastHLine(0, y, display.width(), GxEPD_BLACK);

                char tbuf1[8], tbuf2[8];
                relative_time(now - g_peers[idx].first_seen, tbuf1, sizeof(tbuf1));
                relative_time(now - g_peers[idx].last_seen,  tbuf2, sizeof(tbuf2));

                // Line 1: callsign + rssi
                char line1[24];
                snprintf(line1, sizeof(line1), "%-10s %ddBm", g_peers[idx].name, (int)g_peers[idx].rssi);
                display.setCursor(8, y + 14);
                display.print(line1);

                // Line 2: first/last times
                char line2[24];
                snprintf(line2, sizeof(line2), "1st:%s last:%s", tbuf1, tbuf2);
                display.setCursor(8, y + 30);
                display.print(line2);
            }
            if (g_peer_count > 4) {
                char more[16];
                snprintf(more, sizeof(more), "%d/%d", g_log_cursor + 1, g_peer_count);
                display.setCursor(200, 36);
                display.print(more);
            }
        }

        page_footer("UP/DN:scroll CXL:back");
    } while (display.nextPage());
    display.hibernate();
}

static void draw_espnow_score() {
    if (++g_partial_count >= 10) {
        g_partial_count = 0;
        display.setFullWindow();
    } else {
        display.setPartialWindow(0, 0, display.width(), display.height());
    }
    display.firstPage();
    do {
        espnow_tab_header("SCORE");
        display.setFont(&FreeMono9pt7b);
        display.setTextColor(GxEPD_BLACK);

        char header[32];
        snprintf(header, sizeof(header), "Captured: %d  Got: %d",
            g_capture_count, g_captured_count);
        display.setCursor(8, 36);
        display.print(header);

        if (g_cap_pending) {
            display.setCursor(8, 54);
            display.print("Waiting for response");
        } else if (g_capture_count == 0) {
            display.setCursor(8, 60);
            display.print("No captures yet.");
            display.setCursor(8, 78);
            display.print("Go to PEERS, select");
            display.setCursor(8, 96);
            display.print("a badge, then CAPTURE.");
        } else {
            uint32_t now = millis();
            int visible = min(g_capture_count - g_score_cursor, 4);
            for (int i = 0; i < visible; i++) {
                int idx = g_score_cursor + i;
                int y = 50 + i * 28;
                char tbuf[8];
                relative_time(now - g_captures[idx].timestamp, tbuf, sizeof(tbuf));
                char line[24];
                snprintf(line, sizeof(line), "%s %s%s",
                    g_captures[idx].name, tbuf,
                    g_captures[idx].verified ? " [V]" : "");
                display.setCursor(8, y);
                display.print(line);
            }
            if (g_capture_count > 4) {
                char more[16];
                snprintf(more, sizeof(more), "%d/%d", g_score_cursor + 1, g_capture_count);
                display.setCursor(200, 36);
                display.print(more);
            }
        }

        page_footer("UP/DN:scroll CXL:back");
    } while (display.nextPage());
    display.hibernate();
}

static void draw_espnow() {
    switch (g_espnow_tab) {
        case 0: draw_espnow_beacon(); break;
        case 1: draw_espnow_peers();  break;
        case 2: draw_espnow_log();    break;
        case 3: draw_espnow_score();  break;
    }
}

// ── Dispatch ──────────────────────────────────────────────────────────────────

static void dispatch_render() {
    switch (g_state) {
        case STATE_HOME:          draw_home_screen();         break;
        case STATE_MENU:          draw_menu();                break;
        case STATE_SYS_INFO:      draw_sys_info();            break;
        case STATE_ESPNOW:        draw_espnow();              break;
        case STATE_ESPNOW_EDIT:   draw_espnow_edit();         break;
        case STATE_ESPNOW_TARGET: draw_espnow_target();       break;
        case STATE_ATECC_INTRO:   draw_atecc_intro();         break;
        case STATE_ATECC_DONE:    draw_atecc_done(g_atecc_ok); break;
    }
    g_needs_redraw = false;
}

// ── Button handling ───────────────────────────────────────────────────────────

static void handle_buttons(uint8_t pressed) {
    if (!pressed) return;
    s_last_activity = millis();

    switch (g_state) {
        case STATE_HOME:
            // Any button press enters the menu
            g_state = STATE_MENU;
            g_needs_redraw = true;
            break;

        case STATE_MENU:
            if (pressed & BTN_UP)
                { g_cursor = (g_cursor - 1 + MENU_COUNT) % MENU_COUNT; g_needs_redraw = true; }
            if (pressed & BTN_DOWN)
                { g_cursor = (g_cursor + 1) % MENU_COUNT; g_needs_redraw = true; }
            if (pressed & BTN_SELECT) {
                switch (g_cursor) {
                    case 0:
                        g_sysinfo_page = 0;
                        for (int i = 0; i < 4; i++) {
                            uint32_t r = esp_random();
                            memcpy(&g_sysinfo_rng[i*4], &r, 4);
                        }
                        g_state = STATE_SYS_INFO;
                        break;
                    case 1:
                        init_espnow();
                        if (!g_atecc_ok && !check_atecc() && g_atecc_available) {
                            g_state = STATE_ATECC_INTRO;
                        } else {
                            g_state = STATE_ESPNOW;
                        }
                        break;
                }
                g_needs_redraw = true;
            }
            if (pressed & BTN_CANCEL) { g_state = STATE_HOME; g_needs_redraw = true; }
            break;

        case STATE_SYS_INFO:
            if (pressed & BTN_LEFT)
                { g_sysinfo_page = (g_sysinfo_page + 1) % 2; g_needs_redraw = true; }
            if (pressed & BTN_RIGHT)
                { g_sysinfo_page = (g_sysinfo_page + 1) % 2; g_needs_redraw = true; }
            if (pressed & BTN_CANCEL)
                { g_state = STATE_MENU; g_needs_redraw = true; }
            break;

        case STATE_ESPNOW:
            if (pressed & BTN_LEFT)
                { g_espnow_tab = (g_espnow_tab + 3) % 4; g_needs_redraw = true; }
            if (pressed & BTN_RIGHT)
                { g_espnow_tab = (g_espnow_tab + 1) % 4; g_needs_redraw = true; }
            if (pressed & BTN_UP) {
                if (g_espnow_tab == 0)
                    { enter_espnow_edit(); g_state = STATE_ESPNOW_EDIT; g_needs_redraw = true; }
                else if (g_espnow_tab == 1 && g_peers_cursor > 0)
                    { g_peers_cursor--; g_needs_redraw = true; }
                else if (g_espnow_tab == 2 && g_log_cursor > 0)
                    { g_log_cursor--; g_needs_redraw = true; }
                else if (g_espnow_tab == 3 && g_score_cursor > 0)
                    { g_score_cursor--; g_needs_redraw = true; }
            }
            if (pressed & BTN_DOWN) {
                if (g_espnow_tab == 1 && g_peers_cursor < g_peer_count - 1)
                    { g_peers_cursor++; g_needs_redraw = true; }
                else if (g_espnow_tab == 2 && g_log_cursor < g_peer_count - 1)
                    { g_log_cursor++; g_needs_redraw = true; }
                else if (g_espnow_tab == 3 && g_score_cursor < g_capture_count - 1)
                    { g_score_cursor++; g_needs_redraw = true; }
            }
            if (pressed & BTN_SELECT) {
                if (g_espnow_tab == 0) {
                    send_beacon(); g_needs_redraw = true;
                } else if (g_espnow_tab == 1 && g_peer_count > 0) {
                    g_target_peer_idx = g_peers_cursor;
                    g_target_cursor   = 0;
                    g_state = STATE_ESPNOW_TARGET;
                    g_needs_redraw = true;
                }
            }
            if (pressed & BTN_CANCEL)
                { shutdown_espnow(); g_state = STATE_MENU; g_needs_redraw = true; }
            break;

        case STATE_ESPNOW_TARGET:
            if (pressed & BTN_UP)
                { g_target_cursor = (g_target_cursor - 1 + 3) % 3; g_needs_redraw = true; }
            if (pressed & BTN_DOWN)
                { g_target_cursor = (g_target_cursor + 1) % 3; g_needs_redraw = true; }
            if (pressed & BTN_SELECT) {
                if (g_target_cursor == 0) {
                    // PING — sent unencrypted so target receives it without prior registration
                    add_ping_peer(g_peers[g_target_peer_idx].mac);
                    BeaconMsg ping = {};
                    ping.type = MSG_PING;
                    strncpy(ping.name, g_callsign, sizeof(ping.name));
                    WiFi.macAddress(ping.mac);
                    ping.counter = ++g_send_count;
                    esp_now_send(g_peers[g_target_peer_idx].mac, (uint8_t*)&ping, sizeof(ping));
                    g_ping_sent_at = millis();
                    g_ping_pending = true;
                    Serial.printf("[espnow] ping -> %s\n", g_peers[g_target_peer_idx].name);
                    g_espnow_tab = 0;
                    g_state = STATE_ESPNOW;
                } else if (g_target_cursor == 1) {
                    // CAPTURE — send a signed challenge nonce to the target
                    ensure_unicast_peer(g_peers[g_target_peer_idx].mac);
                    BeaconMsg chal = {};
                    chal.type = MSG_CHALLENGE;
                    strncpy(chal.name, g_callsign, sizeof(chal.name));
                    WiFi.macAddress(chal.mac);
                    chal.counter = ++g_send_count;
                    // Fill text[32] with hardware random nonce
                    for (int nb = 0; nb < 4; nb++) {
                        uint32_t r = esp_random();
                        memcpy((uint8_t*)chal.text + nb * 4, &r, 4);
                    }
                    memcpy(g_cap_nonce, chal.text, 32);
                    g_cap_peer_idx  = g_target_peer_idx;
                    g_cap_sent_at   = millis();
                    g_cap_pending   = true;
                    esp_now_send(g_peers[g_target_peer_idx].mac, (uint8_t*)&chal, sizeof(chal));
                    Serial.printf("[ctf] sent capture challenge -> %s\n", g_peers[g_target_peer_idx].name);
                    g_espnow_tab = 3;  // watch SCORE tab for result
                    g_state = STATE_ESPNOW;
                } else {
                    // BLOCK / UNBLOCK
                    g_peers[g_target_peer_idx].blocked = !g_peers[g_target_peer_idx].blocked;
                    Serial.printf("[espnow] %s %s\n",
                        g_peers[g_target_peer_idx].blocked ? "blocked" : "unblocked",
                        g_peers[g_target_peer_idx].name);
                    g_state = STATE_ESPNOW; g_espnow_tab = 1;
                }
                g_needs_redraw = true;
            }
            if (pressed & BTN_CANCEL)
                { g_state = STATE_ESPNOW; g_espnow_tab = 1; g_needs_redraw = true; }
            break;



        case STATE_ESPNOW_EDIT:
            if (pressed & BTN_UP) {
                g_edit_char_idx = (g_edit_char_idx - 1 + EDIT_CHARSET_LEN) % EDIT_CHARSET_LEN;
                g_edit_buf[g_edit_pos] = EDIT_CHARSET[g_edit_char_idx];
                g_needs_redraw = true;
            }
            if (pressed & BTN_DOWN) {
                g_edit_char_idx = (g_edit_char_idx + 1) % EDIT_CHARSET_LEN;
                g_edit_buf[g_edit_pos] = EDIT_CHARSET[g_edit_char_idx];
                g_needs_redraw = true;
            }
            if (pressed & BTN_RIGHT) {
                // Advance to next position (extend buffer if needed, max 12 chars)
                if (g_edit_pos < 11) {
                    g_edit_pos++;
                    if (g_edit_pos >= (int)strlen(g_edit_buf))
                        g_edit_buf[g_edit_pos] = 'A';
                    // Find char index for new position
                    g_edit_char_idx = 0;
                    for (int i = 0; i < EDIT_CHARSET_LEN; i++) {
                        if (EDIT_CHARSET[i] == g_edit_buf[g_edit_pos]) { g_edit_char_idx = i; break; }
                    }
                    g_needs_redraw = true;
                }
            }
            if (pressed & BTN_LEFT) {
                if (g_edit_pos > 0) {
                    g_edit_pos--;
                    g_edit_char_idx = 0;
                    for (int i = 0; i < EDIT_CHARSET_LEN; i++) {
                        if (EDIT_CHARSET[i] == g_edit_buf[g_edit_pos]) { g_edit_char_idx = i; break; }
                    }
                    g_needs_redraw = true;
                }
            }
            if (pressed & BTN_SELECT) {
                save_callsign();
                g_state = STATE_ESPNOW;
                g_needs_redraw = true;
            }
            if (pressed & BTN_CANCEL) {
                g_state = STATE_ESPNOW;  // discard changes
                g_needs_redraw = true;
            }
            break;

        case STATE_ATECC_INTRO:
            if (pressed & BTN_SELECT) {
                bool ok = do_provision_atecc();
                g_state = STATE_ATECC_DONE;
                g_needs_redraw = true;
                if (ok) Serial.println("[atecc] onboarding complete");
                else    Serial.println("[atecc] onboarding failed");
            }
            if (pressed & BTN_CANCEL)
                { g_state = STATE_ESPNOW; g_needs_redraw = true; }
            break;

        case STATE_ATECC_DONE:
            if (pressed)
                { g_state = STATE_ESPNOW; g_needs_redraw = true; }
            break;

        default:
            if (pressed & BTN_CANCEL)
                { g_state = STATE_MENU; g_needs_redraw = true; }
            break;
    }
}

// ── Setup ─────────────────────────────────────────────────────────────────────

void setup() {
    Serial.setRxBufferSize(8192);  // default 256 drops the 5808-byte image transfer
    Serial.begin(115200);
    delay(200);

    // Allocate large arrays from PSRAM; fall back to heap if PSRAM unavailable
    g_peers = (PeerEntry*)heap_caps_malloc(MAX_PEERS * sizeof(PeerEntry), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!g_peers) g_peers = (PeerEntry*)malloc(MAX_PEERS * sizeof(PeerEntry));
    if (!g_peers) { Serial.println("[FATAL] g_peers alloc failed"); while (true) delay(1000); }
    memset(g_peers, 0, MAX_PEERS * sizeof(PeerEntry));

    g_captures = (CaptureRecord*)heap_caps_malloc(MAX_CAPTURES * sizeof(CaptureRecord), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!g_captures) g_captures = (CaptureRecord*)malloc(MAX_CAPTURES * sizeof(CaptureRecord));
    if (!g_captures) { Serial.println("[FATAL] g_captures alloc failed"); while (true) delay(1000); }
    memset(g_captures, 0, MAX_CAPTURES * sizeof(CaptureRecord));

    init_peripherals();

    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();

    if (cause == ESP_SLEEP_WAKEUP_EXT0) {
        Serial.println("[oniondao badge] woke from sleep.");
        display.hibernate();
    } else {
        Serial.println("[oniondao badge] booting...");

        Serial.print("I2C scan:");
        for (uint8_t addr = 1; addr < 127; addr++) {
            Wire.beginTransmission(addr);
            if (Wire.endTransmission() == 0) Serial.printf(" 0x%02X", addr);
        }
        Serial.println();
    }

    g_state        = STATE_HOME;
    g_cursor       = 0;
    g_needs_redraw = true;
    s_last_activity = millis();

    Serial.printf("[oniondao badge] awake for %d s\n", SLEEP_AFTER_MS / 1000);
}

// ── Loop ──────────────────────────────────────────────────────────────────────

void loop() {
    check_serial_image();

    if (g_needs_redraw) {
        dispatch_render();
    }

    // Only read I²C when TCA9534 asserts the IRQ line
    uint8_t btns = g_last_btns;
    if (g_btn_irq) {
        g_btn_irq = false;
        btns = read_buttons();
    }
    uint8_t pressed = btns & ~g_last_btns;  // rising-edge detection

    g_last_btns = btns;
    handle_buttons(pressed);

    if (g_recv_flag && g_state == STATE_ESPNOW) {
        g_recv_flag    = false;
        g_needs_redraw = true;
    }

    // Send pong reply from main loop (safe context)
    {
        uint8_t local_pong_mac[6];
        bool do_pong = false;
        portENTER_CRITICAL(&g_espnow_mux);
        if (g_pong_flag && g_espnow_up) {
            do_pong = true;
            memcpy(local_pong_mac, g_pong_mac, 6);
            g_pong_flag = false;
        }
        portEXIT_CRITICAL(&g_espnow_mux);
        if (do_pong) {
            ensure_unicast_peer(local_pong_mac);
            BeaconMsg pong = {};
            pong.type = MSG_PONG;
            strncpy(pong.name, g_callsign, sizeof(pong.name));
            WiFi.macAddress(pong.mac);
            pong.counter = ++g_send_count;
            esp_now_send(local_pong_mac, (uint8_t*)&pong, sizeof(pong));
            Serial.println("[espnow] sent pong");
        }
    }

    // CTF: auto-respond to capture challenges (copy shared state under lock)
    {
        BeaconMsg local_chal = {};
        uint8_t   local_chal_mac[6];
        bool do_challenge = false;
        portENTER_CRITICAL(&g_espnow_mux);
        if (g_challenge_flag && g_espnow_up) {
            do_challenge = true;
            memcpy(&local_chal,     &g_challenge_msg, sizeof(BeaconMsg));
            memcpy(local_chal_mac,   g_challenge_mac,  6);
            g_challenge_flag = false;
        }
        portEXIT_CRITICAL(&g_espnow_mux);
        if (do_challenge) {
            g_captured_count++;
            BeaconMsg tok = {};
            tok.type = MSG_CAP_TOKEN;
            strncpy(tok.name, g_callsign, sizeof(tok.name));
            WiFi.macAddress(tok.mac);
            tok.counter = ++g_send_count;
            memcpy(tok.text, local_chal.text, 32);
            if (g_atecc_ok) {
                uint8_t sig_data[38];
                memcpy(sig_data,      local_chal.text, 32);
                memcpy(sig_data + 32, local_chal_mac,  6);
                uint8_t digest[32];
                mbedtls_sha256(sig_data, 38, digest, 0);
                g_atecc.wakeUp();
                if (g_atecc.createSignature(digest, 0)) {
                    memcpy(tok.pubkey, g_atecc_pubkey, PUBLIC_KEY_SIZE);
                    memcpy(tok.sig,    g_atecc.signature, SIGNATURE_SIZE);
                }
                g_atecc.sleep();
            }
            ensure_unicast_peer(local_chal_mac);
            esp_now_send(local_chal_mac, (uint8_t*)&tok, sizeof(tok));
            Serial.printf("[ctf] sent capture token to %s\n", local_chal.name);
        }
    }

    // CTF: capture timeout (5 seconds)
    if (g_cap_pending && (millis() - g_cap_sent_at > 5000)) {
        g_cap_pending   = false;
        g_cap_peer_idx  = -1;
        Serial.println("[ctf] capture timed out");
        if (g_state == STATE_ESPNOW && g_espnow_tab == 3) g_needs_redraw = true;
    }

    // ECDSA verification (deferred from on_recv callback to main loop)
    // Copy shared state under lock before doing slow crypto operations
    {
        BeaconMsg local_verify = {};
        uint8_t   local_ver_mac[6];
        bool do_verify = false;
        portENTER_CRITICAL(&g_espnow_mux);
        if (g_needs_verify && g_atecc_ok) {
            do_verify = true;
            memcpy(&local_verify,   &g_pending_verify,  sizeof(BeaconMsg));
            memcpy(local_ver_mac,    g_pending_ver_mac,  6);
            g_needs_verify = false;
        }
        portEXIT_CRITICAL(&g_espnow_mux);
        if (do_verify) {
            uint8_t digest[32];
            mbedtls_sha256((const uint8_t*)&local_verify, BEACON_SIGN_LEN, digest, 0);
            g_atecc.wakeUp();
            bool ok = g_atecc.verifySignature(digest, local_verify.sig, local_verify.pubkey);
            g_atecc.sleep();
            for (int i = 0; i < g_peer_count; i++) {
                if (memcmp(g_peers[i].mac, local_ver_mac, 6) == 0) {
                    g_peers[i].verified = ok;
                    memcpy(g_peers[i].pubkey, local_verify.pubkey, PUBLIC_KEY_SIZE);
                    break;
                }
            }
            Serial.printf("[atecc] verify from %s: %s\n",
                local_verify.name, ok ? "OK" : "FAIL");
            if (g_state == STATE_ESPNOW && g_espnow_tab == 1)
                g_needs_redraw = true;
        }
    }

    if (millis() - s_last_activity >= SLEEP_AFTER_MS) {
        go_to_sleep();
    }

    delay(2);
}
