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
enum MsgType : uint8_t { MSG_BEACON, MSG_PING, MSG_PONG, MSG_TEXT };

struct BeaconMsg {
    MsgType  type;
    char     name[16];
    uint8_t  mac[6];
    uint32_t counter;
    char     text[32];
};

struct PeerEntry {
    uint8_t  mac[6];
    char     name[16];
    int8_t   rssi;
    uint32_t last_seen;
};

struct InboxEntry {
    char     name[16];
    char     text[32];
    uint32_t counter;
};

static volatile bool g_recv_flag   = false;
static BeaconMsg     g_last_recv   = {};
static uint32_t      g_send_count  = 0;
static bool          g_espnow_up   = false;
static char          g_callsign[16] = {};
static PeerEntry     g_peers[10]   = {};
static int           g_peer_count  = 0;
static InboxEntry    g_inbox[5]    = {};
static int           g_inbox_count = 0;
static int           g_espnow_tab  = 0;  // 0=BEACON 1=PEERS 2=INBOX
static int           g_peers_cursor = 0;
static int           g_inbox_cursor = 0;

// ── App state ─────────────────────────────────────────────────────────────────
enum AppState {
    STATE_HOME,
    STATE_MENU,
    STATE_SYS_INFO,
    STATE_BTN_TEST,
    STATE_I2C_SCAN,
    STATE_RNG,
    STATE_DISPLAY_TEST,
    STATE_GUIDE,
    STATE_ESPNOW,
};

static AppState g_state        = STATE_MENU;
static int      g_cursor       = 0;
static bool     g_needs_redraw = true;
static uint8_t  g_last_btns    = 0;

// Button test: tracks currently-held buttons for display
static uint8_t  g_btn_display  = 0;

// Display test: current pattern index
static int      g_disp_pattern = 0;

// RNG: 16 bytes to display
static uint8_t  g_rng_bytes[16];

// I2C scan results
static uint8_t  g_i2c_addrs[20];
static int      g_i2c_count = 0;

// Guide: pointer to active guide, count, current page
static const struct GuideScreen* g_guide_src   = nullptr;
static int                       g_guide_count = 0;
static int                       g_guide_page  = 0;

#define MENU_COUNT 8
static const char* MENU_LABELS[MENU_COUNT] = {
    "1. System Info",
    "2. Button Test",
    "3. I2C Scanner",
    "4. RNG / Crypto",
    "5. Display Test",
    "6. Hardware Guide",
    "7. Software Guide",
    "8. ESPNow Beacon",
};

// ── Guide data ────────────────────────────────────────────────────────────────
// Line length limit: 23 chars (23 * 11px xAdvance = 253px < 264px display)
// Line spacing: 18px (= yAdvance of FreeMono9pt7b — prevents vertical overlap)

struct GuideScreen {
    const char* title;
    const char* lines[6];  // nullptr = end of content
};

static const GuideScreen HW_GUIDE[] = {
    {
        "ABOUT THIS BADGE",
        {
            "ESP32-S3 badge with:",
            "264x176 e-paper display",
            "6 buttons via I2C",
            "Crypto secure element",
            "CC1101 radio module",
            "LFT/RGT: flip pages",
        }
    },
    {
        "THE MCU",
        {
            "ESP32-S3 -- the brain",
            "Dual Xtensa LX7 cores",
            "Up to 240 MHz clock",
            "512KB internal SRAM",
            "8MB OPI PSRAM external",
            "WiFi + BLE built in",
        }
    },
    {
        "GPIO PINS",
        {
            "General Purpose I/O",
            "Each pin: in or output",
            "HIGH = 3.3V  LOW = 0V",
            "Set HIGH: turn on LED",
            "Read pin: detect input",
            "ESP32-S3 has 45 GPIOs",
        }
    },
    {
        "SPI BUS",
        {
            "Serial Peripheral IF",
            "CLK, MOSI, MISO, CS",
            "Master drives the clock",
            "Transfers bits serially",
            "Fast: 4+ MHz typical",
            "Badge: MCU -> SSD1680",
        }
    },
    {
        "E-PAPER DISPLAY",
        {
            "Capsules of charged ink",
            "Black/white by E-field",
            "SSD1680 controller chip",
            "264x176 pixels, 1-bit",
            "~1.5s full refresh",
            "Holds image: no power!",
        }
    },
    {
        "I2C BUS",
        {
            "I2C = 2-wire serial",
            "SCL=clock  SDA=data",
            "Devices have addresses",
            "Master picks by address",
            "Slower: ~100-400 kHz",
            "Badge: 0x20 and 0x60",
        }
    },
    {
        "TCA9534 EXPANDER",
        {
            "I2C I/O expander chip",
            "Adds 8 GPIO via 2 wires",
            "Saves 6 MCU pins",
            "Buttons: active-LOW",
            "Pressed = reads LOW",
            "~btns = active-HIGH",
        }
    },
    {
        "ATECC608B CRYPTO",
        {
            "Hardware secure element",
            "Keys stored inside chip",
            "Keys never leave the IC",
            "ECDSA, ECDH, SHA-256",
            "HW TRNG: true random",
            "I2C address: 0x60",
        }
    },
    {
        "DEEP SLEEP",
        {
            "ESP32 power modes:",
            "Active:  ~240 mA",
            "Modem:   ~20 mA",
            "Light:   ~2 mA",
            "Deep:    <10 uA (!)",
            "Wake: button GPIO edge",
        }
    },
    {
        "BOARD OVERVIEW",
        {
            "SPI: CLK=11 MOSI=17",
            "     CS=12  DC=13",
            "I2C: SCL=9  SDA=10",
            "Buttons: TCA9534 @0x20",
            "Crypto: ATECC608B @0x60",
            "Power:GPIO18 Wake:GPIO1",
        }
    },
};

static const GuideScreen SW_GUIDE[] = {
    {
        "C++ ON EMBEDDED",
        {
            "Runs as machine code",
            "No OS, bare metal",
            "You control every byte",
            "Arduino wraps hardware",
            "main.cpp: our whole app",
            "Entry: setup() + loop()",
        }
    },
    {
        "INCLUDES & HEADERS",
        {
            "#include loads a header",
            "<Wire.h>  = I2C library",
            "<SPI.h>   = SPI library",
            "<Arduino.h> = core API",
            "badge_pins.h = our pins",
            ".h=header .cpp=source",
        }
    },
    {
        "#DEFINE & MACROS",
        {
            "#define = text replace",
            "Done before compiling",
            "No type, no memory used",
            "#define BTN_UP (1<<2)",
            "Equals 0b00000100 = 4",
            "Avoids 'magic numbers'",
        }
    },
    {
        "VARIABLES & TYPES",
        {
            "uint8_t  = 8-bit 0-255",
            "uint32_t = 32-bit 0..4B",
            "bool     = true/false",
            "static   = persistent",
            "g_ prefix = global var",
            "Types matter on HW!",
        }
    },
    {
        "BITWISE OPERATORS",
        {
            "Buttons = 1 byte value",
            "(1<<2) = 0b00000100",
            "btns & BTN_UP  // test",
            "btns | BTN_UP  // set",
            "~btns       // invert",
            "Active-LOW: pressed=0V",
        }
    },
    {
        "FUNCTIONS & SCOPE",
        {
            "static void fn_name() {",
            "  // renders content",
            "}",
            "static = only this file",
            "void = returns nothing",
            "Used in dispatch_render",
        }
    },
    {
        "ENUM & STATE MACHINE",
        {
            "enum AppState {",
            "  STATE_MENU,",
            "  STATE_SYS_INFO, ...",
            "};",
            "g_state = active screen",
            "switch() routes to draw",
        }
    },
    {
        "SETUP & LOOP",
        {
            "Arduino entry points:",
            "setup(): runs once",
            "  - init pins, display",
            "  - set g_state = MENU",
            "loop():  runs forever",
            "  - poll btns, redraw",
        }
    },
    {
        "DISPATCH PATTERN",
        {
            "g_needs_redraw = true",
            "  = redraw needed",
            "dispatch_render() calls",
            "  the right draw_ fn",
            "Separates what/how:",
            "logic > state > render",
        }
    },
    {
        "STRUCTS & ARRAYS",
        {
            "struct GuideScreen {",
            "  const char* title;",
            "  const char* lines[6];",
            "};",
            "HW_GUIDE[10] stores all",
            "10 guide screens on HW",
        }
    },
};

#define HW_GUIDE_COUNT 10
#define SW_GUIDE_COUNT 10

// ── Hardware init ─────────────────────────────────────────────────────────────

static void init_peripherals() {
    pinMode(PIN_PWR, OUTPUT);
    digitalWrite(PIN_PWR, HIGH);

    pinMode(PIN_SE_EN, OUTPUT);
    digitalWrite(PIN_SE_EN, HIGH);

    delay(50);  // let regulators settle

    Wire.begin(PIN_SDA, PIN_SCL);
    Wire.setClock(100000);  // 100 kHz — required for ATECC608B

    Wire.beginTransmission(TCA9534_ADDR);
    Wire.write(TCA9534_CONFIG);
    Wire.write(0xFF);  // all pins = inputs
    Wire.endTransmission();

    SPI.begin(PIN_EPD_SCK, /*MISO*/-1, PIN_EPD_MOSI, PIN_EPD_CS);
    display.init(115200, true, 10, false);
    display.setRotation(1);  // landscape: 264 wide × 176 tall
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

    Serial.println("[null badge] sleeping — press any button to wake.");
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
        display.print("NULL CITY BADGE");
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
        page_header("NULL CITY BADGE");
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

static void draw_sys_info() {
    display.setFullWindow();
    display.firstPage();
    do {
        page_header("SYSTEM INFO");
        display.setFont(&FreeMono9pt7b);
        display.setTextColor(GxEPD_BLACK);

        char buf[36];
        int y = 50;

        snprintf(buf, sizeof(buf), "Chip:  %s", ESP.getChipModel());
        display.setCursor(8, y); display.print(buf); y += 18;

        snprintf(buf, sizeof(buf), "Cores: %d @ %d MHz",
                 ESP.getChipCores(), (int)ESP.getCpuFreqMHz());
        display.setCursor(8, y); display.print(buf); y += 18;

        snprintf(buf, sizeof(buf), "Heap:  %u B free",
                 (unsigned)ESP.getFreeHeap());
        display.setCursor(8, y); display.print(buf); y += 18;

        uint32_t psram = ESP.getPsramSize();
        if (psram > 0)
            snprintf(buf, sizeof(buf), "PSRAM: %u MB", (unsigned)(psram / 1024 / 1024));
        else
            snprintf(buf, sizeof(buf), "PSRAM: not enabled");
        display.setCursor(8, y); display.print(buf); y += 18;

        snprintf(buf, sizeof(buf), "Flash: %u MB",
                 (unsigned)(ESP.getFlashChipSize() / 1024 / 1024));
        display.setCursor(8, y); display.print(buf);

        page_footer("CXL: back to menu");
    } while (display.nextPage());
    display.hibernate();
}

// ── Draw: button test ─────────────────────────────────────────────────────────

static void draw_btn_test() {
    display.setFullWindow();
    display.firstPage();
    do {
        page_header("BUTTON TEST");
        display.setFont(&FreeMono9pt7b);
        display.setTextColor(GxEPD_BLACK);

        // Two columns, three rows — label + filled/empty square
        struct { const char* label; uint8_t mask; int col; int row; } btndef[] = {
            {"UP  ", BTN_UP,     0, 0},
            {"DWN ", BTN_DOWN,   1, 0},
            {"LFT ", BTN_LEFT,   0, 1},
            {"RGT ", BTN_RIGHT,  1, 1},
            {"SEL ", BTN_SELECT, 0, 2},
            {"CXL ", BTN_CANCEL, 1, 2},
        };

        for (auto& b : btndef) {
            int x = (b.col == 0) ? 8 : 140;
            int y = 54 + b.row * 30;
            display.setCursor(x, y);
            display.print(b.label);
            if (g_btn_display & b.mask)
                display.fillRect(x + 50, y - 12, 14, 14, GxEPD_BLACK);
            else
                display.drawRect(x + 50, y - 12, 14, 14, GxEPD_BLACK);
        }

        page_footer("Press buttons. CXL:back");
    } while (display.nextPage());
    display.hibernate();
}

// ── Draw: I2C scanner ─────────────────────────────────────────────────────────

static const char* i2c_device_name(uint8_t addr) {
    if (addr == 0x20) return "TCA9534  (buttons)";
    if (addr == 0x60) return "ATECC608B (crypto)";
    return "(unknown)";
}

static void run_i2c_scan() {
    g_i2c_count = 0;
    for (uint8_t a = 1; a < 127 && g_i2c_count < 20; a++) {
        Wire.beginTransmission(a);
        if (Wire.endTransmission() == 0)
            g_i2c_addrs[g_i2c_count++] = a;
    }
}

static void draw_i2c_scan() {
    display.setFullWindow();
    display.firstPage();
    do {
        page_header("I2C SCANNER");
        display.setFont(&FreeMono9pt7b);
        display.setTextColor(GxEPD_BLACK);

        if (g_i2c_count == 0) {
            display.setCursor(8, 54);
            display.print("No devices found.");
        } else {
            int y = 50;
            for (int i = 0; i < g_i2c_count && y < 138; i++, y += 18) {
                char buf[36];
                snprintf(buf, sizeof(buf), "0x%02X  %s",
                         g_i2c_addrs[i], i2c_device_name(g_i2c_addrs[i]));
                display.setCursor(8, y);
                display.print(buf);
            }
        }

        page_footer("CXL: back to menu");
    } while (display.nextPage());
    display.hibernate();
}

// ── Draw: RNG / Crypto ────────────────────────────────────────────────────────

static void gen_rng() {
    for (int i = 0; i < 4; i++) {
        uint32_t r = esp_random();
        memcpy(&g_rng_bytes[i * 4], &r, 4);
    }
}

static void draw_rng() {
    display.setFullWindow();
    display.firstPage();
    do {
        page_header("RNG / CRYPTO");
        display.setFont(&FreeMono9pt7b);
        display.setTextColor(GxEPD_BLACK);

        display.setCursor(8, 50);
        display.print("ATECC608B @ 0x60");

        display.setCursor(8, 70);
        display.print("HW random (ESP32 TRNG):");

        // 16 bytes as 4 rows of 4 bytes
        for (int row = 0; row < 4; row++) {
            char buf[16];
            snprintf(buf, sizeof(buf), "%02X %02X %02X %02X",
                     g_rng_bytes[row*4+0], g_rng_bytes[row*4+1],
                     g_rng_bytes[row*4+2], g_rng_bytes[row*4+3]);
            display.setCursor(8, 92 + row * 14);
            display.print(buf);
        }

        page_footer("SEL:regen  CXL:back");
    } while (display.nextPage());
    display.hibernate();
}

// ── Draw: display test ────────────────────────────────────────────────────────

#define PATTERN_COUNT 4
static const char* PATTERN_NAMES[PATTERN_COUNT] = {
    "Checkerboard",
    "Horiz stripes",
    "Vert stripes",
    "Border+cross",
};

static void draw_display_test() {
    display.setFullWindow();
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);

        switch (g_disp_pattern) {
            case 0:  // Checkerboard — 8×8 blocks
                for (int by = 0; by < 22; by++)
                    for (int bx = 0; bx < 33; bx++)
                        if ((bx + by) % 2 == 0)
                            display.fillRect(bx*8, by*8, 8, 8, GxEPD_BLACK);
                break;

            case 1:  // Horizontal stripes — 8px on, 8px off
                for (int y = 0; y < 176; y += 16)
                    display.fillRect(0, y, 264, 8, GxEPD_BLACK);
                break;

            case 2:  // Vertical stripes — 8px on, 8px off
                for (int x = 0; x < 264; x += 16)
                    display.fillRect(x, 0, 8, 176, GxEPD_BLACK);
                break;

            case 3:  // Double border + X diagonals
                display.drawRect(0, 0, 264, 176, GxEPD_BLACK);
                display.drawRect(2, 2, 260, 172, GxEPD_BLACK);
                display.drawLine(0, 0, 263, 175, GxEPD_BLACK);
                display.drawLine(263, 0, 0, 175, GxEPD_BLACK);
                break;
        }

        // Overlay footer
        display.fillRect(0, 156, 264, 20, GxEPD_WHITE);
        display.drawFastHLine(0, 156, 264, GxEPD_BLACK);
        display.setFont(&FreeMono9pt7b);
        display.setTextColor(GxEPD_BLACK);
        char buf[28];
        snprintf(buf, sizeof(buf), "%d/%d: %-12s L/R",
                 g_disp_pattern + 1, PATTERN_COUNT, PATTERN_NAMES[g_disp_pattern]);
        display.setCursor(8, 171);
        display.print(buf);
    } while (display.nextPage());
    display.hibernate();
}

// ── Draw: guide ───────────────────────────────────────────────────────────────

static void draw_guide() {
    const GuideScreen& scr = g_guide_src[g_guide_page];

    display.setFullWindow();
    display.firstPage();
    do {
        guide_header(scr.title, g_guide_page, g_guide_count);

        display.setFont(&FreeMono9pt7b);
        display.setTextColor(GxEPD_BLACK);

        // 18px spacing = yAdvance of FreeMono9pt7b, no vertical overlap
        int y = 50;
        for (int i = 0; i < 6 && scr.lines[i] != nullptr; i++, y += 18) {
            display.setCursor(8, y);
            display.print(scr.lines[i]);
        }

        page_footer("LFT/RGT  CXL:back");
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
            g_peers[i].rssi      = rssi;
            g_peers[i].last_seen = millis();
            return;
        }
    }
    if (g_peer_count < 10) {
        memcpy(g_peers[g_peer_count].mac,  mac,      6);
        memcpy(g_peers[g_peer_count].name, msg.name, sizeof(g_peers[0].name));
        g_peers[g_peer_count].rssi      = rssi;
        g_peers[g_peer_count].last_seen = millis();
        g_peer_count++;
    }
}

static void on_recv(const uint8_t* mac, const uint8_t* data, int len) {
    if ((size_t)len == sizeof(BeaconMsg)) {
        BeaconMsg msg;
        memcpy(&msg, data, sizeof(BeaconMsg));
        upsert_peer(mac, msg, -50);
        memcpy((void*)&g_last_recv, &msg, sizeof(BeaconMsg));

        // Push to inbox (ring buffer — oldest entry dropped when full)
        int slot = g_inbox_count < 5 ? g_inbox_count : 4;
        if (g_inbox_count >= 5)
            memmove(&g_inbox[1], &g_inbox[0], sizeof(InboxEntry) * 4);
        else
            g_inbox_count++;
        strncpy(g_inbox[0].name,    msg.name, sizeof(g_inbox[0].name));
        strncpy(g_inbox[0].text,    msg.text, sizeof(g_inbox[0].text));
        g_inbox[0].counter = msg.counter;
        (void)slot;

        g_recv_flag = true;
        Serial.printf("[espnow] recv from %s (%02X:%02X:%02X:%02X:%02X:%02X) #%u\n",
            msg.name, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], msg.counter);
    }
}

static void send_beacon() {
    BeaconMsg msg = {};
    msg.type = MSG_BEACON;
    strncpy(msg.name, g_callsign, sizeof(msg.name));
    WiFi.macAddress(msg.mac);
    msg.counter = ++g_send_count;
    uint8_t broadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    esp_now_send(broadcast, (uint8_t*)&msg, sizeof(msg));
    Serial.printf("[espnow] sent beacon #%u as %s\n", msg.counter, g_callsign);
}

static void shutdown_espnow() {
    if (!g_espnow_up) return;
    esp_now_deinit();
    WiFi.mode(WIFI_OFF);
    g_espnow_up    = false;
    g_peer_count   = 0;
    g_send_count   = 0;
    g_inbox_count  = 0;
    g_espnow_tab   = 0;
    g_peers_cursor = 0;
    g_inbox_cursor = 0;
    memset((void*)&g_last_recv, 0, sizeof(g_last_recv));
    memset(g_inbox, 0, sizeof(g_inbox));
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

    esp_now_peer_info_t peer = {};
    memset(peer.peer_addr, 0xFF, 6);
    peer.channel = 0;
    peer.encrypt = false;
    esp_now_add_peer(&peer);

    g_espnow_up = true;
    Serial.printf("[espnow] ready as %s\n", g_callsign);
}

// ── Draw: ESPNow sub-screens ──────────────────────────────────────────────────

static void espnow_tab_header(const char* title) {
    display.fillScreen(GxEPD_WHITE);
    display.setFont(&FreeMonoBold9pt7b);
    display.setTextColor(GxEPD_BLACK);

    // Tab bar: [BEACON] [PEERS] [INBOX]
    const char* tabs[] = {"BEACON", "PEERS", "INBOX"};
    int x = 0;
    for (int i = 0; i < 3; i++) {
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
    display.setFullWindow();
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

        snprintf(buf, sizeof(buf), "Sent: %-4u  Peers: %d", g_send_count, g_peer_count);
        display.setCursor(8, 68);
        display.print(buf);

        display.drawFastHLine(8, 76, display.width() - 16, GxEPD_BLACK);

        display.setCursor(8, 92);
        display.print("Last recv:");

        if (g_last_recv.mac[0] || g_last_recv.mac[1] || g_last_recv.mac[2]) {
            char peer_mac[18];
            snprintf(peer_mac, sizeof(peer_mac),
                "%02X:%02X:%02X:%02X:%02X:%02X",
                g_last_recv.mac[0], g_last_recv.mac[1], g_last_recv.mac[2],
                g_last_recv.mac[3], g_last_recv.mac[4], g_last_recv.mac[5]);
            display.setCursor(8, 108);
            display.print(g_last_recv.name);
            display.setCursor(8, 124);
            display.print(peer_mac);
        } else {
            display.setCursor(8, 108);
            display.print("(none yet)");
        }

        page_footer("SEL:send LFT/RGT:tab CXL:back");
    } while (display.nextPage());
    display.hibernate();
}

static void draw_espnow_peers() {
    display.setFullWindow();
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
            // Show up to 4 peers, scrollable
            int visible = min(g_peer_count, 4);
            int start   = min(g_peers_cursor, max(0, g_peer_count - visible));
            for (int i = 0; i < visible; i++) {
                int idx = start + i;
                if (idx >= g_peer_count) break;
                int y = 34 + i * 30;

                if (idx == g_peers_cursor) {
                    display.fillRect(0, y - 14, display.width(), 16, GxEPD_BLACK);
                    display.setTextColor(GxEPD_WHITE);
                } else {
                    display.setTextColor(GxEPD_BLACK);
                }

                char buf[32];
                snprintf(buf, sizeof(buf), "%-12s", g_peers[idx].name);
                display.setCursor(8, y);
                display.print(buf);

                display.setTextColor(GxEPD_BLACK);
                char mac_buf[18];
                snprintf(mac_buf, sizeof(mac_buf), "%02X:%02X:%02X:%02X:%02X:%02X",
                    g_peers[idx].mac[0], g_peers[idx].mac[1], g_peers[idx].mac[2],
                    g_peers[idx].mac[3], g_peers[idx].mac[4], g_peers[idx].mac[5]);
                display.setCursor(8, y + 14);
                display.print(mac_buf);
            }
        }

        page_footer("UP/DN:scroll LFT/RGT:tab CXL:back");
    } while (display.nextPage());
    display.hibernate();
}

static void draw_espnow_inbox() {
    display.setFullWindow();
    display.firstPage();
    do {
        espnow_tab_header("INBOX");
        display.setFont(&FreeMono9pt7b);
        display.setTextColor(GxEPD_BLACK);

        if (g_inbox_count == 0) {
            display.setCursor(8, 50);
            display.print("No messages yet.");
        } else {
            int visible = min(g_inbox_count, 3);
            for (int i = 0; i < visible; i++) {
                int y = 34 + i * 42;
                display.drawFastHLine(0, y - 4, display.width(), GxEPD_BLACK);

                char buf[32];
                snprintf(buf, sizeof(buf), "From: %s #%u", g_inbox[i].name, g_inbox[i].counter);
                display.setCursor(8, y + 10);
                display.print(buf);

                if (g_inbox[i].text[0]) {
                    display.setCursor(8, y + 26);
                    display.print(g_inbox[i].text);
                } else {
                    display.setCursor(8, y + 26);
                    display.print("[beacon]");
                }
            }
        }

        page_footer("LFT/RGT:tab  CXL:back");
    } while (display.nextPage());
    display.hibernate();
}

static void draw_espnow() {
    switch (g_espnow_tab) {
        case 0: draw_espnow_beacon(); break;
        case 1: draw_espnow_peers();  break;
        case 2: draw_espnow_inbox();  break;
    }
}

// ── Dispatch ──────────────────────────────────────────────────────────────────

static void dispatch_render() {
    switch (g_state) {
        case STATE_HOME:         draw_home_screen();  break;
        case STATE_MENU:         draw_menu();         break;
        case STATE_SYS_INFO:     draw_sys_info();     break;
        case STATE_BTN_TEST:     draw_btn_test();     break;
        case STATE_I2C_SCAN:     draw_i2c_scan();     break;
        case STATE_RNG:          draw_rng();          break;
        case STATE_DISPLAY_TEST: draw_display_test(); break;
        case STATE_GUIDE:        draw_guide();        break;
        case STATE_ESPNOW:       draw_espnow();       break;
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
                    case 0: g_state = STATE_SYS_INFO; break;
                    case 1: g_btn_display = read_buttons(); g_state = STATE_BTN_TEST; break;
                    case 2: run_i2c_scan(); g_state = STATE_I2C_SCAN; break;
                    case 3: gen_rng();      g_state = STATE_RNG;      break;
                    case 4: g_state = STATE_DISPLAY_TEST; break;
                    case 5:
                        g_guide_src   = HW_GUIDE;
                        g_guide_count = HW_GUIDE_COUNT;
                        g_guide_page  = 0;
                        g_state = STATE_GUIDE;
                        break;
                    case 6:
                        g_guide_src   = SW_GUIDE;
                        g_guide_count = SW_GUIDE_COUNT;
                        g_guide_page  = 0;
                        g_state = STATE_GUIDE;
                        break;
                    case 7:
                        init_espnow();
                        g_state = STATE_ESPNOW;
                        break;
                }
                g_needs_redraw = true;
            }
            if (pressed & BTN_CANCEL) { g_state = STATE_HOME; g_needs_redraw = true; }
            break;

        case STATE_BTN_TEST:
            // State changes are handled by the btns-changed check in loop();
            // only CANCEL exits.
            if (pressed & BTN_CANCEL)
                { g_state = STATE_MENU; g_needs_redraw = true; }
            break;

        case STATE_RNG:
            if (pressed & BTN_SELECT) { gen_rng(); g_needs_redraw = true; }
            if (pressed & BTN_CANCEL) { g_state = STATE_MENU; g_needs_redraw = true; }
            break;

        case STATE_DISPLAY_TEST:
            if (pressed & BTN_LEFT)
                { g_disp_pattern = (g_disp_pattern - 1 + PATTERN_COUNT) % PATTERN_COUNT; g_needs_redraw = true; }
            if (pressed & BTN_RIGHT)
                { g_disp_pattern = (g_disp_pattern + 1) % PATTERN_COUNT; g_needs_redraw = true; }
            if (pressed & BTN_CANCEL)
                { g_state = STATE_MENU; g_needs_redraw = true; }
            break;

        case STATE_GUIDE:
            if (pressed & BTN_LEFT)
                { g_guide_page = (g_guide_page - 1 + g_guide_count) % g_guide_count; g_needs_redraw = true; }
            if (pressed & BTN_RIGHT)
                { g_guide_page = (g_guide_page + 1) % g_guide_count; g_needs_redraw = true; }
            if (pressed & BTN_CANCEL)
                { g_state = STATE_MENU; g_needs_redraw = true; }
            break;

        case STATE_ESPNOW:
            if (pressed & BTN_LEFT)
                { g_espnow_tab = (g_espnow_tab + 2) % 3; g_needs_redraw = true; }
            if (pressed & BTN_RIGHT)
                { g_espnow_tab = (g_espnow_tab + 1) % 3; g_needs_redraw = true; }
            if (pressed & BTN_UP) {
                if (g_espnow_tab == 1 && g_peers_cursor > 0)
                    { g_peers_cursor--; g_needs_redraw = true; }
            }
            if (pressed & BTN_DOWN) {
                if (g_espnow_tab == 1 && g_peers_cursor < g_peer_count - 1)
                    { g_peers_cursor++; g_needs_redraw = true; }
            }
            if (pressed & BTN_SELECT && g_espnow_tab == 0)
                { send_beacon(); g_needs_redraw = true; }
            if (pressed & BTN_CANCEL)
                { shutdown_espnow(); g_state = STATE_MENU; g_needs_redraw = true; }
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

    init_peripherals();

    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();

    if (cause == ESP_SLEEP_WAKEUP_EXT0) {
        Serial.println("[null badge] woke from sleep.");
        display.hibernate();
    } else {
        Serial.println("[null badge] booting...");

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

    Serial.printf("[null badge] awake for %d s\n", SLEEP_AFTER_MS / 1000);
}

// ── Loop ──────────────────────────────────────────────────────────────────────

void loop() {
    check_serial_image();

    if (g_needs_redraw) {
        dispatch_render();
    }

    uint8_t btns    = read_buttons();
    uint8_t pressed = btns & ~g_last_btns;  // rising-edge detection

    // In button test, redraw whenever held-button state changes
    if (g_state == STATE_BTN_TEST && btns != g_last_btns) {
        g_btn_display  = btns;
        g_needs_redraw = true;
    }

    g_last_btns = btns;
    handle_buttons(pressed);

    if (g_recv_flag && g_state == STATE_ESPNOW) {
        g_recv_flag    = false;
        g_needs_redraw = true;
    }

    if (millis() - s_last_activity >= SLEEP_AFTER_MS) {
        go_to_sleep();
    }

    delay(10);
}
