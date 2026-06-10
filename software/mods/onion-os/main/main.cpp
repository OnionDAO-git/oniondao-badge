#include "badge_state.h"
#include "display.h"
#include "wifi.h"
#include "mqtt.h"
#include "espnow.h"
#include "solana.h"
#include "serial_cmd.h"
#include "buttons.h"

extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}

void setup() {
    Serial.begin(SERIAL_BAUD);
    delay(500);
    printHelp();

    if (sodium_init() < 0) {
        Serial.println("libsodium init failed");
    }
    loadConfig();
    initPeripherals();
    g_screen = SCREEN_BOOT_SPLASH;
    g_needsRedraw = true;
    redraw();
    delay(BOOT_SPLASH_MS);
    sampleBattery(true);
    g_screen = SCREEN_STATUS;
    g_needsRedraw = true;
    redraw();
    String keyError;
    if (loadOrCreateSolanaKey(false, keyError)) {
        setLog("Onion OS ready");
    } else {
        setLog("Wallet locked: " + clipped(keyError, 18));
    }
    ensureWifi();
    doHttpHandshake();
    refreshPublicProfile();  // pull the current balance once at boot
}

void loop() {
    handleSerial();
    handleButtons();
    sampleBattery();

    // Handle WiFi worker state transitions
    int wr = g_wifiWorkerResult.load();
    if (g_screen == SCREEN_WIFI_SCANNING) {
        if (wr == WIFI_WORKER_DONE) {
            g_wifiWorkerResult.store(WIFI_WORKER_IDLE);
            g_wifiListSel = 0;
            g_screen = SCREEN_WIFI_LIST;
            g_needsRedraw = true;
        } else if (wr == WIFI_WORKER_FAILED) {
            g_wifiWorkerResult.store(WIFI_WORKER_IDLE);
            g_wifiResultMsg = "Scan failed";
            g_screen = SCREEN_WIFI_RESULT;
            g_needsRedraw = true;
        }
    } else if (g_screen == SCREEN_WIFI_CONNECTING) {
        if (wr == WIFI_WORKER_DONE) {
            g_wifiWorkerResult.store(WIFI_WORKER_IDLE);
            g_prefs.putString("wifi_ssid", g_wifiConnectSsid.c_str());
            g_prefs.putString("wifi_pass", g_wifiPassBuf);
            g_config.wifiSsid = g_wifiConnectSsid;
            g_config.wifiPassword = String(g_wifiPassBuf);
            setLog("WiFi connected");
            g_wifiResultMsg = "Connected!";
            g_screen = SCREEN_WIFI_RESULT;
            g_needsRedraw = true;
        } else if (wr == WIFI_WORKER_FAILED) {
            g_wifiWorkerResult.store(WIFI_WORKER_IDLE);
            g_wifiResultMsg = "Connect failed";
            g_screen = SCREEN_WIFI_RESULT;
            g_needsRedraw = true;
        }
    }

    if (WiFi.status() != WL_CONNECTED && millis() - g_lastWifiAttempt > 30000) {
        g_lastWifiAttempt = millis();
        triggerWifiReconnect();
    }
    ensureMqtt();
    processCheckInService();

    if (millis() - g_lastHandshake > HANDSHAKE_INTERVAL_MS) {
        g_lastHandshake = millis();
        if (g_mqttConnected) doMqttHandshake();
    }

    // Periodic Onion-balance refresh.
    if (WiFi.status() == WL_CONNECTED &&
        g_wifiWorkerResult.load() != WIFI_WORKER_RUNNING &&
        millis() - g_lastProfileRefresh > PROFILE_REFRESH_INTERVAL_MS) {
        g_lastProfileRefresh = millis();
        refreshPublicProfile(true);
    }

    redraw();
    delay(20);
}
