#include "badge_state.h"

#include <mbedtls/base64.h>
#include <esp_random.h>

// ── Display ────────────────────────────────────────────────────────────────
GxEPD2_BW<GxEPD2_270_GDEY027T91, GxEPD2_270_GDEY027T91::HEIGHT> display(
    GxEPD2_270_GDEY027T91(PIN_EPD_CS, PIN_EPD_DC, PIN_EPD_RST, PIN_EPD_BUSY)
);
GFXcanvas1 g_luaCanvas(ONION_DISPLAY_WIDTH, ONION_DISPLAY_HEIGHT);
GFXcanvas1 g_frame(ONION_DISPLAY_WIDTH, ONION_DISPLAY_HEIGHT);
const int FRAME_BPR   = (ONION_DISPLAY_WIDTH + 7) / 8;
const int FRAME_BYTES = FRAME_BPR * ONION_DISPLAY_HEIGHT;
uint8_t g_prevFrame[FRAME_BPR * ONION_DISPLAY_HEIGHT];
uint16_t g_partialCount    = 0;
bool     g_forceFullRefresh = true;
bool     g_luaDeferFlush    = false;
bool     g_luaFramePending  = false;

// ── Identity / config ──────────────────────────────────────────────────────
RuntimeConfig g_config;
BadgeIdentity g_identity;
LinkPrompt g_linkPrompt;
TransactionPrompt g_txPrompt;
LuaScriptPrompt g_luaPrompt;
CheckInPrompt g_checkinPrompt;
CheckInResult g_checkinResult;
Preferences g_prefs;

// ── MQTT ───────────────────────────────────────────────────────────────────
esp_mqtt_client_handle_t g_mqtt = nullptr;
bool g_mqttConnected = false;

// ── Screen state ───────────────────────────────────────────────────────────
Screen g_screen     = SCREEN_BOOT_SPLASH;
Screen g_lastScreen = SCREEN_BOOT_SPLASH;
bool g_needsRedraw = true;
uint8_t g_lastButtons = 0;
uint32_t g_lastButtonPoll = 0;
uint32_t g_lastHandshake = 0;
uint32_t g_lastProfileRefresh = 0;
uint32_t g_lastMqttAttempt = 0;
uint32_t g_lastWifiAttempt = 0;
uint32_t g_lastCheckInScan = 0;
uint32_t g_lastCheckInPromptAt = 0;
uint32_t g_mqttHandshakeSentAt = 0;
bool g_mqttHandshakePending = false;
String g_log = "Booting";
String g_lastLuaError;
int g_homeSelection = 0;
int g_scriptSelection = 0;
std::vector<String> g_scripts;
String g_pendingDeletePath;
bool g_deleteChoice = false;
bool g_luaDisplayActive = false;
bool g_ateccReady = false;
uint8_t g_ateccSerial[ATECC_SERIAL_LEN] = {};

#if PIN_BATTERY_ADC >= 0
int g_batteryPercent = -1;
int g_batteryVoltageMv = 0;
int g_batteryDisplayedPercent = -1;
uint32_t g_lastBatterySample = 0;
#endif

// ── WiFi setup state ───────────────────────────────────────────────────────
std::vector<WifiNetwork> g_wifiNetworks;
int g_wifiListSel = 0;
String g_wifiConnectSsid;
char g_wifiPassBuf[65] = {};
int g_wifiPassLen = 0;
int g_settingsSel = 0;
int g_wifiOverviewSel = 0;
String g_wifiResultMsg;
std::atomic<int> g_wifiWorkerResult(WIFI_WORKER_IDLE);
WifiConnectArgs g_wifiConnectArgs = {};

const char* kKbNormal[5] = {
    "1234567890",
    "qwertyuiop",
    "asdfghjkl",
    "zxcvbnm",
    "!@#$%&*-_=+.",
};
const char* kKbCaps[5] = {
    "1234567890",
    "QWERTYUIOP",
    "ASDFGHJKL",
    "ZXCVBNM",
    "!@#$%&*-_=+.",
};
const int kKbTotalRows = 6;
int g_kbRow = 0;
int g_kbCol = 0;
bool g_kbCaps = false;

// ── ESP-NOW ────────────────────────────────────────────────────────────────
bool g_espnowStarted = false;
uint32_t g_espnowSent = 0;
uint32_t g_espnowReceived = 0;
portMUX_TYPE g_espnowMux = portMUX_INITIALIZER_UNLOCKED;
EspNowQueuedMessage g_espnowQueue[ONION_ESPNOW_QUEUE_LEN];
uint8_t g_espnowQueueHead = 0;
uint8_t g_espnowQueueCount = 0;

// ── Check-in ───────────────────────────────────────────────────────────────
portMUX_TYPE g_checkinMux = portMUX_INITIALIZER_UNLOCKED;
CheckInPendingOffer g_checkinPendingOffer;
CheckInPendingResult g_checkinPendingResult;
bool g_checkinOfferPending = false;
bool g_checkinResultPending = false;
String g_lastCheckInBeaconId;

// ── Lua MQTT ───────────────────────────────────────────────────────────────
portMUX_TYPE g_luaMqttMux = portMUX_INITIALIZER_UNLOCKED;
char g_luaMqttSubs[ONION_LUA_MQTT_MAX_SUBS][ONION_LUA_MQTT_MAX_TOPIC + 1];
uint8_t g_luaMqttSubCount = 0;
MqttQueuedMessage g_luaMqttQueue[ONION_LUA_MQTT_QUEUE_LEN];
uint8_t g_luaMqttQueueHead = 0;
uint8_t g_luaMqttQueueCount = 0;

// ── MQTT fragment reassembly ───────────────────────────────────────────────
String g_mqttRxTopic;
uint8_t* g_mqttRxPresent = nullptr;
File g_mqttRxFile;
int g_mqttRxExpectedLen = 0;
int g_mqttRxReceivedLen = 0;
size_t g_mqttRxPresentBytes = 0;
int g_mqttRxMsgId = 0;

// ── Module / radio / audio state ───────────────────────────────────────────
ActiveModule g_activeModule = MODULE_NONE;
int g_modulePowerPin = -1;

// SubGHz
double g_subghzFreq = 0.0;

// Sound
int g_soundSampleRate = SOUND_SPK_SAMPLE_RATE;
int g_soundCtrlPin = -1;
uint32_t g_soundMicStartedAt = 0;
uint32_t g_soundMicSamples = 0;
uint32_t g_soundMicBytes = 0;
uint32_t g_soundMicTimeouts = 0;
i2s_chan_handle_t g_i2sTx = nullptr;
i2s_chan_handle_t g_i2sRx = nullptr;

// ── Utility functions ──────────────────────────────────────────────────────

String prefString(const char* key, const char* fallback) {
    String value = g_prefs.getString(key, "");
    return value.length() ? value : String(fallback);
}

void saveConfigValue(const char* key, const String& value) {
    g_prefs.putString(key, value);
}

void setLog(const String& message) {
    bool changed = g_log != message;
    g_log = message;
    Serial.printf("[onion-os] %s\n", message.c_str());
    if (changed && !g_luaDisplayActive) g_needsRedraw = true;
}

String generateHardwareId() {
    char buf[65];
    for (int i = 0; i < 32; i += 4) {
        uint32_t r = esp_random();
        snprintf(buf + (i * 2), 9, "%08lx", (unsigned long)r);
    }
    buf[64] = '\0';
    return String(buf);
}

void loadConfig() {
    g_prefs.begin("onion-os", false);
    g_config.wifiSsid = prefString("wifi_ssid", ONION_HARDCODED_WIFI_SSID);
    g_config.wifiPassword = prefString("wifi_pass", ONION_HARDCODED_WIFI_PASSWORD);
    g_config.serverBaseUrl = ONION_HARDCODED_SERVER_BASE_URL;
    g_config.badgeApiKey = prefString("api_key", ONION_DEFAULT_BADGE_API_KEY);
    g_config.mqttUri = ONION_HARDCODED_MQTT_URI;
    g_config.mqttUsername = ONION_HARDCODED_MQTT_USERNAME;
    g_config.mqttPassword = ONION_HARDCODED_MQTT_PASSWORD;
    g_config.mqttTopicPrefix = prefString("mqtt_prefix", ONION_DEFAULT_MQTT_TOPIC_PREFIX);
    g_config.scriptManifestUrl = prefString("script_url", ONION_DEFAULT_SCRIPT_MANIFEST_URL);
    g_config.moduleVariant = prefString("mod_variant", "L1");
    g_identity.hardwareId = prefString("hw_id", "");
    if (!g_identity.hardwareId.length()) {
        g_identity.hardwareId = generateHardwareId();
        g_prefs.putString("hw_id", g_identity.hardwareId);
    }
    g_identity.onionId = g_prefs.getULong64("onion_id", 0);
    g_identity.status = prefString("status", "new");
    g_identity.username = prefString("username", "");
    g_identity.onionCount = prefString("onions", "0");
    g_identity.solanaPublicKey = prefString("sol_pub", "");
    g_identity.linked = g_prefs.getBool("linked", false);
}

String bytesToHex(const uint8_t* data, size_t len) {
    static const char* hex = "0123456789abcdef";
    String out;
    out.reserve(len * 2);
    for (size_t i = 0; i < len; ++i) {
        out += hex[data[i] >> 4];
        out += hex[data[i] & 0x0F];
    }
    return out;
}

bool prefsGetBytes(const char* key, uint8_t* out, size_t len) {
    if (!g_prefs.isKey(key)) return false;
    return g_prefs.getBytesLength(key) == len && g_prefs.getBytes(key, out, len) == len;
}

String jsonEscape(const String& value) {
    String out;
    out.reserve(value.length() + 8);
    for (size_t i = 0; i < value.length(); ++i) {
        char ch = value[i];
        if (ch == '"' || ch == '\\') {
            out += '\\';
            out += ch;
        } else if (ch == '\n') {
            out += "\\n";
        } else if (ch == '\r') {
            out += "\\r";
        } else if (ch == '\t') {
            out += "\\t";
        } else {
            out += ch;
        }
    }
    return out;
}

bool base64Decode(const String& input, std::vector<uint8_t>& out) {
    size_t olen = 0;
    int rc = mbedtls_base64_decode(nullptr, 0, &olen,
        reinterpret_cast<const unsigned char*>(input.c_str()), input.length());
    if (rc != MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL && rc != 0) return false;
    out.assign(olen, 0);
    rc = mbedtls_base64_decode(out.data(), out.size(), &olen,
        reinterpret_cast<const unsigned char*>(input.c_str()), input.length());
    if (rc != 0) return false;
    out.resize(olen);
    return true;
}

String base64Encode(const uint8_t* data, size_t len) {
    size_t olen = 0;
    mbedtls_base64_encode(nullptr, 0, &olen, data, len);
    std::vector<uint8_t> out(olen + 1, 0);
    if (mbedtls_base64_encode(out.data(), out.size(), &olen, data, len) != 0) return String();
    return String(reinterpret_cast<const char*>(out.data())).substring(0, olen);
}

String base58Encode(const uint8_t* data, size_t len) {
    static const char* alphabet = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";
    size_t zeros = 0;
    while (zeros < len && data[zeros] == 0) zeros++;

    std::vector<uint8_t> b58((len - zeros) * 138 / 100 + 1);
    size_t length = 0;
    for (size_t i = zeros; i < len; ++i) {
        int carry = data[i];
        size_t j = 0;
        for (auto it = b58.rbegin(); (carry != 0 || j < length) && it != b58.rend(); ++it, ++j) {
            carry += 256 * (*it);
            *it = carry % 58;
            carry /= 58;
        }
        length = j;
    }

    String out;
    out.reserve(zeros + length);
    for (size_t i = 0; i < zeros; ++i) out += '1';
    auto it = b58.begin() + (b58.size() - length);
    while (it != b58.end()) out += alphabet[*it++];
    return out;
}

bool readSolanaShortVec(const std::vector<uint8_t>& data, size_t& offset, size_t& value) {
    value = 0;
    int shift = 0;
    for (int i = 0; i < 3; ++i) {
        if (offset >= data.size()) return false;
        uint8_t byte = data[offset++];
        value |= (size_t)(byte & 0x7F) << shift;
        if ((byte & 0x80) == 0) return true;
        shift += 7;
    }
    return false;
}
