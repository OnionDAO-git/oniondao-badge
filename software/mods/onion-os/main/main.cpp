#include <Arduino.h>
#include <SPI.h>
#include <SPIFFS.h>
#include <Preferences.h>
#include <WiFi.h>
#include <Wire.h>
#include <esp_now.h>
#include <esp_crt_bundle.h>
#include <esp_http_client.h>
#include <esp_random.h>
#include <esp_wifi.h>
#include <mqtt_client.h>
#include <cJSON.h>
#include <mbedtls/base64.h>
#include <sodium.h>

extern "C" {
#include "cryptoauthlib.h"
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}

#include <GxEPD2_BW.h>
#include <gdey/GxEPD2_270_GDEY027T91.h>
#include <Fonts/FreeMono9pt7b.h>
#include <Fonts/FreeMonoBold9pt7b.h>
#include <Fonts/FreeMonoBold18pt7b.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "badge_pins.h"
#include "logo_bitmap.h"

#if __has_include("onion_config.h")
#include "onion_config.h"
#else
#define ONION_DEFAULT_WIFI_SSID ""
#define ONION_DEFAULT_WIFI_PASSWORD ""
#define ONION_DEFAULT_SERVER_BASE_URL "https://oniondao.dev"
#define ONION_DEFAULT_BADGE_API_KEY ""
#define ONION_DEFAULT_MQTT_URI ""
#define ONION_DEFAULT_MQTT_USERNAME "oniondao"
#define ONION_DEFAULT_MQTT_PASSWORD "02eb3d5e04fd2dc9cbb6f3f5c6c9d89d9c96acd987cc24ddf9f717f9480e8786"
#define ONION_DEFAULT_MQTT_TOPIC_PREFIX "oniondao"
#define ONION_DEFAULT_SCRIPT_MANIFEST_URL ""
#endif

#define ONION_HARDCODED_WIFI_SSID "CIC Guest"
#define ONION_HARDCODED_WIFI_PASSWORD "1nnovation"
#define ONION_HARDCODED_SERVER_BASE_URL "https://oniondao.dev"
#define ONION_HARDCODED_MQTT_URI "mqtt://shortline.proxy.rlwy.net:20928"
#define ONION_HARDCODED_MQTT_USERNAME "oniondao"
#define ONION_HARDCODED_MQTT_PASSWORD "02eb3d5e04fd2dc9cbb6f3f5c6c9d89d9c96acd987cc24ddf9f717f9480e8786"

#define TCA9534_ADDR   0x20
#define TCA9534_INPUT  0x00
#define TCA9534_CONFIG 0x03

#define BTN_LEFT   (1 << 0)
#define BTN_DOWN   (1 << 1)
#define BTN_UP     (1 << 2)
#define BTN_RIGHT  (1 << 3)
#define BTN_SELECT (1 << 4)
#define BTN_CANCEL (1 << 5)

#define SERIAL_BAUD 115200
#define WIFI_CONNECT_TIMEOUT_MS 15000
#define HANDSHAKE_INTERVAL_MS 30000
#define MQTT_RECONNECT_INTERVAL_MS 5000
#define MQTT_HANDSHAKE_ACCEPT_WINDOW_MS 10000
#define BOOT_SPLASH_MS 3000
#define MAX_SCRIPT_BYTES (64 * 1024)
#define MAX_IMAGE_BYTES (192 * 1024)
#define ATECC_HMAC_SLOT 10
#define ATECC_I2C_ADDRESS_8BIT 0xC0
#define ATECC_SERIAL_LEN 9
#define SOLANA_PUBKEY_LEN 32
#define SOLANA_SIGNATURE_LEN 64
#define SOLANA_SECRET_KEY_LEN 64
#define SOLANA_SEED_LEN 32
#define SOLANA_KEY_NONCE_LEN crypto_aead_xchacha20poly1305_ietf_NPUBBYTES
#define SOLANA_KEY_MAC_LEN crypto_aead_xchacha20poly1305_ietf_ABYTES
#define LUA_GPIO_POLL_MAX_MS 30000
#define LUA_SLEEP_MAX_MS 60000
#define LUA_ESPNOW_RECV_MAX_MS 30000
#define ONION_ESPNOW_MAX_PAYLOAD 240
#define ONION_ESPNOW_QUEUE_LEN 8
#define ONION_DISPLAY_WIDTH 264
#define ONION_DISPLAY_HEIGHT 176

GxEPD2_BW<GxEPD2_270_GDEY027T91, GxEPD2_270_GDEY027T91::HEIGHT> display(
    GxEPD2_270_GDEY027T91(PIN_EPD_CS, PIN_EPD_DC, PIN_EPD_RST, PIN_EPD_BUSY)
);
static GFXcanvas1 g_luaCanvas(ONION_DISPLAY_WIDTH, ONION_DISPLAY_HEIGHT);

enum Screen : uint8_t {
    SCREEN_BOOT_SPLASH,
    SCREEN_STATUS,
    SCREEN_SCRIPT_EXPLORER,
    SCREEN_LINK_PROMPT,
    SCREEN_TX_PROMPT,
    SCREEN_LUA_PROMPT,
    SCREEN_LOG,
};

enum HomeItem : int {
    HOME_ITEM_SCRIPTS,
    HOME_ITEM_SYNC,
    HOME_ITEM_REFRESH,
    HOME_ITEM_COUNT,
};

struct RuntimeConfig {
    String wifiSsid;
    String wifiPassword;
    String serverBaseUrl;
    String badgeApiKey;
    String aiApiKey;
    String mqttUri;
    String mqttUsername;
    String mqttPassword;
    String mqttTopicPrefix;
    String scriptManifestUrl;
};

struct BadgeIdentity {
    String hardwareId;
    uint64_t onionId = 0;
    String status = "booting";
    String username;
    String onionCount = "0";
    String solanaPublicKey;
    bool linked = false;
};

struct LinkPrompt {
    String requestId;
    String username;
    bool active = false;
};

struct TransactionPrompt {
    String operationId;
    String requestId;
    String type;
    int amount = 0;
    String transactionBase64;
    bool active = false;
};

struct LuaScriptPrompt {
    String requestId;
    String scriptId;
    String title;
    String fileName;
    String description;
    String authorUsername;
    String code;
    int sizeBytes = 0;
    bool active = false;
};

static RuntimeConfig g_config;
static BadgeIdentity g_identity;
static LinkPrompt g_linkPrompt;
static TransactionPrompt g_txPrompt;
static LuaScriptPrompt g_luaPrompt;
static Preferences g_prefs;
static esp_mqtt_client_handle_t g_mqtt = nullptr;
static bool g_mqttConnected = false;
static Screen g_screen = SCREEN_BOOT_SPLASH;
static bool g_needsRedraw = true;
static uint8_t g_lastButtons = 0;
static uint32_t g_lastHandshake = 0;
static uint32_t g_lastMqttAttempt = 0;
static uint32_t g_mqttHandshakeSentAt = 0;
static bool g_mqttHandshakePending = false;
static String g_log = "Booting";
static int g_homeSelection = 0;
static int g_scriptSelection = 0;
static std::vector<String> g_scripts;
static bool g_luaDisplayActive = false;
static bool g_luaBatchMode = false;
static bool g_ateccReady = false;
static uint8_t g_ateccSerial[ATECC_SERIAL_LEN] = {};

static const int kLuaReadableGpios[] = {48, 47, 19, 42, 41, 40, 38, 39, 16, 15, 7, 6, 5, 4};

struct LuaButton {
    const char* name;
    uint8_t mask;
};

struct EspNowQueuedMessage {
    uint8_t mac[6] = {};
    uint8_t len = 0;
    char payload[ONION_ESPNOW_MAX_PAYLOAD + 1] = {};
    int8_t rssi = 0;
    uint32_t receivedAt = 0;
};

static const LuaButton kLuaButtons[] = {
    {"left", BTN_LEFT},
    {"down", BTN_DOWN},
    {"up", BTN_UP},
    {"right", BTN_RIGHT},
    {"select", BTN_SELECT},
    {"cancel", BTN_CANCEL},
};

static const uint8_t kEspNowBroadcastMac[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

static bool g_espnowStarted = false;
static uint32_t g_espnowSent = 0;
static uint32_t g_espnowReceived = 0;
static portMUX_TYPE g_espnowMux = portMUX_INITIALIZER_UNLOCKED;
static EspNowQueuedMessage g_espnowQueue[ONION_ESPNOW_QUEUE_LEN];
static uint8_t g_espnowQueueHead = 0;
static uint8_t g_espnowQueueCount = 0;

static String prefString(const char* key, const char* fallback) {
    String value = g_prefs.getString(key, "");
    return value.length() ? value : String(fallback);
}

static void saveConfigValue(const char* key, const String& value) {
    g_prefs.putString(key, value);
}

static void setLog(const String& message) {
    bool changed = g_log != message;
    g_log = message;
    Serial.printf("[onion-os] %s\n", message.c_str());
    if (changed && !g_luaDisplayActive) g_needsRedraw = true;
}

static String generateHardwareId() {
    char buf[65];
    for (int i = 0; i < 32; i += 4) {
        uint32_t r = esp_random();
        snprintf(buf + (i * 2), 9, "%08lx", (unsigned long)r);
    }
    buf[64] = '\0';
    return String(buf);
}

static void loadConfig() {
    g_prefs.begin("onion-os", false);
    g_config.wifiSsid = ONION_HARDCODED_WIFI_SSID;
    g_config.wifiPassword = ONION_HARDCODED_WIFI_PASSWORD;
    g_config.serverBaseUrl = ONION_HARDCODED_SERVER_BASE_URL;
    g_config.badgeApiKey = prefString("api_key", ONION_DEFAULT_BADGE_API_KEY);
    g_config.aiApiKey = prefString("ai_key", "");
    g_config.mqttUri = ONION_HARDCODED_MQTT_URI;
    g_config.mqttUsername = ONION_HARDCODED_MQTT_USERNAME;
    g_config.mqttPassword = ONION_HARDCODED_MQTT_PASSWORD;
    g_config.mqttTopicPrefix = prefString("mqtt_prefix", ONION_DEFAULT_MQTT_TOPIC_PREFIX);
    g_config.scriptManifestUrl = prefString("script_url", ONION_DEFAULT_SCRIPT_MANIFEST_URL);
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

static String bytesToHex(const uint8_t* data, size_t len) {
    static const char* hex = "0123456789abcdef";
    String out;
    out.reserve(len * 2);
    for (size_t i = 0; i < len; ++i) {
        out += hex[data[i] >> 4];
        out += hex[data[i] & 0x0F];
    }
    return out;
}

static bool prefsGetBytes(const char* key, uint8_t* out, size_t len) {
    if (!g_prefs.isKey(key)) return false;
    return g_prefs.getBytesLength(key) == len && g_prefs.getBytes(key, out, len) == len;
}

static String jsonEscape(const String& value) {
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

static bool base64Decode(const String& input, std::vector<uint8_t>& out) {
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

static String base64Encode(const uint8_t* data, size_t len) {
    size_t olen = 0;
    mbedtls_base64_encode(nullptr, 0, &olen, data, len);
    std::vector<uint8_t> out(olen + 1, 0);
    if (mbedtls_base64_encode(out.data(), out.size(), &olen, data, len) != 0) return String();
    return String(reinterpret_cast<const char*>(out.data())).substring(0, olen);
}

static String base58Encode(const uint8_t* data, size_t len) {
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

static bool readSolanaShortVec(const std::vector<uint8_t>& data, size_t& offset, size_t& value) {
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

static void restartSharedI2cBus() {
    Wire.begin(PIN_SDA, PIN_SCL);
    Wire.setClock(100000);
}

static bool ateccHmac(const std::vector<uint8_t>& message, uint8_t digest[32], String* serialHex, String& error) {
    Wire.end();
    delay(5);
    atcab_release();

    ATCA_IFACECFG_I2C_ADDRESS(&cfg_ateccx08a_i2c_default) = ATECC_I2C_ADDRESS_8BIT;
    cfg_ateccx08a_i2c_default.atcai2c.bus = 0;
    cfg_ateccx08a_i2c_default.atcai2c.baud = 100000;

    ATCA_STATUS status = atcab_init(&cfg_ateccx08a_i2c_default);
    if (status != ATCA_SUCCESS) {
        error = "ATECC init failed " + String((int)status);
        restartSharedI2cBus();
        return false;
    }

    status = atcab_read_serial_number(g_ateccSerial);
    if (status != ATCA_SUCCESS) {
        error = "ATECC serial failed " + String((int)status);
        atcab_release();
        restartSharedI2cBus();
        return false;
    }
    g_ateccReady = true;
    if (serialHex) *serialHex = bytesToHex(g_ateccSerial, sizeof(g_ateccSerial));

    status = atcab_sha_hmac(message.data(), message.size(), ATECC_HMAC_SLOT, digest, SHA_MODE_TARGET_OUT_ONLY);
    atcab_release();
    restartSharedI2cBus();
    if (status != ATCA_SUCCESS) {
        error = "ATECC HMAC failed " + String((int)status);
        return false;
    }
    return true;
}

static std::vector<uint8_t> stringBytes(const String& value) {
    return std::vector<uint8_t>(
        reinterpret_cast<const uint8_t*>(value.c_str()),
        reinterpret_cast<const uint8_t*>(value.c_str()) + value.length()
    );
}

static bool deriveWrappingKey(uint8_t key[32], String& error) {
    String context = "onion-os:solana-seed-wrap:v1:" + g_identity.hardwareId;
    std::vector<uint8_t> bytes = stringBytes(context);
    return ateccHmac(bytes, key, nullptr, error);
}

static bool createAteccAttestation(const String& purpose, const String& subject, String& json, String& error) {
    uint8_t nonce[32];
    uint8_t mac[32];
    esp_fill_random(nonce, sizeof(nonce));

    String context = "onion-os:attestation:v1:" + purpose + ":" + subject + ":" +
        bytesToHex(nonce, sizeof(nonce)) + ":" + g_identity.solanaPublicKey;
    std::vector<uint8_t> bytes = stringBytes(context);
    String serialHex;
    if (!ateccHmac(bytes, mac, &serialHex, error)) return false;

    json = "{\"version\":1,\"slot\":" + String(ATECC_HMAC_SLOT) +
        ",\"serial\":\"" + serialHex +
        "\",\"purpose\":\"" + jsonEscape(purpose) +
        "\",\"subject\":\"" + jsonEscape(subject) +
        "\",\"nonce\":\"" + bytesToHex(nonce, sizeof(nonce)) +
        "\",\"hmac\":\"" + bytesToHex(mac, sizeof(mac)) + "\"}";
    return true;
}

static bool decryptSolanaSeed(uint8_t seed[SOLANA_SEED_LEN], String& error) {
    uint8_t nonce[SOLANA_KEY_NONCE_LEN];
    uint8_t ciphertext[SOLANA_SEED_LEN + SOLANA_KEY_MAC_LEN];
    if (!prefsGetBytes("key_nonce", nonce, sizeof(nonce)) ||
        !prefsGetBytes("key_ct", ciphertext, sizeof(ciphertext))) {
        error = "No wrapped wallet seed";
        return false;
    }

    uint8_t wrapKey[32];
    if (!deriveWrappingKey(wrapKey, error)) return false;

    unsigned long long plainLen = 0;
    int rc = crypto_aead_xchacha20poly1305_ietf_decrypt(
        seed, &plainLen, nullptr,
        ciphertext, sizeof(ciphertext),
        nullptr, 0,
        nonce, wrapKey
    );
    sodium_memzero(wrapKey, sizeof(wrapKey));
    if (rc != 0 || plainLen != SOLANA_SEED_LEN) {
        error = "Wallet unwrap failed";
        return false;
    }
    return true;
}

static bool storeSolanaSeed(const uint8_t seed[SOLANA_SEED_LEN], const uint8_t pubkey[SOLANA_PUBKEY_LEN], String& error) {
    uint8_t nonce[SOLANA_KEY_NONCE_LEN];
    uint8_t ciphertext[SOLANA_SEED_LEN + SOLANA_KEY_MAC_LEN];
    uint8_t wrapKey[32];
    randombytes_buf(nonce, sizeof(nonce));
    if (!deriveWrappingKey(wrapKey, error)) return false;

    unsigned long long cipherLen = 0;
    int rc = crypto_aead_xchacha20poly1305_ietf_encrypt(
        ciphertext, &cipherLen,
        seed, SOLANA_SEED_LEN,
        nullptr, 0, nullptr,
        nonce, wrapKey
    );
    sodium_memzero(wrapKey, sizeof(wrapKey));
    if (rc != 0 || cipherLen != sizeof(ciphertext)) {
        error = "Wallet wrap failed";
        return false;
    }

    g_prefs.putBytes("key_nonce", nonce, sizeof(nonce));
    g_prefs.putBytes("key_ct", ciphertext, sizeof(ciphertext));
    g_identity.solanaPublicKey = base58Encode(pubkey, SOLANA_PUBKEY_LEN);
    g_prefs.putString("sol_pub", g_identity.solanaPublicKey);
    return true;
}

static bool loadOrCreateSolanaKey(bool rotate, String& error) {
    uint8_t seed[SOLANA_SEED_LEN];
    uint8_t pubkey[SOLANA_PUBKEY_LEN];
    uint8_t secret[SOLANA_SECRET_KEY_LEN];
    bool hasWrappedKey = g_prefs.isKey("key_nonce") || g_prefs.isKey("key_ct");

    if (!rotate && hasWrappedKey) {
        if (!decryptSolanaSeed(seed, error)) return false;
        crypto_sign_seed_keypair(pubkey, secret, seed);
        g_identity.solanaPublicKey = base58Encode(pubkey, SOLANA_PUBKEY_LEN);
        g_prefs.putString("sol_pub", g_identity.solanaPublicKey);
        sodium_memzero(seed, sizeof(seed));
        sodium_memzero(secret, sizeof(secret));
        return true;
    }

    randombytes_buf(seed, sizeof(seed));
    crypto_sign_seed_keypair(pubkey, secret, seed);
    bool ok = storeSolanaSeed(seed, pubkey, error);
    sodium_memzero(seed, sizeof(seed));
    sodium_memzero(secret, sizeof(secret));
    return ok;
}

static void clearSolanaKey() {
    g_prefs.remove("key_nonce");
    g_prefs.remove("key_ct");
    g_prefs.remove("sol_pub");
    g_identity.solanaPublicKey = "";
}

static String topic(const String& suffix) {
    String prefix = g_config.mqttTopicPrefix.length() ? g_config.mqttTopicPrefix : "oniondao";
    if (prefix.endsWith("/")) prefix.remove(prefix.length() - 1);
    return prefix + "/" + suffix;
}

static uint64_t handshakeOnionIdFromTopic(const String& incomingTopic) {
    String base = topic("badge/");
    if (!incomingTopic.startsWith(base) || !incomingTopic.endsWith("/handshake/accepted")) return 0;

    int idStart = base.length();
    int idEnd = incomingTopic.indexOf('/', idStart);
    if (idEnd <= idStart) return 0;

    String idText = incomingTopic.substring(idStart, idEnd);
    char* end = nullptr;
    uint64_t onionId = strtoull(idText.c_str(), &end, 10);
    return (end && *end == '\0') ? onionId : 0;
}

static void initPeripherals() {
    pinMode(PIN_PWR, OUTPUT);
    // GPIO18 drives the battery power-hold latch. Keep it released so the
    // physical ON/OFF switch can actually cut VSYS and the display power.
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

    SPIFFS.begin(true);
}

static uint8_t readButtons() {
    Wire.beginTransmission(TCA9534_ADDR);
    Wire.write(TCA9534_INPUT);
    if (Wire.endTransmission(false) != 0) return 0;
    if (Wire.requestFrom(TCA9534_ADDR, 1) != 1) return 0;
    return (~Wire.read()) & 0x3F;
}

static void printLine(const char* text, int y, const GFXfont* font = &FreeMono9pt7b) {
    display.setFont(font);
    display.setTextColor(GxEPD_BLACK);
    display.setCursor(6, y);
    display.print(text);
}

static void printString(const String& text, int y, const GFXfont* font = &FreeMono9pt7b) {
    printLine(text.c_str(), y, font);
}

static String clipped(const String& value, size_t len) {
    if (value.length() <= len) return value;
    return value.substring(0, len - 3) + "...";
}

static String storedScriptDisplayName(const String& path) {
    String name = path;
    if (name.startsWith("/")) name.remove(0, 1);
    if (name.startsWith("scripts_")) name.remove(0, 8);
    return name;
}

static bool validAssetFileName(const String& name, const char* requiredSuffix = nullptr) {
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

static bool validImageFileName(const String& name) {
    if (!validAssetFileName(name)) return false;
    return name.endsWith(".pbm") || name.endsWith(".bmp");
}

static String imagePathForName(const String& name) {
    if (!validImageFileName(name)) return String();
    return "/images_" + name;
}

static void refreshScriptList() {
    g_scripts.clear();
    File root = SPIFFS.open("/");
    if (!root) return;

    File file = root.openNextFile();
    while (file) {
        String name = file.name();
        if (!file.isDirectory() && name.endsWith(".lua") &&
            (name.startsWith("/scripts_") || name.startsWith("scripts_"))) {
            g_scripts.push_back(name.startsWith("/") ? name : "/" + name);
        }
        file = root.openNextFile();
    }

    std::sort(g_scripts.begin(), g_scripts.end(), [](const String& a, const String& b) {
        return strcmp(a.c_str(), b.c_str()) < 0;
    });
    if (g_scriptSelection >= (int)g_scripts.size()) g_scriptSelection = std::max(0, (int)g_scripts.size() - 1);
}

static void drawBootSplash() {
    display.fillScreen(GxEPD_WHITE);
    int x = (display.width() - ONION_LOGO_WIDTH) / 2;
    int y = (display.height() - ONION_LOGO_HEIGHT) / 2;
    display.drawBitmap(x, y, ONION_LOGO_BLACK_BITMAP, ONION_LOGO_WIDTH, ONION_LOGO_HEIGHT, GxEPD_BLACK);
}

static void drawHomeItem(HomeItem item, const String& label, int y) {
    String prefix = g_homeSelection == item ? "> " : "  ";
    printString(prefix + label, y, g_homeSelection == item ? &FreeMonoBold9pt7b : &FreeMono9pt7b);
}

static void drawStatus() {
    display.fillScreen(GxEPD_WHITE);
    printLine("ONION OS", 22, &FreeMonoBold18pt7b);
    String user = g_identity.username.length() ? g_identity.username : (g_identity.linked ? "linked" : "not linked");
    printString("User: " + clipped(user, 21), 48, &FreeMonoBold9pt7b);
    printString("Onions: " + clipped(g_identity.onionCount, 18), 68);
    printString("ID: " + String(g_identity.onionId ? String(g_identity.onionId) : "pending") +
        "  " + String(g_mqttConnected ? "MQTT" : (WiFi.status() == WL_CONNECTED ? "WiFi" : "offline")), 88);
    drawHomeItem(HOME_ITEM_SCRIPTS, "Scripts Explorer", 110);
    drawHomeItem(HOME_ITEM_SYNC, "Sync Scripts", 130);
    drawHomeItem(HOME_ITEM_REFRESH, "Refresh Profile", 150);
    printString(clipped(g_log, 30), 170);
}

static void drawScriptExplorer() {
    display.fillScreen(GxEPD_WHITE);
    printLine("SCRIPTS", 22, &FreeMonoBold18pt7b);
    if (g_scripts.empty()) {
        printString("No scripts installed", 58, &FreeMonoBold9pt7b);
        printString("RIGHT = sync", 96);
        printString("CANCEL = home", 116);
        return;
    }

    int start = 0;
    const int visibleRows = 5;
    if (g_scriptSelection >= visibleRows) start = g_scriptSelection - visibleRows + 1;
    for (int row = 0; row < visibleRows && start + row < (int)g_scripts.size(); ++row) {
        int idx = start + row;
        String prefix = idx == g_scriptSelection ? "> " : "  ";
        printString(prefix + clipped(storedScriptDisplayName(g_scripts[idx]), 24), 52 + row * 20,
            idx == g_scriptSelection ? &FreeMonoBold9pt7b : &FreeMono9pt7b);
    }
    printString("SELECT run LEFT delete", 146);
    printString("RIGHT sync CANCEL home", 166);
}

static void drawLinkPrompt() {
    display.fillScreen(GxEPD_WHITE);
    printLine("LINK BADGE?", 24, &FreeMonoBold18pt7b);
    printString("User:", 54, &FreeMonoBold9pt7b);
    printString(clipped(g_linkPrompt.username, 22), 74);
    printString("SELECT = approve", 112);
    printString("CANCEL = deny", 132);
    printString("Wallet: " + String(g_identity.solanaPublicKey.length() ? "ready" : "create on approve"), 160);
}

static void drawTransactionPrompt() {
    display.fillScreen(GxEPD_WHITE);
    printLine("SIGN ONIONS?", 24, &FreeMonoBold18pt7b);
    printString("Type: " + clipped(g_txPrompt.type, 18), 54);
    printString("Amount: " + String(g_txPrompt.amount), 74);
    printString("SELECT = sign/approve", 112);
    printString("CANCEL = deny", 132);
    printString("Signer: Ed25519 + ATECC", 160);
}

static void drawLuaPrompt() {
    display.fillScreen(GxEPD_WHITE);
    printLine("INSTALL LUA?", 24, &FreeMonoBold18pt7b);
    printString(clipped(g_luaPrompt.title.length() ? g_luaPrompt.title : g_luaPrompt.fileName, 24), 54, &FreeMonoBold9pt7b);
    printString("By: " + clipped(g_luaPrompt.authorUsername, 18), 76);
    printString(clipped(g_luaPrompt.description, 28), 96);
    printString("Size: " + String(g_luaPrompt.sizeBytes) + " bytes", 116);
    printString("SELECT = install/run", 146);
    printString("CANCEL = deny", 166);
}

static void redraw() {
    if (!g_needsRedraw) return;
    display.setFullWindow();
    display.firstPage();
    do {
        if (g_screen == SCREEN_BOOT_SPLASH) drawBootSplash();
        else if (g_screen == SCREEN_LINK_PROMPT) drawLinkPrompt();
        else if (g_screen == SCREEN_SCRIPT_EXPLORER) drawScriptExplorer();
        else if (g_screen == SCREEN_TX_PROMPT) drawTransactionPrompt();
        else if (g_screen == SCREEN_LUA_PROMPT) drawLuaPrompt();
        else drawStatus();
    } while (display.nextPage());
    g_needsRedraw = false;
}

static bool ensureWifi() {
    if (WiFi.status() == WL_CONNECTED) return true;
    if (!g_config.wifiSsid.length()) {
        setLog("Provision WiFi over serial");
        return false;
    }

    WiFi.mode(WIFI_STA);
    WiFi.begin(g_config.wifiSsid.c_str(), g_config.wifiPassword.c_str());
    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_CONNECT_TIMEOUT_MS) {
        delay(250);
    }

    if (WiFi.status() == WL_CONNECTED) {
        setLog("WiFi connected");
        return true;
    }
    setLog("WiFi connect failed");
    return false;
}

static esp_err_t httpCaptureEvent(esp_http_client_event_t* event) {
    if (event->event_id == HTTP_EVENT_ON_DATA && event->user_data && event->data && event->data_len > 0) {
        String* response = static_cast<String*>(event->user_data);
        response->concat(static_cast<const char*>(event->data), event->data_len);
    }
    return ESP_OK;
}

static int httpPostJson(const String& path, const String& body, String* response) {
    if (!ensureWifi()) return -1;
    String url = g_config.serverBaseUrl;
    if (url.endsWith("/")) url.remove(url.length() - 1);
    url += path;

    String responseBuffer;
    esp_http_client_config_t cfg = {};
    cfg.url = url.c_str();
    cfg.timeout_ms = 10000;
    cfg.crt_bundle_attach = esp_crt_bundle_attach;
    cfg.event_handler = httpCaptureEvent;
    cfg.user_data = &responseBuffer;

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) return -1;
    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    if (g_config.badgeApiKey.length()) {
        String auth = "Bearer " + g_config.badgeApiKey;
        esp_http_client_set_header(client, "Authorization", auth.c_str());
    }
    esp_http_client_set_post_field(client, body.c_str(), body.length());
    esp_err_t err = esp_http_client_perform(client);
    int code = err == ESP_OK ? esp_http_client_get_status_code(client) : -1;
    if (response) *response = responseBuffer;
    esp_http_client_cleanup(client);
    return code;
}

static String jsonString(cJSON* obj, const char* key) {
    cJSON* item = cJSON_GetObjectItemCaseSensitive(obj, key);
    return cJSON_IsString(item) && item->valuestring ? String(item->valuestring) : String();
}

static bool jsonValueString(cJSON* obj, const char* key, String& out) {
    cJSON* item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsString(item) && item->valuestring) {
        out = item->valuestring;
        return true;
    }
    if (cJSON_IsNumber(item)) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%.0f", item->valuedouble);
        out = buf;
        return true;
    }
    return false;
}

static int jsonInt(cJSON* obj, const char* key, int fallback = 0) {
    cJSON* item = cJSON_GetObjectItemCaseSensitive(obj, key);
    return cJSON_IsNumber(item) ? item->valueint : fallback;
}

static uint64_t jsonUint64(cJSON* obj, const char* key, uint64_t fallback = 0) {
    cJSON* item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsNumber(item)) return (uint64_t)item->valuedouble;
    if (cJSON_IsString(item) && item->valuestring) {
        char* end = nullptr;
        uint64_t value = strtoull(item->valuestring, &end, 10);
        return (end && *end == '\0') ? value : fallback;
    }
    return fallback;
}

static bool jsonBool(cJSON* obj, const char* key, bool fallback = false) {
    cJSON* item = cJSON_GetObjectItemCaseSensitive(obj, key);
    return cJSON_IsBool(item) ? cJSON_IsTrue(item) : fallback;
}

static void persistBadgeState() {
    g_prefs.putULong64("onion_id", g_identity.onionId);
    g_prefs.putString("status", g_identity.status);
    g_prefs.putString("username", g_identity.username);
    g_prefs.putString("onions", g_identity.onionCount);
    g_prefs.putBool("linked", g_identity.linked);
}

static bool refreshPublicProfile();

static bool updateProfileFromObject(cJSON* obj) {
    if (!cJSON_IsObject(obj)) return false;

    bool changed = false;
    String value;
    const char* usernameKeys[] = {"username", "userName", "linkedUsername", "displayName", "name"};
    for (const char* key : usernameKeys) {
        if (jsonValueString(obj, key, value) && value.length()) {
            if (g_identity.username != value) {
                g_identity.username = value;
                changed = true;
            }
            break;
        }
    }

    const char* onionKeys[] = {
        "onionCount", "onions", "onionPoints", "points", "pointBalance",
        "currentOnionTokens", "currentOnionPoints", "tokenBalance", "onionTokens", "balance"
    };
    for (const char* key : onionKeys) {
        if (jsonValueString(obj, key, value) && value.length()) {
            if (g_identity.onionCount != value) {
                g_identity.onionCount = value;
                changed = true;
            }
            break;
        }
    }

    return changed;
}

static void updateProfileFromJson(cJSON* root) {
    bool changed = updateProfileFromObject(root);
    const char* objectKeys[] = {"profile", "user", "account"};
    for (const char* key : objectKeys) {
        changed = updateProfileFromObject(cJSON_GetObjectItemCaseSensitive(root, key)) || changed;
    }
    if (changed) persistBadgeState();
}

static void subscribeBadgeTopics();

static bool mqttHandshakeResponseMatchesBadge(cJSON* root, const String& incomingTopic, uint64_t onionId) {
    if (!incomingTopic.length()) return true;

    String hardwareId = jsonString(root, "hardwareId");
    if (hardwareId.length()) return hardwareId == g_identity.hardwareId;

    uint64_t topicOnionId = handshakeOnionIdFromTopic(incomingTopic);
    if (g_identity.onionId) {
        return (!topicOnionId || topicOnionId == g_identity.onionId) &&
            (!onionId || onionId == g_identity.onionId);
    }

    bool recentHandshake = g_mqttHandshakePending &&
        millis() - g_mqttHandshakeSentAt <= MQTT_HANDSHAKE_ACCEPT_WINDOW_MS;
    return recentHandshake && topicOnionId && onionId == topicOnionId;
}

static bool handleHandshakeResponse(const String& response, const String& incomingTopic = String()) {
    cJSON* root = cJSON_Parse(response.c_str());
    if (!root) return false;
    uint64_t onionId = jsonUint64(root, "onionId", 0);
    String status = jsonString(root, "status");
    if (!onionId) {
        cJSON_Delete(root);
        return false;
    }
    if (!mqttHandshakeResponseMatchesBadge(root, incomingTopic, onionId)) {
        cJSON_Delete(root);
        return false;
    }

    g_identity.onionId = onionId;
    g_mqttHandshakePending = false;
    if (status.length()) g_identity.status = status;
    g_identity.linked = g_identity.status == "linked";
    updateProfileFromJson(root);
    cJSON_Delete(root);
    persistBadgeState();
    subscribeBadgeTopics();
    setLog("Handshake accepted");
    return true;
}

static void doHttpHandshake() {
    String body = "{\"hardwareId\":\"" + g_identity.hardwareId + "\",\"firmware\":\"onion-os\",\"transport\":\"http\"}";
    String response;
    int code = httpPostJson("/api/badge/handshake", body, &response);
    if (code >= 200 && code < 300 && handleHandshakeResponse(response)) return;
    setLog("HTTP handshake failed " + String(code));
}

static bool publishMqtt(const String& mqttTopic, const String& payload) {
    if (!g_mqtt || !g_mqttConnected) return false;
    return esp_mqtt_client_publish(g_mqtt, mqttTopic.c_str(), payload.c_str(), 0, 1, 0) >= 0;
}

static void doMqttHandshake() {
    String replyTo = topic(String("badge/hardware/") + g_identity.hardwareId + "/handshake/accepted");
    String body = "{\"hardwareId\":\"" + jsonEscape(g_identity.hardwareId) +
        "\",\"firmware\":\"onion-os\",\"transport\":\"mqtt\",\"replyTo\":\"" + jsonEscape(replyTo) + "\"}";
    if (publishMqtt(topic("badge/handshake"), body)) {
        g_mqttHandshakePending = true;
        g_mqttHandshakeSentAt = millis();
    }
}

static void subscribeBadgeTopics() {
    if (!g_mqtt || !g_mqttConnected) return;
    esp_mqtt_client_subscribe(g_mqtt, topic("badge/+/handshake/accepted").c_str(), 1);
    esp_mqtt_client_subscribe(g_mqtt, topic(String("badge/hardware/") + g_identity.hardwareId + "/handshake/accepted").c_str(), 1);

    if (!g_identity.onionId) return;
    String base = "badge/" + String((unsigned long long)g_identity.onionId) + "/";
    esp_mqtt_client_subscribe(g_mqtt, topic(base + "handshake/accepted").c_str(), 1);
    esp_mqtt_client_subscribe(g_mqtt, topic(base + "link/request").c_str(), 1);
    esp_mqtt_client_subscribe(g_mqtt, topic(base + "transaction/request").c_str(), 1);
    esp_mqtt_client_subscribe(g_mqtt, topic(base + "lua/request").c_str(), 1);
}

static void handleLinkRequest(cJSON* root) {
    g_luaDisplayActive = false;
    g_linkPrompt.requestId = jsonString(root, "requestId");
    g_linkPrompt.username = jsonString(root, "username");
    g_linkPrompt.active = true;
    g_screen = SCREEN_LINK_PROMPT;
    setLog("Link request received");
}

static void handleTransactionRequest(cJSON* root) {
    g_luaDisplayActive = false;
    g_txPrompt.operationId = jsonString(root, "operationId");
    g_txPrompt.requestId = jsonString(root, "requestId");
    g_txPrompt.type = jsonString(root, "type");
    g_txPrompt.amount = jsonInt(root, "amount", 0);
    g_txPrompt.transactionBase64 = jsonString(root, "transaction");
    g_txPrompt.active = true;
    g_screen = SCREEN_TX_PROMPT;
    setLog("Transaction request received");
}

static void handleLuaRequest(cJSON* root) {
    g_luaDisplayActive = false;
    g_luaPrompt.requestId = jsonString(root, "requestId");
    g_luaPrompt.scriptId = jsonString(root, "scriptId");
    g_luaPrompt.title = jsonString(root, "title");
    g_luaPrompt.fileName = jsonString(root, "fileName");
    g_luaPrompt.description = jsonString(root, "description");
    g_luaPrompt.authorUsername = jsonString(root, "authorUsername");
    g_luaPrompt.code = jsonString(root, "code");
    g_luaPrompt.sizeBytes = jsonInt(root, "sizeBytes", g_luaPrompt.code.length());
    g_luaPrompt.active = true;
    g_screen = SCREEN_LUA_PROMPT;
    setLog("Lua push received");
}

static void handleMqttPayload(const String& incomingTopic, const String& payload) {
    cJSON* root = cJSON_Parse(payload.c_str());
    if (!root) {
        setLog("Bad MQTT JSON");
        return;
    }

    if (incomingTopic.endsWith("/handshake/accepted")) {
        if (!handleHandshakeResponse(payload, incomingTopic)) {
            Serial.printf("[onion-os] ignored MQTT handshake on %s\n", incomingTopic.c_str());
        }
    } else if (incomingTopic.endsWith("/link/request")) {
        handleLinkRequest(root);
    } else if (incomingTopic.endsWith("/transaction/request")) {
        handleTransactionRequest(root);
    } else if (incomingTopic.endsWith("/lua/request")) {
        handleLuaRequest(root);
    }

    cJSON_Delete(root);
}

static void mqttEventHandler(void*, esp_event_base_t, int32_t eventId, void* eventData) {
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)eventData;
    switch ((esp_mqtt_event_id_t)eventId) {
    case MQTT_EVENT_CONNECTED:
        g_mqttConnected = true;
        setLog("MQTT connected");
        subscribeBadgeTopics();
        doMqttHandshake();
        break;
    case MQTT_EVENT_DISCONNECTED:
        g_mqttConnected = false;
        setLog("MQTT disconnected");
        break;
    case MQTT_EVENT_DATA: {
        String incomingTopic(event->topic, event->topic_len);
        String payload(event->data, event->data_len);
        handleMqttPayload(incomingTopic, payload);
        break;
    }
    default:
        break;
    }
}

static void ensureMqtt() {
    if (g_mqttConnected || !g_config.mqttUri.length() || WiFi.status() != WL_CONNECTED) return;
    if (millis() - g_lastMqttAttempt < MQTT_RECONNECT_INTERVAL_MS) return;
    g_lastMqttAttempt = millis();

    if (!g_mqtt) {
        esp_mqtt_client_config_t cfg = {};
        cfg.broker.address.uri = g_config.mqttUri.c_str();
        cfg.credentials.username = g_config.mqttUsername.length() ? g_config.mqttUsername.c_str() : nullptr;
        cfg.credentials.authentication.password = g_config.mqttPassword.length() ? g_config.mqttPassword.c_str() : nullptr;
        g_mqtt = esp_mqtt_client_init(&cfg);
        esp_mqtt_client_register_event(g_mqtt, MQTT_EVENT_ANY, mqttEventHandler, nullptr);
        esp_mqtt_client_start(g_mqtt);
    } else {
        esp_mqtt_client_reconnect(g_mqtt);
    }
}

static bool sendLinkResponse(bool approved) {
    if (!g_identity.onionId) {
        setLog("No Onion ID yet");
        return false;
    }

    String attestation;
    if (approved) {
        String keyError;
        if (!loadOrCreateSolanaKey(false, keyError)) {
            setLog(keyError);
            return false;
        }
        String subject = String((unsigned long long)g_identity.onionId) + ":" + g_linkPrompt.requestId + ":" + g_linkPrompt.username;
        if (!createAteccAttestation("link", subject, attestation, keyError)) {
            setLog(keyError);
            return false;
        }
    }

    String body = "{\"onionId\":" + String((unsigned long long)g_identity.onionId) +
        ",\"approved\":" + String(approved ? "true" : "false") +
        ",\"solanaPublicKey\":\"" + jsonEscape(g_identity.solanaPublicKey) + "\"";
    if (g_linkPrompt.requestId.length()) {
        body += ",\"requestId\":\"" + jsonEscape(g_linkPrompt.requestId) + "\"";
    }
    if (attestation.length()) body += ",\"attestation\":" + attestation;
    body += "}";

    bool sentOverMqtt = publishMqtt(topic("badge/" + String((unsigned long long)g_identity.onionId) + "/link/response"), body);
    String response;
    int code = sentOverMqtt ? 200 : httpPostJson("/api/badge/link-response", body, &response);
    if (code >= 200 && code < 300) {
        g_identity.linked = approved;
        g_identity.status = approved ? "linked" : "seen";
        if (approved && g_linkPrompt.username.length()) g_identity.username = g_linkPrompt.username;
        persistBadgeState();
        setLog(approved ? "Link approved" : "Link denied");
        if (approved) refreshPublicProfile();
    } else {
        setLog("Link response HTTP " + String(code));
    }

    g_linkPrompt.active = false;
    g_screen = SCREEN_STATUS;
    return code >= 200 && code < 300;
}

static bool signSolanaTransaction(const String& transactionBase64, String& signedTransaction, String& error) {
    if (!loadOrCreateSolanaKey(false, error)) return false;

    uint8_t seed[SOLANA_SEED_LEN];
    uint8_t pubkey[SOLANA_PUBKEY_LEN];
    uint8_t secret[SOLANA_SECRET_KEY_LEN];
    if (!decryptSolanaSeed(seed, error)) return false;
    crypto_sign_seed_keypair(pubkey, secret, seed);
    sodium_memzero(seed, sizeof(seed));

    std::vector<uint8_t> tx;
    if (!base64Decode(transactionBase64, tx)) {
        sodium_memzero(secret, sizeof(secret));
        error = "Bad transaction base64";
        return false;
    }

    size_t offset = 0;
    size_t sigCount = 0;
    if (!readSolanaShortVec(tx, offset, sigCount) || sigCount == 0) {
        sodium_memzero(secret, sizeof(secret));
        error = "Bad Solana signatures";
        return false;
    }
    size_t sigStart = offset;
    size_t messageStart = sigStart + sigCount * SOLANA_SIGNATURE_LEN;
    if (messageStart + 3 > tx.size()) {
        sodium_memzero(secret, sizeof(secret));
        error = "Solana tx too short";
        return false;
    }

    uint8_t requiredSigners = tx[messageStart];
    if (requiredSigners == 0 || requiredSigners > sigCount) {
        sodium_memzero(secret, sizeof(secret));
        error = "Badge is not required signer";
        return false;
    }

    size_t messageOffset = messageStart + 3;
    size_t accountCount = 0;
    if (!readSolanaShortVec(tx, messageOffset, accountCount)) {
        sodium_memzero(secret, sizeof(secret));
        error = "Bad account vector";
        return false;
    }
    if (messageOffset + accountCount * SOLANA_PUBKEY_LEN > tx.size() || accountCount < requiredSigners) {
        sodium_memzero(secret, sizeof(secret));
        error = "Bad account keys";
        return false;
    }

    int signerIndex = -1;
    for (size_t i = 0; i < requiredSigners; ++i) {
        const uint8_t* accountKey = tx.data() + messageOffset + i * SOLANA_PUBKEY_LEN;
        if (memcmp(accountKey, pubkey, SOLANA_PUBKEY_LEN) == 0) {
            signerIndex = (int)i;
            break;
        }
    }
    if (signerIndex < 0) {
        sodium_memzero(secret, sizeof(secret));
        error = "Wrong signer wallet";
        return false;
    }

    uint8_t* signatureOut = tx.data() + sigStart + signerIndex * SOLANA_SIGNATURE_LEN;
    if (crypto_sign_detached(signatureOut, nullptr, tx.data() + messageStart, tx.size() - messageStart, secret) != 0) {
        sodium_memzero(secret, sizeof(secret));
        error = "Ed25519 signing failed";
        return false;
    }
    sodium_memzero(secret, sizeof(secret));

    signedTransaction = base64Encode(tx.data(), tx.size());
    if (!signedTransaction.length()) {
        error = "Signed tx encode failed";
        return false;
    }
    return true;
}

static bool sendTransactionResponse(bool approved) {
    if (!g_identity.onionId || !g_txPrompt.operationId.length()) {
        setLog("No transaction active");
        return false;
    }

    String signedTx;
    String signError;
    String attestation;
    if (approved && !signSolanaTransaction(g_txPrompt.transactionBase64, signedTx, signError)) {
        setLog(signError);
        return false;
    }
    if (approved) {
        String subject = g_txPrompt.operationId + ":" + g_txPrompt.requestId + ":" + g_txPrompt.type + ":" + String(g_txPrompt.amount);
        if (!createAteccAttestation("transaction", subject, attestation, signError)) {
            setLog(signError);
            return false;
        }
    }

    String body = "{\"onionId\":" + String((unsigned long long)g_identity.onionId) +
        ",\"operationId\":\"" + jsonEscape(g_txPrompt.operationId) +
        "\",\"approved\":" + String(approved ? "true" : "false") +
        ",\"signedTransaction\":\"" + jsonEscape(signedTx) + "\"";
    if (g_txPrompt.requestId.length()) {
        body += ",\"requestId\":\"" + jsonEscape(g_txPrompt.requestId) + "\"";
    }
    if (attestation.length()) body += ",\"attestation\":" + attestation;
    body += "}";

    bool sentOverMqtt = publishMqtt(topic("badge/" + String((unsigned long long)g_identity.onionId) + "/transaction/response"), body);
    String response;
    int code = sentOverMqtt ? 200 : httpPostJson("/api/badge/transaction-response", body, &response);
    if (code >= 200 && code < 300) {
        setLog(approved ? "Transaction approved" : "Transaction denied");
        g_txPrompt.active = false;
        g_screen = SCREEN_STATUS;
    } else {
        setLog("Txn response HTTP " + String(code));
    }
    return code >= 200 && code < 300;
}

static int httpGetString(const String& url, String* response) {
    if (!ensureWifi()) return false;
    String responseBuffer;
    esp_http_client_config_t cfg = {};
    cfg.url = url.c_str();
    cfg.timeout_ms = 10000;
    cfg.crt_bundle_attach = esp_crt_bundle_attach;
    cfg.event_handler = httpCaptureEvent;
    cfg.user_data = &responseBuffer;

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) return -1;
    if (g_config.badgeApiKey.length()) {
        String auth = "Bearer " + g_config.badgeApiKey;
        esp_http_client_set_header(client, "Authorization", auth.c_str());
    }
    esp_err_t err = esp_http_client_perform(client);
    int code = err == ESP_OK ? esp_http_client_get_status_code(client) : -1;
    if (response) *response = responseBuffer;
    esp_http_client_cleanup(client);
    return code;
}

static String urlEncode(const String& value) {
    static const char* hex = "0123456789ABCDEF";
    String out;
    out.reserve(value.length() + 8);
    for (size_t i = 0; i < value.length(); ++i) {
        uint8_t ch = (uint8_t)value[i];
        bool safe = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
            (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' || ch == '.' || ch == '~';
        if (safe) {
            out += (char)ch;
        } else {
            out += '%';
            out += hex[ch >> 4];
            out += hex[ch & 0x0F];
        }
    }
    return out;
}

static bool refreshPublicProfile() {
    if (!g_identity.onionId && !g_identity.username.length()) {
        setLog("No profile identity");
        return false;
    }

    String base = g_config.serverBaseUrl;
    if (base.endsWith("/")) base.remove(base.length() - 1);
    String response;
    int code = g_identity.onionId
        ? httpGetString(base + "/api/badge/profile/" + String((unsigned long long)g_identity.onionId), &response)
        : -1;
    if ((code < 200 || code >= 300) && g_identity.username.length()) {
        code = httpGetString(base + "/api/public/profile/" + urlEncode(g_identity.username), &response);
    }
    if (code < 200 || code >= 300) {
        setLog("Profile GET failed " + String(code));
        return false;
    }

    cJSON* root = cJSON_Parse(response.c_str());
    if (!root) {
        setLog("Bad profile JSON");
        return false;
    }
    updateProfileFromJson(root);
    String wallet = jsonString(root, "solanaWalletAddress");
    if (wallet.length() && wallet != g_identity.solanaPublicKey) {
        g_identity.solanaPublicKey = wallet;
        g_prefs.putString("sol_pub", g_identity.solanaPublicKey);
    }
    cJSON_Delete(root);
    setLog("Profile refreshed");
    return true;
}

static bool downloadFile(const String& url, const String& path, size_t maxBytes, const String& label) {
    if (!ensureWifi()) return false;
    esp_http_client_config_t cfg = {};
    cfg.url = url.c_str();
    cfg.timeout_ms = 10000;
    cfg.crt_bundle_attach = esp_crt_bundle_attach;

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) return false;
    if (g_config.badgeApiKey.length()) {
        String auth = "Bearer " + g_config.badgeApiKey;
        esp_http_client_set_header(client, "Authorization", auth.c_str());
    }
    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        esp_http_client_cleanup(client);
        setLog(label + " open failed");
        return false;
    }
    int contentLength = esp_http_client_fetch_headers(client);
    int code = esp_http_client_get_status_code(client);
    if (code < 200 || code >= 300) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        setLog(label + " GET failed " + String(code));
        return false;
    }
    if (contentLength > 0 && (size_t)contentLength > maxBytes) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        setLog(label + " too large");
        return false;
    }

    File file = SPIFFS.open(path, FILE_WRITE);
    if (!file) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        setLog(label + " open failed");
        return false;
    }

    uint8_t buf[256];
    size_t written = 0;
    while (true) {
        int read = esp_http_client_read(client, reinterpret_cast<char*>(buf), sizeof(buf));
        if (read < 0) {
            file.close();
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            SPIFFS.remove(path);
            setLog(label + " read failed");
            return false;
        }
        if (read == 0) break;
        file.write(buf, (size_t)read);
        written += read;
        if (written > maxBytes) {
            file.close();
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            SPIFFS.remove(path);
            setLog(label + " too large");
            return false;
        }
    }

    file.close();
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return true;
}

static bool downloadScriptFile(const String& url, const String& path) {
    return downloadFile(url, path, MAX_SCRIPT_BYTES, "Script");
}

static bool downloadImageFile(const String& url, const String& path) {
    return downloadFile(url, path, MAX_IMAGE_BYTES, "Image");
}

struct LoadedBitmap {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> bits;
};

static void setBitmapPixel(LoadedBitmap& bitmap, int x, int y, bool black) {
    if (!black || x < 0 || y < 0 || x >= bitmap.width || y >= bitmap.height) return;
    size_t rowBytes = (bitmap.width + 7) / 8;
    bitmap.bits[y * rowBytes + x / 8] |= 0x80 >> (x & 7);
}

static uint16_t readLe16(const std::vector<uint8_t>& data, size_t offset) {
    if (offset + 2 > data.size()) return 0;
    return (uint16_t)data[offset] | ((uint16_t)data[offset + 1] << 8);
}

static uint32_t readLe32(const std::vector<uint8_t>& data, size_t offset) {
    if (offset + 4 > data.size()) return 0;
    return (uint32_t)data[offset] | ((uint32_t)data[offset + 1] << 8) |
        ((uint32_t)data[offset + 2] << 16) | ((uint32_t)data[offset + 3] << 24);
}

static int32_t readLeS32(const std::vector<uint8_t>& data, size_t offset) {
    return (int32_t)readLe32(data, offset);
}

static bool isBlackRgb(uint8_t r, uint8_t g, uint8_t b) {
    return ((uint16_t)r * 30 + (uint16_t)g * 59 + (uint16_t)b * 11) < 12800;
}

static bool loadFileBytes(const String& path, std::vector<uint8_t>& bytes, size_t maxBytes, String& error) {
    File file = SPIFFS.open(path, FILE_READ);
    if (!file) {
        error = "Image missing";
        return false;
    }
    size_t size = file.size();
    if (!size || size > maxBytes) {
        file.close();
        error = "Image too large";
        return false;
    }
    bytes.assign(size, 0);
    size_t read = file.read(bytes.data(), bytes.size());
    file.close();
    if (read != size) {
        error = "Image read failed";
        return false;
    }
    return true;
}

static bool readPbmToken(const std::vector<uint8_t>& data, size_t& offset, String& token) {
    token = "";
    while (offset < data.size()) {
        char ch = (char)data[offset];
        if (ch == '#') {
            while (offset < data.size() && data[offset] != '\n') offset++;
        } else if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') {
            offset++;
        } else {
            break;
        }
    }
    while (offset < data.size()) {
        char ch = (char)data[offset];
        if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n' || ch == '#') break;
        token += ch;
        offset++;
    }
    return token.length() > 0;
}

static bool loadPbmBitmap(const std::vector<uint8_t>& data, LoadedBitmap& bitmap, String& error) {
    size_t offset = 0;
    String magic;
    String widthToken;
    String heightToken;
    if (!readPbmToken(data, offset, magic) || !readPbmToken(data, offset, widthToken) ||
        !readPbmToken(data, offset, heightToken)) {
        error = "Bad PBM header";
        return false;
    }

    bitmap.width = widthToken.toInt();
    bitmap.height = heightToken.toInt();
    if (bitmap.width <= 0 || bitmap.height <= 0 ||
        bitmap.width > display.width() || bitmap.height > display.height()) {
        error = "PBM size unsupported";
        return false;
    }

    size_t rowBytes = (bitmap.width + 7) / 8;
    bitmap.bits.assign(rowBytes * bitmap.height, 0);

    if (magic == "P4") {
        if (offset < data.size() &&
            (data[offset] == ' ' || data[offset] == '\t' || data[offset] == '\r' || data[offset] == '\n')) offset++;
        size_t needed = rowBytes * bitmap.height;
        if (offset + needed > data.size()) {
            error = "PBM data short";
            return false;
        }
        memcpy(bitmap.bits.data(), data.data() + offset, needed);
        return true;
    }

    if (magic == "P1") {
        String pixel;
        for (int y = 0; y < bitmap.height; ++y) {
            for (int x = 0; x < bitmap.width; ++x) {
                if (!readPbmToken(data, offset, pixel)) {
                    error = "PBM data short";
                    return false;
                }
                setBitmapPixel(bitmap, x, y, pixel == "1");
            }
        }
        return true;
    }

    error = "Unsupported PBM";
    return false;
}

static bool loadBmpBitmap(const std::vector<uint8_t>& data, LoadedBitmap& bitmap, String& error) {
    if (data.size() < 54 || data[0] != 'B' || data[1] != 'M') {
        error = "Bad BMP header";
        return false;
    }

    uint32_t pixelOffset = readLe32(data, 10);
    uint32_t dibSize = readLe32(data, 14);
    int32_t width = readLeS32(data, 18);
    int32_t rawHeight = readLeS32(data, 22);
    uint16_t planes = readLe16(data, 26);
    uint16_t bpp = readLe16(data, 28);
    uint32_t compression = readLe32(data, 30);
    uint32_t colorsUsed = readLe32(data, 46);
    if (dibSize < 40 || width <= 0 || rawHeight == 0 || planes != 1 || compression != 0 ||
        (bpp != 1 && bpp != 4 && bpp != 8 && bpp != 24 && bpp != 32)) {
        error = "Unsupported BMP";
        return false;
    }

    int height = rawHeight < 0 ? -rawHeight : rawHeight;
    bool topDown = rawHeight < 0;
    if (width > display.width() || height > display.height()) {
        error = "BMP size unsupported";
        return false;
    }

    bitmap.width = width;
    bitmap.height = height;
    size_t outRowBytes = (bitmap.width + 7) / 8;
    bitmap.bits.assign(outRowBytes * bitmap.height, 0);

    uint32_t paletteEntries = bpp <= 8 ? (colorsUsed ? colorsUsed : (1UL << bpp)) : 0;
    size_t paletteOffset = 14 + dibSize;
    if (paletteEntries && paletteOffset + paletteEntries * 4 > data.size()) {
        error = "BMP palette bad";
        return false;
    }

    uint32_t rowBytes = ((uint32_t)width * bpp + 31) / 32 * 4;
    if (pixelOffset + rowBytes * height > data.size()) {
        error = "BMP data short";
        return false;
    }

    for (int y = 0; y < height; ++y) {
        int srcY = topDown ? y : (height - 1 - y);
        size_t rowOffset = pixelOffset + rowBytes * srcY;
        for (int x = 0; x < width; ++x) {
            uint8_t r = 255;
            uint8_t g = 255;
            uint8_t b = 255;
            if (bpp == 24 || bpp == 32) {
                size_t pixel = rowOffset + x * (bpp / 8);
                b = data[pixel];
                g = data[pixel + 1];
                r = data[pixel + 2];
            } else {
                uint8_t index = 0;
                if (bpp == 8) {
                    index = data[rowOffset + x];
                } else if (bpp == 4) {
                    uint8_t packed = data[rowOffset + x / 2];
                    index = (x & 1) ? (packed & 0x0F) : (packed >> 4);
                } else {
                    uint8_t packed = data[rowOffset + x / 8];
                    index = (packed >> (7 - (x & 7))) & 0x01;
                }
                if (index >= paletteEntries) {
                    error = "BMP palette index";
                    return false;
                }
                size_t color = paletteOffset + index * 4;
                b = data[color];
                g = data[color + 1];
                r = data[color + 2];
            }
            setBitmapPixel(bitmap, x, y, isBlackRgb(r, g, b));
        }
    }
    return true;
}

static bool loadStoredBitmap(const String& name, LoadedBitmap& bitmap, String& error) {
    String path = imagePathForName(name);
    if (!path.length()) {
        error = "Bad image name";
        return false;
    }

    std::vector<uint8_t> bytes;
    if (!loadFileBytes(path, bytes, MAX_IMAGE_BYTES, error)) return false;
    if (name.endsWith(".pbm")) return loadPbmBitmap(bytes, bitmap, error);
    if (name.endsWith(".bmp")) return loadBmpBitmap(bytes, bitmap, error);
    error = "Unsupported image";
    return false;
}

static uint16_t canvasColor(uint16_t displayColor) {
    return displayColor == GxEPD_BLACK ? 1 : 0;
}

static void refreshLuaCanvas() {
    if (g_luaBatchMode) return;
    display.setFullWindow();
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);
        display.drawBitmap(0, 0, g_luaCanvas.getBuffer(), g_luaCanvas.width(), g_luaCanvas.height(), GxEPD_BLACK);
    } while (display.nextPage());
    g_luaDisplayActive = true;
    g_needsRedraw = false;
}

static int luaOnionDisplayBeginBatch(lua_State*) {
    g_luaBatchMode = true;
    return 0;
}

static int luaOnionDisplayEndBatch(lua_State*) {
    g_luaBatchMode = false;
    refreshLuaCanvas();
    return 0;
}

static void renderBitmap(const LoadedBitmap& bitmap, int x, int y, bool clearScreen) {
    if (clearScreen) g_luaCanvas.fillScreen(0);
    g_luaCanvas.drawBitmap(x, y, bitmap.bits.data(), bitmap.width, bitmap.height, 1);
    refreshLuaCanvas();
}

static bool luaTableBool(lua_State* L, int index, const char* key, bool fallback) {
    if (!lua_istable(L, index)) return fallback;
    lua_getfield(L, index, key);
    bool value = lua_isnil(L, -1) ? fallback : lua_toboolean(L, -1);
    lua_pop(L, 1);
    return value;
}

static int luaTableInt(lua_State* L, int index, const char* key, int fallback) {
    if (!lua_istable(L, index)) return fallback;
    lua_getfield(L, index, key);
    int value = lua_isnumber(L, -1) ? (int)lua_tointeger(L, -1) : fallback;
    lua_pop(L, 1);
    return value;
}

static const char* luaTableString(lua_State* L, int index, const char* key, const char* fallback) {
    if (!lua_istable(L, index)) return fallback;
    lua_getfield(L, index, key);
    const char* value = lua_isstring(L, -1) ? lua_tostring(L, -1) : fallback;
    lua_pop(L, 1);
    return value;
}

static uint16_t displayColorFromName(const char* value, uint16_t fallback) {
    if (!value) return fallback;
    if (strcmp(value, "white") == 0 || strcmp(value, "bg") == 0 || strcmp(value, "background") == 0) {
        return GxEPD_WHITE;
    }
    return GxEPD_BLACK;
}

static const GFXfont* displayFontFromName(const char* value) {
    if (!value || strcmp(value, "small") == 0 || strcmp(value, "regular") == 0) return &FreeMono9pt7b;
    if (strcmp(value, "bold") == 0) return &FreeMonoBold9pt7b;
    if (strcmp(value, "large") == 0 || strcmp(value, "title") == 0) return &FreeMonoBold18pt7b;
    return &FreeMono9pt7b;
}

static bool luaDisplayClearArg(lua_State* L, int index, bool fallback = true) {
    if (lua_gettop(L) < index) return fallback;
    if (lua_istable(L, index)) return luaTableBool(L, index, "clear", fallback);
    return lua_toboolean(L, index);
}

static String macToString(const uint8_t mac[6]) {
    char buf[18];
    snprintf(buf, sizeof(buf), "%02x:%02x:%02x:%02x:%02x:%02x",
        mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return String(buf);
}

static bool parseMacString(const char* value, uint8_t mac[6]) {
    if (!value) return false;
    unsigned int parts[6];
    if (sscanf(value, "%x:%x:%x:%x:%x:%x",
        &parts[0], &parts[1], &parts[2], &parts[3], &parts[4], &parts[5]) != 6) {
        return false;
    }
    for (int i = 0; i < 6; ++i) {
        if (parts[i] > 0xff) return false;
        mac[i] = (uint8_t)parts[i];
    }
    return true;
}

static bool espnowQueuePop(EspNowQueuedMessage& out) {
    bool hasMessage = false;
    portENTER_CRITICAL(&g_espnowMux);
    if (g_espnowQueueCount > 0) {
        out = g_espnowQueue[g_espnowQueueHead];
        g_espnowQueueHead = (g_espnowQueueHead + 1) % ONION_ESPNOW_QUEUE_LEN;
        g_espnowQueueCount--;
        hasMessage = true;
    }
    portEXIT_CRITICAL(&g_espnowMux);
    return hasMessage;
}

static void espnowQueuePush(const uint8_t mac[6], const uint8_t* data, int len, int8_t rssi) {
    if (!mac || !data || len <= 0) return;
    if (len > ONION_ESPNOW_MAX_PAYLOAD) len = ONION_ESPNOW_MAX_PAYLOAD;

    portENTER_CRITICAL(&g_espnowMux);
    uint8_t slot = (g_espnowQueueHead + g_espnowQueueCount) % ONION_ESPNOW_QUEUE_LEN;
    if (g_espnowQueueCount == ONION_ESPNOW_QUEUE_LEN) {
        slot = g_espnowQueueHead;
        g_espnowQueueHead = (g_espnowQueueHead + 1) % ONION_ESPNOW_QUEUE_LEN;
    } else {
        g_espnowQueueCount++;
    }

    EspNowQueuedMessage& msg = g_espnowQueue[slot];
    memcpy(msg.mac, mac, sizeof(msg.mac));
    msg.len = (uint8_t)len;
    memcpy(msg.payload, data, len);
    msg.payload[len] = '\0';
    msg.rssi = rssi;
    msg.receivedAt = millis();
    g_espnowReceived++;
    portEXIT_CRITICAL(&g_espnowMux);
}

static void onEspNowReceive(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
    int8_t rssi = 0;
    if (info && info->rx_ctrl) rssi = info->rx_ctrl->rssi;
    if (info && info->src_addr) espnowQueuePush(info->src_addr, data, len, rssi);
}

static void onEspNowSend(const esp_now_send_info_t*, esp_now_send_status_t status) {
    if (status == ESP_NOW_SEND_SUCCESS) g_espnowSent++;
}

static bool espnowAddPeer(const uint8_t mac[6], uint8_t channel, String& error) {
    if (esp_now_is_peer_exist(mac)) return true;

    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, mac, 6);
    peer.channel = channel;
    peer.ifidx = WIFI_IF_STA;
    peer.encrypt = false;
    esp_err_t rc = esp_now_add_peer(&peer);
    if (rc != ESP_OK) {
        error = "ESP-NOW add peer failed " + String((int)rc);
        return false;
    }
    return true;
}

static bool ensureEspNow(int requestedChannel, String& error) {
    if (requestedChannel < 0 || requestedChannel > 14) {
        error = "Bad ESP-NOW channel";
        return false;
    }

    WiFi.mode(WIFI_STA);

    uint8_t primary = 0;
    wifi_second_chan_t second = WIFI_SECOND_CHAN_NONE;
    esp_wifi_get_channel(&primary, &second);
    if (requestedChannel > 0 && WiFi.status() == WL_CONNECTED && requestedChannel != primary) {
        error = "WiFi connected; ESP-NOW uses AP channel " + String(primary);
        return false;
    }
    if (requestedChannel > 0 && WiFi.status() != WL_CONNECTED) {
        esp_wifi_set_promiscuous(true);
        esp_err_t channelRc = esp_wifi_set_channel((uint8_t)requestedChannel, WIFI_SECOND_CHAN_NONE);
        esp_wifi_set_promiscuous(false);
        if (channelRc != ESP_OK) {
            error = "ESP-NOW channel failed " + String((int)channelRc);
            return false;
        }
    }

    if (!g_espnowStarted) {
        esp_err_t rc = esp_now_init();
        if (rc != ESP_OK) {
            error = "ESP-NOW init failed " + String((int)rc);
            return false;
        }
        esp_now_register_recv_cb(onEspNowReceive);
        esp_now_register_send_cb(onEspNowSend);
        g_espnowStarted = true;
    }

    return espnowAddPeer(kEspNowBroadcastMac, 0, error);
}

static bool luaCanReadGpio(int pin) {
    for (int allowed : kLuaReadableGpios) {
        if (allowed == pin) return true;
    }
    return false;
}

static bool luaConfigureInputGpio(lua_State* L, int pin, int modeArg) {
    if (!luaCanReadGpio(pin)) {
        lua_pushboolean(L, false);
        lua_pushfstring(L, "GPIO %d is not readable by Lua", pin);
        return false;
    }

    const char* mode = luaL_optstring(L, modeArg, "input");
    if (strcmp(mode, "input") == 0 || strcmp(mode, "floating") == 0) {
        pinMode(pin, INPUT);
    } else if (strcmp(mode, "pullup") == 0 || strcmp(mode, "up") == 0) {
        pinMode(pin, INPUT_PULLUP);
    } else if (strcmp(mode, "pulldown") == 0 || strcmp(mode, "down") == 0) {
        pinMode(pin, INPUT_PULLDOWN);
    } else {
        lua_pushboolean(L, false);
        lua_pushfstring(L, "Bad GPIO mode: %s", mode);
        return false;
    }
    return true;
}

static int luaOnionLog(lua_State* L) {
    const char* message = luaL_checkstring(L, 1);
    setLog("Lua: " + String(message));
    return 0;
}

static int luaOnionHardwareId(lua_State* L) {
    lua_pushstring(L, g_identity.hardwareId.c_str());
    return 1;
}

static int luaOnionOnionId(lua_State* L) {
    lua_pushinteger(L, (lua_Integer)g_identity.onionId);
    return 1;
}

static int luaOnionWallet(lua_State* L) {
    lua_pushstring(L, g_identity.solanaPublicKey.c_str());
    return 1;
}

static int luaOnionDisplaySize(lua_State* L) {
    lua_newtable(L);
    lua_pushinteger(L, display.width());
    lua_setfield(L, -2, "width");
    lua_pushinteger(L, display.height());
    lua_setfield(L, -2, "height");
    return 1;
}

static int luaOnionClearDisplay(lua_State*) {
    g_luaCanvas.fillScreen(0);
    refreshLuaCanvas();
    return 0;
}

static int luaOnionReleaseDisplay(lua_State*) {
    g_luaDisplayActive = false;
    g_needsRedraw = true;
    return 0;
}

static int luaOnionDisplayText(lua_State* L) {
    const char* text = luaL_checkstring(L, 1);
    int x = (int)luaL_optinteger(L, 2, 6);
    int y = (int)luaL_optinteger(L, 3, 22);
    bool clearScreen = luaDisplayClearArg(L, 4, true);
    const GFXfont* font = &FreeMono9pt7b;
    uint16_t color = GxEPD_BLACK;
    uint16_t bg = GxEPD_WHITE;

    if (lua_istable(L, 4)) {
        font = displayFontFromName(luaTableString(L, 4, "font", "small"));
        color = displayColorFromName(luaTableString(L, 4, "color", "black"), GxEPD_BLACK);
        bg = displayColorFromName(luaTableString(L, 4, "background", "white"), GxEPD_WHITE);
    } else if (lua_gettop(L) >= 5) {
        font = displayFontFromName(luaL_optstring(L, 5, "small"));
    }

    if (clearScreen) g_luaCanvas.fillScreen(canvasColor(bg));
    g_luaCanvas.setFont(font);
    g_luaCanvas.setTextColor(canvasColor(color));
    g_luaCanvas.setCursor(x, y);
    g_luaCanvas.print(text);
    refreshLuaCanvas();
    return 0;
}

static int luaOnionDisplayLines(lua_State* L) {
    int x = (int)luaL_optinteger(L, 2, 6);
    int y = (int)luaL_optinteger(L, 3, 22);
    int lineHeight = (int)luaL_optinteger(L, 4, 18);
    bool clearScreen = luaDisplayClearArg(L, 5, true);
    const GFXfont* font = &FreeMono9pt7b;
    uint16_t color = GxEPD_BLACK;
    uint16_t bg = GxEPD_WHITE;
    if (lua_istable(L, 5)) {
        font = displayFontFromName(luaTableString(L, 5, "font", "small"));
        color = displayColorFromName(luaTableString(L, 5, "color", "black"), GxEPD_BLACK);
        bg = displayColorFromName(luaTableString(L, 5, "background", "white"), GxEPD_WHITE);
        lineHeight = luaTableInt(L, 5, "line_height", lineHeight);
    }
    if (lineHeight < 8) lineHeight = 8;
    if (lineHeight > 64) lineHeight = 64;

    if (clearScreen) g_luaCanvas.fillScreen(canvasColor(bg));
    g_luaCanvas.setFont(font);
    g_luaCanvas.setTextColor(canvasColor(color));
    if (lua_istable(L, 1)) {
        int count = (int)lua_rawlen(L, 1);
        for (int i = 1; i <= count; ++i) {
            lua_rawgeti(L, 1, i);
            const char* line = lua_tostring(L, -1);
            if (line) {
                g_luaCanvas.setCursor(x, y + (i - 1) * lineHeight);
                g_luaCanvas.print(line);
            }
            lua_pop(L, 1);
        }
    } else {
        String text = luaL_checkstring(L, 1);
        int line = 0;
        int start = 0;
        while (start <= (int)text.length()) {
            int end = text.indexOf('\n', start);
            if (end < 0) end = text.length();
            g_luaCanvas.setCursor(x, y + line * lineHeight);
            g_luaCanvas.print(text.substring(start, end));
            if (end == (int)text.length()) break;
            start = end + 1;
            line++;
        }
    }
    refreshLuaCanvas();
    return 0;
}

static int luaOnionDisplayLine(lua_State* L) {
    int x0 = (int)luaL_checkinteger(L, 1);
    int y0 = (int)luaL_checkinteger(L, 2);
    int x1 = (int)luaL_checkinteger(L, 3);
    int y1 = (int)luaL_checkinteger(L, 4);
    bool clearScreen = luaDisplayClearArg(L, 5, false);
    uint16_t color = GxEPD_BLACK;
    if (lua_istable(L, 5)) color = displayColorFromName(luaTableString(L, 5, "color", "black"), GxEPD_BLACK);

    if (clearScreen) g_luaCanvas.fillScreen(0);
    g_luaCanvas.drawLine(x0, y0, x1, y1, canvasColor(color));
    refreshLuaCanvas();
    return 0;
}

static int luaOnionDisplayRect(lua_State* L) {
    int x = (int)luaL_checkinteger(L, 1);
    int y = (int)luaL_checkinteger(L, 2);
    int w = (int)luaL_checkinteger(L, 3);
    int h = (int)luaL_checkinteger(L, 4);
    bool clearScreen = luaDisplayClearArg(L, 5, false);
    bool fill = lua_istable(L, 5) ? luaTableBool(L, 5, "fill", false) : false;
    uint16_t color = lua_istable(L, 5)
        ? displayColorFromName(luaTableString(L, 5, "color", "black"), GxEPD_BLACK)
        : GxEPD_BLACK;

    if (clearScreen) g_luaCanvas.fillScreen(0);
    if (fill) g_luaCanvas.fillRect(x, y, w, h, canvasColor(color));
    else g_luaCanvas.drawRect(x, y, w, h, canvasColor(color));
    refreshLuaCanvas();
    return 0;
}

static int luaOnionDisplayBitmap(lua_State* L) {
    String name = luaL_checkstring(L, 1);
    int x = (int)luaL_optinteger(L, 2, 0);
    int y = (int)luaL_optinteger(L, 3, 0);
    bool clearScreen = lua_gettop(L) < 4 || lua_toboolean(L, 4);

    LoadedBitmap bitmap;
    String error;
    if (!loadStoredBitmap(name, bitmap, error)) {
        lua_pushboolean(L, false);
        lua_pushstring(L, error.c_str());
        setLog(error);
        return 2;
    }

    if (x < 0) x = (display.width() - bitmap.width) / 2;
    if (y < 0) y = (display.height() - bitmap.height) / 2;
    renderBitmap(bitmap, x, y, clearScreen);
    lua_pushboolean(L, true);
    return 1;
}

static int luaOnionImages(lua_State* L) {
    lua_newtable(L);

    File root = SPIFFS.open("/");
    if (!root) return 1;

    int index = 1;
    File file = root.openNextFile();
    while (file) {
        String name = file.name();
        if (!file.isDirectory() && name.startsWith("/images_")) {
            name.remove(0, 8);
            if (validImageFileName(name)) {
                lua_pushstring(L, name.c_str());
                lua_rawseti(L, -2, index++);
            }
        }
        file = root.openNextFile();
    }

    return 1;
}

static bool luaButtonMaskForName(const char* name, uint8_t& mask) {
    for (const LuaButton& button : kLuaButtons) {
        if (strcmp(name, button.name) == 0) {
            mask = button.mask;
            return true;
        }
    }
    return false;
}

static int luaOnionButtonMask(lua_State* L) {
    const char* name = luaL_checkstring(L, 1);
    uint8_t mask = 0;
    if (!luaButtonMaskForName(name, mask)) {
        lua_pushnil(L);
        lua_pushfstring(L, "Bad button name: %s", name);
        return 2;
    }
    lua_pushinteger(L, mask);
    return 1;
}

static int luaOnionButtons(lua_State* L) {
    uint8_t buttons = readButtons();
    g_lastButtons = buttons;
    lua_newtable(L);

    lua_pushinteger(L, buttons);
    lua_setfield(L, -2, "mask");

    for (const LuaButton& button : kLuaButtons) {
        lua_pushboolean(L, (buttons & button.mask) != 0);
        lua_setfield(L, -2, button.name);
    }

    return 1;
}

static int luaOnionSleep(lua_State* L) {
    uint32_t ms = (uint32_t)luaL_optinteger(L, 1, 0);
    if (ms > LUA_SLEEP_MAX_MS) ms = LUA_SLEEP_MAX_MS;
    delay(ms);
    return 0;
}

static int luaOnionGpioRead(lua_State* L) {
    int pin = (int)luaL_checkinteger(L, 1);
    if (!luaConfigureInputGpio(L, pin, 2)) return 2;

    lua_pushinteger(L, digitalRead(pin) == HIGH ? 1 : 0);
    return 1;
}

static int luaOnionGpioPoll(lua_State* L) {
    int pin = (int)luaL_checkinteger(L, 1);
    int target = (int)luaL_checkinteger(L, 2) ? 1 : 0;
    uint32_t timeoutMs = (uint32_t)luaL_optinteger(L, 3, 1000);
    uint32_t intervalMs = (uint32_t)luaL_optinteger(L, 4, 25);
    if (timeoutMs > LUA_GPIO_POLL_MAX_MS) timeoutMs = LUA_GPIO_POLL_MAX_MS;
    if (intervalMs < 1) intervalMs = 1;
    if (intervalMs > 1000) intervalMs = 1000;
    if (!luaConfigureInputGpio(L, pin, 5)) return 2;

    uint32_t start = millis();
    int value = digitalRead(pin) == HIGH ? 1 : 0;
    while ((uint32_t)(millis() - start) <= timeoutMs) {
        value = digitalRead(pin) == HIGH ? 1 : 0;
        if (value == target) {
            lua_pushboolean(L, true);
            lua_pushinteger(L, value);
            lua_pushinteger(L, (lua_Integer)(millis() - start));
            return 3;
        }
        delay(intervalMs);
    }

    lua_pushboolean(L, false);
    lua_pushinteger(L, value);
    lua_pushinteger(L, (lua_Integer)(millis() - start));
    return 3;
}

static int luaOnionEspNowStart(lua_State* L) {
    int channel = (int)luaL_optinteger(L, 1, 0);
    String error;
    if (!ensureEspNow(channel, error)) {
        lua_pushboolean(L, false);
        lua_pushstring(L, error.c_str());
        return 2;
    }
    setLog("ESP-NOW ready");
    lua_pushboolean(L, true);
    return 1;
}

static int luaOnionEspNowStop(lua_State* L) {
    if (g_espnowStarted) {
        esp_now_unregister_recv_cb();
        esp_now_unregister_send_cb();
        esp_now_deinit();
        g_espnowStarted = false;
    }
    portENTER_CRITICAL(&g_espnowMux);
    g_espnowQueueHead = 0;
    g_espnowQueueCount = 0;
    portEXIT_CRITICAL(&g_espnowMux);
    lua_pushboolean(L, true);
    return 1;
}

static int luaOnionEspNowMac(lua_State* L) {
    uint8_t mac[6] = {};
    esp_wifi_get_mac(WIFI_IF_STA, mac);
    String macText = macToString(mac);
    lua_pushstring(L, macText.c_str());
    return 1;
}

static int luaOnionEspNowInfo(lua_State* L) {
    uint8_t mac[6] = {};
    uint8_t channel = 0;
    wifi_second_chan_t second = WIFI_SECOND_CHAN_NONE;
    esp_wifi_get_mac(WIFI_IF_STA, mac);
    esp_wifi_get_channel(&channel, &second);

    lua_newtable(L);
    lua_pushboolean(L, g_espnowStarted);
    lua_setfield(L, -2, "started");
    String macText = macToString(mac);
    lua_pushstring(L, macText.c_str());
    lua_setfield(L, -2, "mac");
    lua_pushinteger(L, channel);
    lua_setfield(L, -2, "channel");
    lua_pushinteger(L, (lua_Integer)g_espnowSent);
    lua_setfield(L, -2, "sent");
    lua_pushinteger(L, (lua_Integer)g_espnowReceived);
    lua_setfield(L, -2, "received");
    portENTER_CRITICAL(&g_espnowMux);
    lua_pushinteger(L, g_espnowQueueCount);
    portEXIT_CRITICAL(&g_espnowMux);
    lua_setfield(L, -2, "queued");
    return 1;
}

static int luaOnionEspNowSend(lua_State* L) {
    size_t len = 0;
    const char* payload = luaL_checklstring(L, 1, &len);
    if (len == 0 || len > ONION_ESPNOW_MAX_PAYLOAD) {
        lua_pushboolean(L, false);
        lua_pushfstring(L, "ESP-NOW payload must be 1-%d bytes", ONION_ESPNOW_MAX_PAYLOAD);
        return 2;
    }

    uint8_t mac[6];
    memcpy(mac, kEspNowBroadcastMac, sizeof(mac));
    if (lua_gettop(L) >= 2 && !lua_isnil(L, 2)) {
        if (!parseMacString(luaL_checkstring(L, 2), mac)) {
            lua_pushboolean(L, false);
            lua_pushstring(L, "Bad ESP-NOW MAC");
            return 2;
        }
    }

    String error;
    if (!ensureEspNow(0, error) || !espnowAddPeer(mac, 0, error)) {
        lua_pushboolean(L, false);
        lua_pushstring(L, error.c_str());
        return 2;
    }

    esp_err_t rc = esp_now_send(mac, reinterpret_cast<const uint8_t*>(payload), len);
    if (rc != ESP_OK) {
        lua_pushboolean(L, false);
        lua_pushfstring(L, "ESP-NOW send failed %d", (int)rc);
        return 2;
    }
    lua_pushboolean(L, true);
    return 1;
}

static int luaOnionEspNowReceive(lua_State* L) {
    uint32_t timeoutMs = (uint32_t)luaL_optinteger(L, 1, 0);
    if (timeoutMs > LUA_ESPNOW_RECV_MAX_MS) timeoutMs = LUA_ESPNOW_RECV_MAX_MS;

    String error;
    if (!ensureEspNow(0, error)) {
        lua_pushnil(L);
        lua_pushstring(L, error.c_str());
        return 2;
    }

    uint32_t start = millis();
    EspNowQueuedMessage msg;
    while (!espnowQueuePop(msg)) {
        if ((uint32_t)(millis() - start) >= timeoutMs) {
            lua_pushnil(L);
            return 1;
        }
        delay(10);
    }

    lua_newtable(L);
    String macText = macToString(msg.mac);
    lua_pushstring(L, macText.c_str());
    lua_setfield(L, -2, "mac");
    lua_pushlstring(L, msg.payload, msg.len);
    lua_setfield(L, -2, "payload");
    lua_pushlstring(L, msg.payload, msg.len);
    lua_setfield(L, -2, "message");
    lua_pushinteger(L, msg.len);
    lua_setfield(L, -2, "len");
    lua_pushinteger(L, msg.rssi);
    lua_setfield(L, -2, "rssi");
    lua_pushinteger(L, (lua_Integer)msg.receivedAt);
    lua_setfield(L, -2, "received_at");
    return 1;
}

static int luaOnionHttpPost(lua_State* L) {
    const char* url  = luaL_checkstring(L, 1);
    const char* body = luaL_checkstring(L, 2);

    if (!ensureWifi()) {
        lua_newtable(L);
        lua_pushinteger(L, -1);
        lua_setfield(L, -2, "status");
        lua_pushstring(L, "no wifi");
        lua_setfield(L, -2, "body");
        return 1;
    }

    String responseBuffer;
    esp_http_client_config_t cfg = {};
    cfg.url               = url;
    cfg.timeout_ms        = 15000;
    cfg.crt_bundle_attach = esp_crt_bundle_attach;
    cfg.event_handler     = httpCaptureEvent;
    cfg.user_data         = &responseBuffer;

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        lua_newtable(L);
        lua_pushinteger(L, -1);
        lua_setfield(L, -2, "status");
        lua_pushstring(L, "init failed");
        lua_setfield(L, -2, "body");
        return 1;
    }

    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    if (g_config.aiApiKey.length()) {
        esp_http_client_set_header(client, "x-api-key", g_config.aiApiKey.c_str());
        esp_http_client_set_header(client, "anthropic-version", "2023-06-01");
    }
    esp_http_client_set_post_field(client, body, strlen(body));

    esp_err_t err = esp_http_client_perform(client);
    int code = (err == ESP_OK) ? esp_http_client_get_status_code(client) : -1;
    esp_http_client_cleanup(client);

    lua_newtable(L);
    lua_pushinteger(L, code);
    lua_setfield(L, -2, "status");
    lua_pushstring(L, responseBuffer.c_str());
    lua_setfield(L, -2, "body");
    return 1;
}

static void registerOnionLua(lua_State* L) {
    lua_newtable(L);
    lua_pushcfunction(L, luaOnionLog);
    lua_setfield(L, -2, "log");
    lua_pushcfunction(L, luaOnionHardwareId);
    lua_setfield(L, -2, "hardware_id");
    lua_pushcfunction(L, luaOnionOnionId);
    lua_setfield(L, -2, "onion_id");
    lua_pushcfunction(L, luaOnionWallet);
    lua_setfield(L, -2, "wallet");
    lua_pushcfunction(L, luaOnionDisplaySize);
    lua_setfield(L, -2, "display_size");
    lua_pushcfunction(L, luaOnionClearDisplay);
    lua_setfield(L, -2, "clear_display");
    lua_pushcfunction(L, luaOnionReleaseDisplay);
    lua_setfield(L, -2, "release_display");
    lua_pushcfunction(L, luaOnionDisplayText);
    lua_setfield(L, -2, "display_text");
    lua_pushcfunction(L, luaOnionDisplayLines);
    lua_setfield(L, -2, "display_lines");
    lua_pushcfunction(L, luaOnionDisplayLine);
    lua_setfield(L, -2, "display_line");
    lua_pushcfunction(L, luaOnionDisplayRect);
    lua_setfield(L, -2, "display_rect");
    lua_pushcfunction(L, luaOnionDisplayBitmap);
    lua_setfield(L, -2, "display_bitmap");
    lua_pushcfunction(L, luaOnionImages);
    lua_setfield(L, -2, "images");
    lua_pushcfunction(L, luaOnionButtons);
    lua_setfield(L, -2, "buttons");
    lua_pushcfunction(L, luaOnionButtonMask);
    lua_setfield(L, -2, "button_mask");
    lua_pushcfunction(L, luaOnionSleep);
    lua_setfield(L, -2, "sleep");
    lua_pushcfunction(L, luaOnionGpioRead);
    lua_setfield(L, -2, "gpio_read");
    lua_pushcfunction(L, luaOnionGpioPoll);
    lua_setfield(L, -2, "gpio_poll");
    lua_pushcfunction(L, luaOnionEspNowStart);
    lua_setfield(L, -2, "espnow_start");
    lua_pushcfunction(L, luaOnionEspNowStop);
    lua_setfield(L, -2, "espnow_stop");
    lua_pushcfunction(L, luaOnionEspNowMac);
    lua_setfield(L, -2, "espnow_mac");
    lua_pushcfunction(L, luaOnionEspNowInfo);
    lua_setfield(L, -2, "espnow_info");
    lua_pushcfunction(L, luaOnionEspNowSend);
    lua_setfield(L, -2, "espnow_send");
    lua_pushcfunction(L, luaOnionEspNowReceive);
    lua_setfield(L, -2, "espnow_receive");
    lua_pushcfunction(L, luaOnionHttpPost);
    lua_setfield(L, -2, "http_post");
    lua_pushcfunction(L, luaOnionDisplayBeginBatch);
    lua_setfield(L, -2, "display_begin_batch");
    lua_pushcfunction(L, luaOnionDisplayEndBatch);
    lua_setfield(L, -2, "display_end_batch");
    lua_setglobal(L, "onion");
}

static bool runLuaSource(const String& source, const String& name) {
    lua_State* L = luaL_newstate();
    if (!L) {
        setLog("Lua state failed");
        return false;
    }
    luaL_openlibs(L);
    registerOnionLua(L);

    String logBeforeRun = g_log;
    int status = luaL_loadbuffer(L, source.c_str(), source.length(), name.c_str());
    if (status == LUA_OK) status = lua_pcall(L, 0, 0, 0);
    if (status != LUA_OK) {
        String err = lua_tostring(L, -1);
        lua_close(L);
        g_luaBatchMode = false;
        setLog("Lua error: " + clipped(err, 22));
        return false;
    }

    lua_close(L);
    g_luaBatchMode = false;
    if (g_luaDisplayActive) {
        Serial.printf("[onion-os] Lua ran %s\n", name.c_str());
    } else {
        if (g_log == logBeforeRun) setLog("Lua ran " + name);
    }
    return true;
}

static void runStoredScript(const String& path) {
    File file = SPIFFS.open(path, FILE_READ);
    if (!file) {
        setLog("Lua script missing");
        return;
    }
    if (file.size() > MAX_SCRIPT_BYTES) {
        file.close();
        setLog("Lua script too large");
        return;
    }
    String source;
    source.reserve(file.size());
    while (file.available()) source += (char)file.read();
    file.close();
    runLuaSource(source, path);
}

static void runScriptByName(const String& name) {
    if (!name.length() || name.indexOf('/') >= 0) {
        setLog("Bad script name");
        return;
    }
    runStoredScript("/scripts_" + name);
}

static bool validScriptFileName(const String& name) {
    return validAssetFileName(name, ".lua");
}

static bool validStoredScriptPath(const String& path) {
    if (!path.startsWith("/scripts_") || !path.endsWith(".lua")) return false;
    return validScriptFileName(storedScriptDisplayName(path));
}

static bool deleteStoredScript(const String& path) {
    if (!validStoredScriptPath(path)) {
        setLog("Bad script path");
        return false;
    }
    String name = storedScriptDisplayName(path);
    if (!SPIFFS.remove(path)) {
        setLog("Lua delete failed");
        return false;
    }
    setLog("Deleted " + clipped(name, 20));
    return true;
}

static void deleteScriptByName(const String& name) {
    if (!validScriptFileName(name)) {
        setLog("Bad script name");
        return;
    }
    deleteStoredScript("/scripts_" + name);
}

static String pushedScriptFileName() {
    if (validScriptFileName(g_luaPrompt.fileName)) return g_luaPrompt.fileName;
    String suffix = g_luaPrompt.scriptId.length() ? g_luaPrompt.scriptId : g_luaPrompt.requestId;
    String safe;
    safe.reserve(64);
    for (size_t i = 0; i < suffix.length() && safe.length() < 64; ++i) {
        char ch = suffix[i];
        bool ok = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
            (ch >= '0' && ch <= '9') || ch == '-' || ch == '_';
        safe += ok ? ch : '_';
    }
    if (!safe.length()) safe = "script";
    return "pushed_" + safe + ".lua";
}

static bool installAndRunPushedScript(String& error) {
    if (!g_luaPrompt.requestId.length()) {
        error = "Missing Lua request";
        return false;
    }
    if (!g_luaPrompt.code.length()) {
        error = "Empty Lua script";
        return false;
    }
    if (g_luaPrompt.code.length() > MAX_SCRIPT_BYTES || g_luaPrompt.sizeBytes > MAX_SCRIPT_BYTES) {
        error = "Lua script too large";
        return false;
    }

    String name = pushedScriptFileName();
    String path = "/scripts_" + name;
    File file = SPIFFS.open(path, FILE_WRITE);
    if (!file) {
        error = "Lua write failed";
        return false;
    }
    size_t written = file.print(g_luaPrompt.code);
    file.close();
    if (written != g_luaPrompt.code.length()) {
        SPIFFS.remove(path);
        error = "Lua write short";
        return false;
    }

    if (!runLuaSource(g_luaPrompt.code, name)) {
        error = "Lua runtime error";
        return false;
    }
    return true;
}

static bool sendLuaPushResponse(bool approved) {
    if (!g_identity.onionId || !g_luaPrompt.requestId.length()) {
        setLog("No Lua push active");
        return false;
    }

    String error;
    bool accepted = approved;
    if (approved && !installAndRunPushedScript(error)) {
        accepted = false;
    }
    if (!approved) error = "User denied";

    String body = "{\"onionId\":" + String((unsigned long long)g_identity.onionId) +
        ",\"requestId\":\"" + jsonEscape(g_luaPrompt.requestId) +
        "\",\"approved\":" + String(accepted ? "true" : "false");
    if (!accepted && error.length()) {
        body += ",\"error\":\"" + jsonEscape(error) + "\"";
    }
    body += "}";

    bool sentOverMqtt = publishMqtt(topic("badge/" + String((unsigned long long)g_identity.onionId) + "/lua/response"), body);
    String response;
    int code = sentOverMqtt ? 200 : httpPostJson("/api/badge/lua-response", body, &response);
    if (code >= 200 && code < 300) {
        setLog(accepted ? "Lua installed" : "Lua denied");
        g_luaPrompt.active = false;
        g_luaPrompt.code = "";
        g_screen = SCREEN_STATUS;
    } else {
        setLog("Lua response HTTP " + String(code));
    }
    return code >= 200 && code < 300;
}

static void syncScripts() {
    if (!g_config.scriptManifestUrl.length()) {
        setLog("No script manifest URL");
        return;
    }
    if (!ensureWifi()) return;

    String payload;
    int code = httpGetString(g_config.scriptManifestUrl, &payload);

    if (code < 200 || code >= 300) {
        setLog("Manifest GET failed " + String(code));
        return;
    }

    cJSON* root = cJSON_Parse(payload.c_str());
    cJSON* scripts = root ? cJSON_GetObjectItemCaseSensitive(root, "scripts") : nullptr;
    cJSON* images = root ? cJSON_GetObjectItemCaseSensitive(root, "images") : nullptr;
    if (!cJSON_IsArray(scripts) && !cJSON_IsArray(images)) {
        if (root) cJSON_Delete(root);
        setLog("Bad script manifest");
        return;
    }

    int count = 0;
    if (cJSON_IsArray(scripts)) {
        cJSON* script = nullptr;
        cJSON_ArrayForEach(script, scripts) {
            String name = jsonString(script, "name");
            String url = jsonString(script, "url");
            bool autorun = jsonBool(script, "autorun", false);
            if (!validScriptFileName(name) || !url.length()) continue;
            String path = "/scripts_" + name;
            if (downloadScriptFile(url, path)) {
                count++;
                if (autorun) runStoredScript(path);
            }
        }
    }
    int imageCount = 0;
    if (cJSON_IsArray(images)) {
        cJSON* image = nullptr;
        cJSON_ArrayForEach(image, images) {
            String name = jsonString(image, "name");
            String url = jsonString(image, "url");
            if (!validImageFileName(name) || !url.length()) continue;
            if (downloadImageFile(url, imagePathForName(name))) imageCount++;
        }
    }
    cJSON_Delete(root);
    setLog("Synced S:" + String(count) + " I:" + String(imageCount));
    refreshScriptList();
}

static void printHelp() {
    Serial.println();
    Serial.println("Onion OS serial commands:");
    Serial.println("  api-key <badge_api_key>");
    Serial.println("  mqtt-auth [username] [password] [prefix]");
    Serial.println("  scripts-url <manifest_url>");
    Serial.println("  wallet");
    Serial.println("  keygen confirm");
    Serial.println("  handshake");
    Serial.println("  scripts");
    Serial.println("  run <script_name.lua>");
    Serial.println("  delete <script_name.lua>");
    Serial.println("  state");
    Serial.println("  help");
    Serial.println();
}

static std::vector<String> splitCommand(const String& line) {
    std::vector<String> parts;
    int start = 0;
    while (start < (int)line.length()) {
        while (start < (int)line.length() && line[start] == ' ') start++;
        if (start >= (int)line.length()) break;
        int end = line.indexOf(' ', start);
        if (end < 0) end = line.length();
        parts.push_back(line.substring(start, end));
        start = end + 1;
    }
    return parts;
}

static void handleSerial() {
    static String line;
    while (Serial.available()) {
        char ch = (char)Serial.read();
        if (ch == '\r') continue;
        if (ch != '\n') {
            line += ch;
            continue;
        }

        line.trim();
        std::vector<String> args = splitCommand(line);
        line = "";
        if (args.empty()) return;

        if (args[0] == "wifi") {
            setLog("WiFi is hardcoded");
        } else if (args[0] == "server") {
            g_config.serverBaseUrl = ONION_HARDCODED_SERVER_BASE_URL;
            if (args.size() >= 3) {
                g_config.badgeApiKey = args[2];
                saveConfigValue("api_key", g_config.badgeApiKey);
                setLog("API key saved; URL hardcoded");
            } else {
                setLog("Server URL is hardcoded");
            }
        } else if (args[0] == "api-key" && args.size() >= 2) {
            g_config.badgeApiKey = args[1];
            saveConfigValue("api_key", g_config.badgeApiKey);
            setLog("API key saved");
        } else if (args[0] == "ai-key" && args.size() >= 2) {
            g_config.aiApiKey = args[1];
            saveConfigValue("ai_key", g_config.aiApiKey);
            setLog("AI key saved");
        } else if (args[0] == "mqtt" || args[0] == "mqtt-auth") {
            g_config.mqttUri = ONION_HARDCODED_MQTT_URI;
            size_t firstAuthArg = 1;
            if (args[0] == "mqtt" && args.size() >= 2 &&
                (args[1].indexOf("://") >= 0 || args[1].indexOf(':') >= 0)) {
                firstAuthArg = 2;
            }
            g_config.mqttUsername = args.size() > firstAuthArg ? args[firstAuthArg] : "";
            g_config.mqttPassword = args.size() > firstAuthArg + 1 ? args[firstAuthArg + 1] : "";
            g_config.mqttTopicPrefix = args.size() > firstAuthArg + 2 ? args[firstAuthArg + 2] : "oniondao";
            g_prefs.remove("mqtt_uri");
            saveConfigValue("mqtt_user", g_config.mqttUsername);
            saveConfigValue("mqtt_pass", g_config.mqttPassword);
            saveConfigValue("mqtt_prefix", g_config.mqttTopicPrefix);
            if (g_mqtt) {
                esp_mqtt_client_stop(g_mqtt);
                esp_mqtt_client_destroy(g_mqtt);
                g_mqtt = nullptr;
                g_mqttConnected = false;
            }
            setLog("MQTT auth saved; URL hardcoded");
        } else if (args[0] == "scripts-url" && args.size() >= 2) {
            g_config.scriptManifestUrl = args[1];
            saveConfigValue("script_url", g_config.scriptManifestUrl);
            setLog("Script URL saved");
        } else if (args[0] == "wallet") {
            String keyError;
            if (loadOrCreateSolanaKey(false, keyError)) {
                Serial.printf("wallet=%s\n", g_identity.solanaPublicKey.c_str());
                setLog("Wallet ready");
            } else {
                Serial.printf("wallet_error=%s\n", keyError.c_str());
                setLog(keyError);
            }
        } else if (args[0] == "keygen" && args.size() >= 2 && args[1] == "confirm") {
            if (g_identity.linked) {
                setLog("Refusing linked key rotation");
            } else {
                clearSolanaKey();
                String keyError;
                if (loadOrCreateSolanaKey(true, keyError)) {
                    Serial.printf("wallet=%s\n", g_identity.solanaPublicKey.c_str());
                    setLog("Wallet rotated");
                } else {
                    Serial.printf("wallet_error=%s\n", keyError.c_str());
                    setLog(keyError);
                }
            }
        } else if (args[0] == "handshake") {
            doHttpHandshake();
            doMqttHandshake();
        } else if (args[0] == "scripts") {
            syncScripts();
        } else if (args[0] == "ls") {
            File root = SPIFFS.open("/");
            File f = root.openNextFile();
            int n = 0;
            while (f) {
                Serial.printf("  [%s] %d bytes\n", f.name(), (int)f.size());
                n++;
                f = root.openNextFile();
            }
            Serial.printf("Total: %d files\n", n);
        } else if (args[0] == "run" && args.size() >= 2) {
            runScriptByName(args[1]);
        } else if ((args[0] == "delete" || args[0] == "rm") && args.size() >= 2) {
            deleteScriptByName(args[1]);
        } else if (args[0] == "state") {
            Serial.printf("hardwareId=%s\n", g_identity.hardwareId.c_str());
            Serial.printf("onionId=%llu\n", (unsigned long long)g_identity.onionId);
            Serial.printf("status=%s\n", g_identity.status.c_str());
            Serial.printf("username=%s\n", g_identity.username.c_str());
            Serial.printf("onions=%s\n", g_identity.onionCount.c_str());
            Serial.printf("wallet=%s\n", g_identity.solanaPublicKey.c_str());
            Serial.printf("wifi=%s\n", g_config.wifiSsid.c_str());
            Serial.printf("server=%s\n", g_config.serverBaseUrl.c_str());
            Serial.printf("mqtt=%s\n", g_config.mqttUri.c_str());
        } else {
            printHelp();
        }
    }
}

static void handleButtons() {
    uint8_t buttons = readButtons();
    uint8_t pressed = buttons & ~g_lastButtons;
    g_lastButtons = buttons;
    if (!pressed) return;

    if (g_luaDisplayActive) {
        g_luaDisplayActive = false;
        g_screen = SCREEN_STATUS;
        g_needsRedraw = true;
        return;
    }

    if (g_screen == SCREEN_LINK_PROMPT) {
        if (pressed & BTN_SELECT) sendLinkResponse(true);
        if (pressed & BTN_CANCEL) sendLinkResponse(false);
    } else if (g_screen == SCREEN_TX_PROMPT) {
        if (pressed & BTN_SELECT) sendTransactionResponse(true);
        if (pressed & BTN_CANCEL) sendTransactionResponse(false);
    } else if (g_screen == SCREEN_LUA_PROMPT) {
        if (pressed & BTN_SELECT) sendLuaPushResponse(true);
        if (pressed & BTN_CANCEL) sendLuaPushResponse(false);
    } else if (g_screen == SCREEN_SCRIPT_EXPLORER) {
        if (pressed & BTN_CANCEL) {
            g_screen = SCREEN_STATUS;
        } else if ((pressed & BTN_LEFT) && !g_scripts.empty()) {
            deleteStoredScript(g_scripts[g_scriptSelection]);
            refreshScriptList();
        } else if ((pressed & BTN_RIGHT)) {
            syncScripts();
            refreshScriptList();
        } else if ((pressed & BTN_SELECT) && !g_scripts.empty()) {
            runStoredScript(g_scripts[g_scriptSelection]);
        } else {
            if ((pressed & BTN_UP) && g_scriptSelection > 0) g_scriptSelection--;
            if ((pressed & BTN_DOWN) && g_scriptSelection + 1 < (int)g_scripts.size()) g_scriptSelection++;
        }
    } else {
        if (pressed & BTN_UP) {
            g_homeSelection = (g_homeSelection + HOME_ITEM_COUNT - 1) % HOME_ITEM_COUNT;
        }
        if (pressed & BTN_DOWN) {
            g_homeSelection = (g_homeSelection + 1) % HOME_ITEM_COUNT;
        }
        if (pressed & BTN_SELECT) {
            if (g_homeSelection == HOME_ITEM_SCRIPTS) {
                refreshScriptList();
                g_screen = SCREEN_SCRIPT_EXPLORER;
            } else if (g_homeSelection == HOME_ITEM_SYNC) {
                syncScripts();
            } else if (g_homeSelection == HOME_ITEM_REFRESH) {
                doHttpHandshake();
                doMqttHandshake();
                refreshPublicProfile();
            }
        }
        if (pressed & BTN_RIGHT) {
            refreshScriptList();
            g_screen = SCREEN_SCRIPT_EXPLORER;
        }
    }
    if (!g_luaDisplayActive) g_needsRedraw = true;
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
}

void loop() {
    handleSerial();
    handleButtons();

    if (WiFi.status() != WL_CONNECTED) ensureWifi();
    ensureMqtt();

    if (millis() - g_lastHandshake > HANDSHAKE_INTERVAL_MS) {
        g_lastHandshake = millis();
        if (g_mqttConnected) doMqttHandshake();
    }

    redraw();
    delay(20);
}
