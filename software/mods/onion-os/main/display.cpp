#include "display.h"
#include "badge_state.h"
#include "ui.h"
#include <SPI.h>
#include <SPIFFS.h>
#include <Wire.h>
#include <algorithm>

void initPeripherals() {
    pinMode(PIN_PWR, OUTPUT);
    digitalWrite(PIN_PWR, LOW);

    pinMode(PIN_SE_EN, OUTPUT);
    digitalWrite(PIN_SE_EN, HIGH);
    delay(50);

    Wire.begin(PIN_SDA, PIN_SCL);
    Wire.setClock(100000);

    Wire.beginTransmission(TCA9534_ADDR);
    Wire.write(TCA9534_CONFIG);
    Wire.write(0xFF);
    Wire.endTransmission();

    SPI.begin(PIN_EPD_SCK, -1, PIN_EPD_MOSI, PIN_EPD_CS);
    display.init(SERIAL_BAUD, true, 10, false);
    display.setRotation(1);

#if PIN_BATTERY_ADC >= 0
    pinMode(PIN_BATTERY_ADC, INPUT);
    analogReadResolution(12);
    analogSetPinAttenuation(PIN_BATTERY_ADC, ADC_11db);
#endif

    SPIFFS.begin(true);
}

uint8_t readButtons() {
    Wire.beginTransmission(TCA9534_ADDR);
    Wire.write(TCA9534_INPUT);
    if (Wire.endTransmission(false) != 0) return 0;
    if (Wire.requestFrom(TCA9534_ADDR, 1) != 1) return 0;
    return (~Wire.read()) & 0x3F;
}

void printLine(const char* text, int y, const GFXfont* font) {
    g_frame.setFont(font);
    g_frame.setTextColor(GxEPD_BLACK);
    g_frame.setCursor(6, y);
    g_frame.print(text);
}

void printString(const String& text, int y, const GFXfont* font) {
    printLine(text.c_str(), y, font);
}

String clipped(const String& value, size_t len) {
    if (value.length() <= len) return value;
    return value.substring(0, len - 3) + "...";
}

void sampleBattery(bool force) {
#if PIN_BATTERY_ADC >= 0
    struct CurvePoint {
        int mv;
        int percent;
    };
    static const CurvePoint curve[] = {
        {4200, 100},
        {4110, 90},
        {4020, 80},
        {3920, 70},
        {3840, 60},
        {3790, 50},
        {3750, 40},
        {3710, 30},
        {3670, 20},
        {3610, 10},
        {3300, 0},
    };

    uint32_t now = millis();
    if (!force && now - g_lastBatterySample < BATTERY_SAMPLE_INTERVAL_MS) return;
    g_lastBatterySample = now;

    analogRead(PIN_BATTERY_ADC);
    uint32_t pinMv = 0;
    const int samples = 8;
    for (int i = 0; i < samples; ++i) {
        pinMv += analogReadMilliVolts(PIN_BATTERY_ADC);
        delay(2);
    }
    pinMv /= samples;

    int batteryMv = (int)lroundf((float)pinMv * BATTERY_ADC_DIVIDER_RATIO) + BATTERY_ADC_OFFSET_MV;
    int percent = 0;
    if (batteryMv >= curve[0].mv) {
        percent = 100;
    } else {
        for (size_t i = 1; i < sizeof(curve) / sizeof(curve[0]); ++i) {
            if (batteryMv >= curve[i].mv) {
                int highMv = curve[i - 1].mv;
                int lowMv = curve[i].mv;
                int highPct = curve[i - 1].percent;
                int lowPct = curve[i].percent;
                percent = lowPct + ((batteryMv - lowMv) * (highPct - lowPct)) / (highMv - lowMv);
                break;
            }
        }
    }
    bool shouldRedraw = force || g_batteryDisplayedPercent < 0 ||
        abs(percent - g_batteryDisplayedPercent) >= BATTERY_REDRAW_PERCENT_STEP;

    g_batteryVoltageMv = batteryMv;
    g_batteryPercent = percent;
    if (shouldRedraw) {
        g_batteryDisplayedPercent = percent;
        if (g_screen == SCREEN_STATUS) g_needsRedraw = true;
    }
#else
    (void)force;
#endif
}

String storedScriptDisplayName(const String& path) {
    String name = path;
    if (name.startsWith("/")) name.remove(0, 1);
    if (name.startsWith("scripts_")) name.remove(0, 8);
    return name;
}

bool validAssetFileName(const String& name, const char* requiredSuffix) {
    if (!name.length() || name.length() > 96 ||
        name.indexOf('/') >= 0 || name.indexOf('\\') >= 0) return false;
    if (requiredSuffix && !name.endsWith(requiredSuffix)) return false;
    for (size_t i = 0; i < name.length(); ++i) {
        char ch = name[i];
        bool ok = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
            (ch >= '0' && ch <= '9') || ch == '.' || ch == '-' || ch == '_';
        if (!ok) return false;
    }
    return true;
}

bool validImageFileName(const String& name) {
    if (!validAssetFileName(name)) return false;
    return name.endsWith(".pbm") || name.endsWith(".bmp");
}

String imagePathForName(const String& name) {
    if (!validImageFileName(name)) return String();
    return "/images_" + name;
}

String normalizedSpiffsPath(const String& path) {
    if (path.startsWith("/")) return path;
    return "/" + path;
}

void refreshScriptList() {
    g_scripts.clear();
    File root = SPIFFS.open("/");
    if (!root) return;

    File file = root.openNextFile();
    while (file) {
        String name = file.name();
        if (!file.isDirectory() &&
            (name.startsWith("/scripts_") || name.startsWith("scripts_")) &&
            name.endsWith(".lua")) {
            g_scripts.push_back(name.startsWith("/") ? name : "/" + name);
        }
        file = root.openNextFile();
    }

    std::sort(g_scripts.begin(), g_scripts.end(), [](const String& a, const String& b) {
        return strcmp(a.c_str(), b.c_str()) < 0;
    });
    if (g_scriptSelection > (int)g_scripts.size()) g_scriptSelection = (int)g_scripts.size();
}

void flushFrame() {
    const uint8_t* cur = g_frame.getBuffer();

    bool screenChanged = (g_screen != g_lastScreen);
    g_lastScreen = g_screen;

    int y0 = ONION_DISPLAY_HEIGHT, y1 = -1;
    int bx0 = FRAME_BPR, bx1 = -1;
    for (int y = 0; y < ONION_DISPLAY_HEIGHT; ++y) {
        for (int bx = 0; bx < FRAME_BPR; ++bx) {
            if (cur[y * FRAME_BPR + bx] != g_prevFrame[y * FRAME_BPR + bx]) {
                if (y  < y0)  y0  = y;
                if (y  > y1)  y1  = y;
                if (bx < bx0) bx0 = bx;
                if (bx > bx1) bx1 = bx;
            }
        }
    }

    if (y1 < 0) return;

    int dw = (bx1 - bx0 + 1) * 8;
    int dh = y1 - y0 + 1;
    float dirtyPct = (float)(dw * dh) / (float)(ONION_DISPLAY_WIDTH * ONION_DISPLAY_HEIGHT);

    bool fullRefresh = g_forceFullRefresh || screenChanged ||
                       dirtyPct > 0.75f   || g_partialCount >= 30;

    if (fullRefresh) {
        display.setFullWindow();
        display.firstPage();
        do {
            display.drawBitmap(0, 0, cur,
                ONION_DISPLAY_WIDTH, ONION_DISPLAY_HEIGHT,
                GxEPD_WHITE, GxEPD_BLACK);
        } while (display.nextPage());
        g_partialCount    = 0;
        g_forceFullRefresh = false;
    } else {
        int px0 = bx0 * 8;
        int pw  = dw;
        if (px0 + pw > ONION_DISPLAY_WIDTH) pw = ONION_DISPLAY_WIDTH - px0;
        display.setPartialWindow(px0, y0, pw, dh);
        display.firstPage();
        do {
            display.drawBitmap(0, 0, cur,
                ONION_DISPLAY_WIDTH, ONION_DISPLAY_HEIGHT,
                GxEPD_WHITE, GxEPD_BLACK);
        } while (display.nextPage());
        ++g_partialCount;
        display.powerOff();
    }

    memcpy(g_prevFrame, cur, FRAME_BYTES);
}

void redraw() {
    if (!g_needsRedraw) return;
    if (g_screen == SCREEN_BOOT_SPLASH)      drawBootSplash();
    else if (g_screen == SCREEN_LINK_PROMPT)     drawLinkPrompt();
    else if (g_screen == SCREEN_SCRIPT_EXPLORER) drawScriptExplorer();
    else if (g_screen == SCREEN_TX_PROMPT)       drawTransactionPrompt();
    else if (g_screen == SCREEN_LUA_PROMPT)      drawLuaPrompt();
    else if (g_screen == SCREEN_CHECKIN_PROMPT)  drawCheckInPrompt();
    else if (g_screen == SCREEN_CHECKIN_RESULT)  drawCheckInResult();
    else if (g_screen == SCREEN_SETTINGS)        drawSettingsScreen();
    else if (g_screen == SCREEN_WIFI_OVERVIEW)   drawWifiOverview();
    else if (g_screen == SCREEN_WIFI_SCANNING)   drawWifiScanning();
    else if (g_screen == SCREEN_WIFI_LIST)       drawWifiList();
    else if (g_screen == SCREEN_WIFI_PASSWORD)   drawWifiPassword();
    else if (g_screen == SCREEN_WIFI_CONNECTING) drawWifiConnecting();
    else if (g_screen == SCREEN_WIFI_RESULT)     drawWifiResult();
    else if (g_screen == SCREEN_DELETE_CONFIRM)  drawDeleteConfirm();
    else if (g_screen == SCREEN_ABOUT)           drawAboutScreen();
    else drawStatus();
    flushFrame();
    g_needsRedraw = false;
}
