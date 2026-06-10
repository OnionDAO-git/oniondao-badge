#pragma once

#include <cstdint>
#include <cstring>
#include <Arduino.h>

#include "badge_pins.h"

// ── Onion config defaults ──────────────────────────────────────────────────
#if __has_include("onion_config.generated.h")
#include "onion_config.generated.h"
#elif __has_include("onion_config.h")
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

#ifndef ONION_HARDCODED_WIFI_SSID
#define ONION_HARDCODED_WIFI_SSID "CIC Guest"
#endif
#ifndef ONION_HARDCODED_WIFI_PASSWORD
#define ONION_HARDCODED_WIFI_PASSWORD "1nnovation"
#endif
#ifndef ONION_HARDCODED_SERVER_BASE_URL
#define ONION_HARDCODED_SERVER_BASE_URL "https://oniondao.dev"
#endif
#ifndef ONION_HARDCODED_MQTT_URI
#define ONION_HARDCODED_MQTT_URI "mqtt://shortline.proxy.rlwy.net:20928"
#endif
#ifndef ONION_HARDCODED_MQTT_USERNAME
#define ONION_HARDCODED_MQTT_USERNAME "oniondao"
#endif
#ifndef ONION_HARDCODED_MQTT_PASSWORD
#define ONION_HARDCODED_MQTT_PASSWORD "02eb3d5e04fd2dc9cbb6f3f5c6c9d89d9c96acd987cc24ddf9f717f9480e8786"
#endif

// ── Hardware constants ─────────────────────────────────────────────────────
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
#define PROFILE_REFRESH_INTERVAL_MS 60000
#define MQTT_RECONNECT_INTERVAL_MS 5000
#define MQTT_HANDSHAKE_ACCEPT_WINDOW_MS 10000
#define BOOT_SPLASH_MS 3000
#define MAX_SCRIPT_BYTES (256 * 1024)
#define MQTT_CLIENT_BUFFER_BYTES (32 * 1024)
#define MQTT_CLIENT_OUT_BUFFER_BYTES 4096
#define MQTT_RX_MAX_BYTES (MAX_SCRIPT_BYTES * 2 + 4096)
#define MQTT_RX_TEMP_JSON_PATH "/mqtt_lua_push.json"
#define LUA_PUSH_TEMP_PATH "/scripts_push_tmp.lua"
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
#define LUA_KV_MAX_VALUE 240
#define ONION_ESPNOW_QUEUE_LEN 128
#define LUA_HTTP_MAX_TIMEOUT_MS 30000
#define LUA_HTTP_DEFAULT_TIMEOUT_MS 10000
#define LUA_MQTT_RECV_MAX_MS 30000
#define ONION_LUA_MQTT_MAX_TOPIC 128
#define ONION_LUA_MQTT_MAX_PAYLOAD 512
#define ONION_LUA_MQTT_QUEUE_LEN 8
#define ONION_LUA_MQTT_MAX_SUBS 8
#define ONION_CHECKIN_SCAN_INTERVAL_MS 5000
#define ONION_CHECKIN_PROMPT_COOLDOWN_MS 300000
#define ONION_CHECKIN_RESULT_MS 15000
#define ONION_CHECKIN_DEFAULT_MIN_RSSI -62
#define CC1101_XOSC_MHZ 26.0
#define CC1101_SPI_HZ 4000000
#define SUBGHZ_MAX_PACKET 61
#define SUBGHZ_RX_MAX_MS 30000
#define SOUND_SPK_SAMPLE_RATE 44100
#define SOUND_MIC_SAMPLE_RATE 16000
#define SOUND_TONE_MAX_MS 10000
#define SOUND_PLAY_MAX_BYTES 65536
#define SOUND_MIC_MAX_SAMPLES 4096
#define SOUND_MIC_READ_MAX_TIMEOUT_MS 5000
#define SOUND_MIC_MAX_DISCARD_MS 1000
#define SOUND_AMP_UNMUTE_MS 100
#define ONION_DISPLAY_WIDTH 264
#define ONION_DISPLAY_HEIGHT 176
#ifndef PIN_BATTERY_ADC
#define PIN_BATTERY_ADC -1
#endif
#ifndef BATTERY_ADC_DIVIDER_RATIO
#define BATTERY_ADC_DIVIDER_RATIO 2.0f
#endif
#ifndef BATTERY_ADC_OFFSET_MV
#define BATTERY_ADC_OFFSET_MV 0
#endif
#define BATTERY_SAMPLE_INTERVAL_MS 60000
#define BATTERY_REDRAW_PERCENT_STEP 5

#define WIFI_WORKER_IDLE    0
#define WIFI_WORKER_RUNNING 1
#define WIFI_WORKER_DONE    2
#define WIFI_WORKER_FAILED  3

// ── Enums ──────────────────────────────────────────────────────────────────

enum Screen : uint8_t {
    SCREEN_BOOT_SPLASH,
    SCREEN_STATUS,
    SCREEN_SCRIPT_EXPLORER,
    SCREEN_LINK_PROMPT,
    SCREEN_TX_PROMPT,
    SCREEN_LUA_PROMPT,
    SCREEN_CHECKIN_PROMPT,
    SCREEN_CHECKIN_RESULT,
    SCREEN_LOG,
    SCREEN_SETTINGS,
    SCREEN_WIFI_OVERVIEW,
    SCREEN_WIFI_SCANNING,
    SCREEN_WIFI_LIST,
    SCREEN_WIFI_PASSWORD,
    SCREEN_WIFI_CONNECTING,
    SCREEN_WIFI_RESULT,
    SCREEN_DELETE_CONFIRM,
};

enum ActiveModule : uint8_t {
    MODULE_NONE,
    MODULE_SUBGHZ,
    MODULE_SOUND_SPK,
    MODULE_SOUND_MIC,
};

enum HomeItem : int {
    HOME_ITEM_SCRIPTS,
    HOME_ITEM_REFRESH,
    HOME_ITEM_SETTINGS,
    HOME_ITEM_COUNT,
};

enum CheckInPacketType : uint8_t {
    CHECKIN_PACKET_ADVERTISE = 1,
    CHECKIN_PACKET_APPROVE = 2,
    CHECKIN_PACKET_RESULT = 3,
};

// ── Structs ────────────────────────────────────────────────────────────────

struct RuntimeConfig {
    String wifiSsid;
    String wifiPassword;
    String serverBaseUrl;
    String badgeApiKey;
    String mqttUri;
    String mqttUsername;
    String mqttPassword;
    String mqttTopicPrefix;
    String scriptManifestUrl;
    String moduleVariant;
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
    String downloadUrl;
    String code;
    String codePath;
    int sizeBytes = 0;
    bool active = false;
};

struct CheckInPrompt {
    String beaconId;
    String room;
    String label;
    uint8_t beaconMac[6] = {};
    uint8_t nonce[8] = {};
    int8_t rssi = 0;
    int8_t minRssi = ONION_CHECKIN_DEFAULT_MIN_RSSI;
    bool active = false;
};

struct CheckInResult {
    String beaconId;
    String message;
    int points = 0;
    bool awarded = false;
    uint32_t shownAt = 0;
};

struct WifiNetwork {
    char ssid[33];
    int8_t rssi;
    bool secured;
};

struct WifiConnectArgs {
    char ssid[33];
    char pass[65];
};

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

struct MqttQueuedMessage {
    char topic[ONION_LUA_MQTT_MAX_TOPIC + 1] = {};
    char payload[ONION_LUA_MQTT_MAX_PAYLOAD + 1] = {};
    uint16_t topicLen = 0;
    uint16_t payloadLen = 0;
    uint32_t receivedAt = 0;
};

static const char kCheckInMagic[6] = {'O', 'N', 'C', 'H', 'K', '1'};
static const uint8_t kCheckInVersion = 1;

struct __attribute__((packed)) CheckInPacketHeader {
    char magic[6];
    uint8_t version;
    uint8_t type;
};

struct __attribute__((packed)) CheckInAdvertisePacket {
    CheckInPacketHeader header;
    char beaconId[32];
    char room[32];
    char label[48];
    int8_t minRssi;
    uint8_t nonce[8];
    uint32_t sequence;
};

struct __attribute__((packed)) CheckInApprovePacket {
    CheckInPacketHeader header;
    char beaconId[32];
    uint8_t nonce[8];
    char hardwareId[65];
    uint64_t onionId;
    char username[32];
    char wallet[48];
    int8_t rssi;
    uint32_t approvedAt;
    uint8_t badgeMac[6];
};

struct __attribute__((packed)) CheckInResultPacket {
    CheckInPacketHeader header;
    char beaconId[32];
    uint8_t nonce[8];
    uint8_t awarded;
    uint16_t points;
    char message[80];
};

struct CheckInPendingOffer {
    uint8_t beaconMac[6] = {};
    char beaconId[33] = {};
    char room[33] = {};
    char label[49] = {};
    uint8_t nonce[8] = {};
    int8_t rssi = 0;
    int8_t minRssi = ONION_CHECKIN_DEFAULT_MIN_RSSI;
    uint32_t seenAt = 0;
};

struct CheckInPendingResult {
    char beaconId[33] = {};
    uint8_t nonce[8] = {};
    bool awarded = false;
    uint16_t points = 0;
    char message[81] = {};
};

// ── Static const tables ────────────────────────────────────────────────────

static const int kLuaReadableGpios[] = {48, 47, 19, 42, 41, 40, 38, 39, 16, 15, 7, 6, 5, 4};

static const LuaButton kLuaButtons[] = {
    {"left", BTN_LEFT},
    {"down", BTN_DOWN},
    {"up", BTN_UP},
    {"right", BTN_RIGHT},
    {"select", BTN_SELECT},
    {"cancel", BTN_CANCEL},
};

static const uint8_t kEspNowBroadcastMac[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
