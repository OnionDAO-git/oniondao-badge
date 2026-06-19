#include "wifi.h"
#include "badge_state.h"
#include "display.h"
#include <WiFi.h>
#include <esp_sntp.h>
#include <esp_wifi.h>

void restoreWifiProtocol() {
    esp_wifi_set_protocol(WIFI_IF_STA,
        WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N);
}

bool ensureWifi() {
    if (WiFi.status() == WL_CONNECTED) return true;
    if (g_wifiWorkerResult.load() == WIFI_WORKER_RUNNING) return false;
    if (g_screen == SCREEN_WIFI_OVERVIEW || g_screen == SCREEN_WIFI_SCANNING ||
        g_screen == SCREEN_WIFI_LIST || g_screen == SCREEN_WIFI_PASSWORD ||
        g_screen == SCREEN_WIFI_CONNECTING) return false;
    if (!g_config.wifiSsid.length()) {
        setLog("Provision WiFi in Settings");
        return false;
    }

    WiFi.mode(WIFI_STA);
    restoreWifiProtocol();
    WiFi.begin(g_config.wifiSsid.c_str(), g_config.wifiPassword.c_str());
    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_CONNECT_TIMEOUT_MS) {
        delay(250);
    }

    if (WiFi.status() == WL_CONNECTED) {
        setLog("WiFi connected");
        if (sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET) {
            esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
            esp_sntp_setservername(0, "pool.ntp.org");
            esp_sntp_init();
        }
        return true;
    }
    setLog("WiFi connect failed");
    return false;
}

void triggerWifiReconnect() {
    if (WiFi.status() == WL_CONNECTED) return;
    if (g_wifiWorkerResult.load() == WIFI_WORKER_RUNNING) return;
    if (g_screen == SCREEN_WIFI_OVERVIEW || g_screen == SCREEN_WIFI_SCANNING ||
        g_screen == SCREEN_WIFI_LIST || g_screen == SCREEN_WIFI_PASSWORD ||
        g_screen == SCREEN_WIFI_CONNECTING) return;
    if (!g_config.wifiSsid.length()) return;

    WiFi.mode(WIFI_STA);
    restoreWifiProtocol();
    WiFi.setAutoReconnect(true);
    WiFi.begin(g_config.wifiSsid.c_str(), g_config.wifiPassword.c_str());
    setLog("WiFi reconnecting...");
}

static void wifiScanTask(void*) {
    WiFi.disconnect();
    WiFi.mode(WIFI_STA);
    restoreWifiProtocol();
    int n = WiFi.scanNetworks(false, false);
    g_wifiNetworks.clear();
    if (n >= 0) {
        for (int i = 0; i < n && i < 20; i++) {
            WifiNetwork net;
            String ssid = WiFi.SSID(i);
            strncpy(net.ssid, ssid.c_str(), 32);
            net.ssid[32] = '\0';
            net.rssi = (int8_t)WiFi.RSSI(i);
            net.secured = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
            if (net.ssid[0]) g_wifiNetworks.push_back(net);
        }
        WiFi.scanDelete();
        g_wifiWorkerResult.store(WIFI_WORKER_DONE);
    } else {
        g_wifiWorkerResult.store(WIFI_WORKER_FAILED);
    }
    vTaskDelete(nullptr);
}

static void wifiConnectTask(void* arg) {
    const WifiConnectArgs* a = reinterpret_cast<const WifiConnectArgs*>(arg);
    WiFi.disconnect();
    WiFi.mode(WIFI_STA);
    restoreWifiProtocol();
    WiFi.setAutoReconnect(true);
    WiFi.begin(a->ssid, a->pass);
    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_CONNECT_TIMEOUT_MS) {
        vTaskDelay(pdMS_TO_TICKS(250));
    }
    g_wifiWorkerResult.store(WiFi.status() == WL_CONNECTED ? WIFI_WORKER_DONE : WIFI_WORKER_FAILED);
    vTaskDelete(nullptr);
}

void startWifiScan() {
    g_wifiWorkerResult.store(WIFI_WORKER_RUNNING);
    g_wifiNetworks.clear();
    xTaskCreate(wifiScanTask, "wifi_scan", 4096, nullptr, 5, nullptr);
}

void startWifiConnect(const char* ssid, const char* pass) {
    g_wifiWorkerResult.store(WIFI_WORKER_RUNNING);
    strncpy(g_wifiConnectArgs.ssid, ssid, 32); g_wifiConnectArgs.ssid[32] = '\0';
    strncpy(g_wifiConnectArgs.pass, pass, 64); g_wifiConnectArgs.pass[64] = '\0';
    xTaskCreate(wifiConnectTask, "wifi_conn", 4096, &g_wifiConnectArgs, 5, nullptr);
}
