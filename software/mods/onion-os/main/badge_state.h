#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>
#include <esp_now.h>
#include <mqtt_client.h>
#include <esp_http_client.h>
#include <SPIFFS.h>
#include <driver/i2s_std.h>
#include <driver/i2s_pdm.h>
#include <atomic>
#include <vector>

#include <GxEPD2_BW.h>
#include <gdey/GxEPD2_270_GDEY027T91.h>
#include <Fonts/FreeMono9pt7b.h>
#include <Fonts/FreeMonoBold9pt7b.h>
#include <Fonts/FreeMonoBold18pt7b.h>

extern "C" {
#include "cryptoauthlib.h"
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}

#include <sodium.h>

#include "badge_types.h"
#include "logo_bitmap.h"

// ── Display ────────────────────────────────────────────────────────────────
extern GxEPD2_BW<GxEPD2_270_GDEY027T91, GxEPD2_270_GDEY027T91::HEIGHT> display;
extern GFXcanvas1 g_luaCanvas;
extern GFXcanvas1 g_frame;
extern const int FRAME_BPR;
extern const int FRAME_BYTES;
extern uint8_t g_prevFrame[];
extern uint16_t g_partialCount;
extern bool g_forceFullRefresh;
extern bool g_luaDeferFlush;
extern bool g_luaFramePending;

// ── Identity / config ──────────────────────────────────────────────────────
extern RuntimeConfig g_config;
extern BadgeIdentity g_identity;
extern LinkPrompt g_linkPrompt;
extern TransactionPrompt g_txPrompt;
extern LuaScriptPrompt g_luaPrompt;
extern CheckInPrompt g_checkinPrompt;
extern CheckInResult g_checkinResult;
extern Preferences g_prefs;

// ── MQTT ───────────────────────────────────────────────────────────────────
extern esp_mqtt_client_handle_t g_mqtt;
extern bool g_mqttConnected;

// ── Screen state ───────────────────────────────────────────────────────────
extern Screen g_screen;
extern Screen g_lastScreen;
extern bool g_needsRedraw;
extern uint8_t g_lastButtons;
extern uint32_t g_lastButtonPoll;
extern uint32_t g_lastHandshake;
extern uint32_t g_lastProfileRefresh;
extern uint32_t g_lastMqttAttempt;
extern uint32_t g_lastWifiAttempt;
extern uint32_t g_lastCheckInScan;
extern uint32_t g_lastCheckInPromptAt;
extern uint32_t g_mqttHandshakeSentAt;
extern bool g_mqttHandshakePending;
extern String g_log;
extern String g_lastLuaError;
extern int g_homeSelection;
extern int g_scriptSelection;
extern std::vector<String> g_scripts;
extern String g_pendingDeletePath;
extern bool g_deleteChoice;
extern bool g_luaDisplayActive;
extern bool g_ateccReady;
extern uint8_t g_ateccSerial[ATECC_SERIAL_LEN];

#if PIN_BATTERY_ADC >= 0
extern int g_batteryPercent;
extern int g_batteryVoltageMv;
extern int g_batteryDisplayedPercent;
extern uint32_t g_lastBatterySample;
#endif

// ── WiFi setup state ───────────────────────────────────────────────────────
extern std::vector<WifiNetwork> g_wifiNetworks;
extern int g_wifiListSel;
extern String g_wifiConnectSsid;
extern char g_wifiPassBuf[65];
extern int g_wifiPassLen;
extern int g_settingsSel;
extern int g_wifiOverviewSel;
extern String g_wifiResultMsg;
extern std::atomic<int> g_wifiWorkerResult;
extern WifiConnectArgs g_wifiConnectArgs;

// Keyboard
extern const char* kKbNormal[5];
extern const char* kKbCaps[5];
extern const int kKbTotalRows;
extern int g_kbRow;
extern int g_kbCol;
extern bool g_kbCaps;

// ── ESP-NOW ────────────────────────────────────────────────────────────────
extern bool g_espnowStarted;
extern uint32_t g_espnowSent;
extern uint32_t g_espnowReceived;
extern portMUX_TYPE g_espnowMux;
extern EspNowQueuedMessage g_espnowQueue[];
extern uint8_t g_espnowQueueHead;
extern uint8_t g_espnowQueueCount;

// ── Check-in ───────────────────────────────────────────────────────────────
extern portMUX_TYPE g_checkinMux;
extern CheckInPendingOffer g_checkinPendingOffer;
extern CheckInPendingResult g_checkinPendingResult;
extern bool g_checkinOfferPending;
extern bool g_checkinResultPending;
extern String g_lastCheckInBeaconId;

// ── Lua MQTT ───────────────────────────────────────────────────────────────
extern portMUX_TYPE g_luaMqttMux;
extern char g_luaMqttSubs[][ONION_LUA_MQTT_MAX_TOPIC + 1];
extern uint8_t g_luaMqttSubCount;
extern MqttQueuedMessage g_luaMqttQueue[];
extern uint8_t g_luaMqttQueueHead;
extern uint8_t g_luaMqttQueueCount;

// ── MQTT fragment reassembly ───────────────────────────────────────────────
extern String g_mqttRxTopic;
extern uint8_t* g_mqttRxPresent;
extern File g_mqttRxFile;
extern int g_mqttRxExpectedLen;
extern int g_mqttRxReceivedLen;
extern size_t g_mqttRxPresentBytes;
extern int g_mqttRxMsgId;

// ── Module / radio / audio state ───────────────────────────────────────────
extern ActiveModule g_activeModule;
extern int g_modulePowerPin;

// SubGHz
extern double g_subghzFreq;

// Sound
extern int g_soundSampleRate;
extern int g_soundCtrlPin;
extern uint32_t g_soundMicStartedAt;
extern uint32_t g_soundMicSamples;
extern uint32_t g_soundMicBytes;
extern uint32_t g_soundMicTimeouts;
extern i2s_chan_handle_t g_i2sTx;
extern i2s_chan_handle_t g_i2sRx;

// ── Utility functions used across modules ──────────────────────────────────
String prefString(const char* key, const char* fallback);
void saveConfigValue(const char* key, const String& value);
void setLog(const String& msg);
String generateHardwareId();
void loadConfig();
String bytesToHex(const uint8_t* data, size_t len);
bool prefsGetBytes(const char* key, uint8_t* out, size_t len);
String jsonEscape(const String& input);
bool base64Decode(const String& input, std::vector<uint8_t>& out);
String base64Encode(const uint8_t* data, size_t len);
String base58Encode(const uint8_t* data, size_t len);
bool readSolanaShortVec(const std::vector<uint8_t>& data, size_t& offset, size_t& value);
