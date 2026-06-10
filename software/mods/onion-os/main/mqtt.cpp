#include "mqtt.h"
#include "badge_state.h"
#include "display.h"
#include "http.h"
#include "wifi.h"
#include "solana.h"
#include <mqtt_client.h>
#include <SPIFFS.h>
#include <esp_crt_bundle.h>

String topic(const String& suffix) {
    String prefix = g_config.mqttTopicPrefix.length() ? g_config.mqttTopicPrefix : "oniondao";
    if (prefix.endsWith("/")) prefix.remove(prefix.length() - 1);
    return prefix + "/" + suffix;
}

uint64_t handshakeOnionIdFromTopic(const String& incomingTopic) {
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

void subscribeBadgeTopics() {
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

bool mqttHandshakeResponseMatchesBadge(cJSON* root, const String& incomingTopic, uint64_t onionId) {
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

bool handleHandshakeResponse(const String& response, const String& incomingTopic) {
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

void doHttpHandshake() {
    String body = "{\"hardwareId\":\"" + g_identity.hardwareId + "\",\"firmware\":\"onion-os\",\"transport\":\"http\"}";
    String response;
    int code = httpPostJson("/api/badge/handshake", body, &response);
    if (code >= 200 && code < 300 && handleHandshakeResponse(response)) return;
    setLog("HTTP handshake failed " + String(code));
}

bool publishMqtt(const String& mqttTopic, const String& payload) {
    if (!g_mqtt || !g_mqttConnected) return false;
    return esp_mqtt_client_publish(g_mqtt, mqttTopic.c_str(), payload.c_str(), 0, 1, 0) >= 0;
}

void doMqttHandshake() {
    String replyTo = topic(String("badge/hardware/") + g_identity.hardwareId + "/handshake/accepted");
    String body = "{\"hardwareId\":\"" + jsonEscape(g_identity.hardwareId) +
        "\",\"firmware\":\"onion-os\",\"transport\":\"mqtt\",\"replyTo\":\"" + jsonEscape(replyTo) + "\"}";
    if (publishMqtt(topic("badge/handshake"), body)) {
        g_mqttHandshakePending = true;
        g_mqttHandshakeSentAt = millis();
    }
}

void handleLinkRequest(cJSON* root) {
    g_luaDisplayActive = false;
    g_forceFullRefresh = true;
    g_linkPrompt.requestId = jsonString(root, "requestId");
    g_linkPrompt.username = jsonString(root, "username");
    g_linkPrompt.active = true;
    g_screen = SCREEN_LINK_PROMPT;
    setLog("Link request received");
}

void handleTransactionRequest(cJSON* root) {
    g_luaDisplayActive = false;
    g_forceFullRefresh = true;
    g_txPrompt.operationId = jsonString(root, "operationId");
    g_txPrompt.requestId = jsonString(root, "requestId");
    g_txPrompt.type = jsonString(root, "type");
    g_txPrompt.amount = jsonInt(root, "amount", 0);
    g_txPrompt.transactionBase64 = jsonString(root, "transaction");
    g_txPrompt.active = true;
    g_screen = SCREEN_TX_PROMPT;
    setLog("Transaction request received");
}

bool handleLuaRequestJson(const String& payload) {
    g_luaDisplayActive = false;
    g_forceFullRefresh = true;

    g_luaPrompt.requestId = "";
    g_luaPrompt.scriptId = "";
    g_luaPrompt.title = "";
    g_luaPrompt.fileName = "";
    g_luaPrompt.description = "";
    g_luaPrompt.authorUsername = "";
    g_luaPrompt.downloadUrl = "";
    g_luaPrompt.code = "";
    g_luaPrompt.codePath = "";
    g_luaPrompt.sizeBytes = jsonExtractIntField(payload, "sizeBytes", 0);
    if (g_luaPrompt.sizeBytes > 0) g_luaPrompt.code.reserve((unsigned int)g_luaPrompt.sizeBytes + 1);

    g_luaPrompt.requestId = jsonExtractStringField(payload, "requestId");
    g_luaPrompt.scriptId = jsonExtractStringField(payload, "scriptId");
    g_luaPrompt.title = jsonExtractStringField(payload, "title");
    g_luaPrompt.fileName = jsonExtractStringField(payload, "fileName");
    g_luaPrompt.description = jsonExtractStringField(payload, "description");
    g_luaPrompt.authorUsername = jsonExtractStringField(payload, "authorUsername");
    g_luaPrompt.downloadUrl = jsonExtractStringField(payload, "downloadUrl");
    if (!g_luaPrompt.downloadUrl.length()) g_luaPrompt.downloadUrl = jsonExtractStringField(payload, "url");
    g_luaPrompt.code = jsonExtractStringField(payload, "code");
    if (!g_luaPrompt.code.length() && !g_luaPrompt.downloadUrl.length()) return false;
    if (g_luaPrompt.sizeBytes <= 0) g_luaPrompt.sizeBytes = g_luaPrompt.code.length();

    g_luaPrompt.active = true;
    g_screen = SCREEN_LUA_PROMPT;
    setLog("Lua push received");
    return true;
}

bool handleLuaRequestJsonFile(const char* payloadPath) {
    g_luaDisplayActive = false;
    g_forceFullRefresh = true;

    g_luaPrompt.requestId = "";
    g_luaPrompt.scriptId = "";
    g_luaPrompt.title = "";
    g_luaPrompt.fileName = "";
    g_luaPrompt.description = "";
    g_luaPrompt.authorUsername = "";
    g_luaPrompt.downloadUrl = "";
    g_luaPrompt.code = "";
    g_luaPrompt.codePath = "";
    File sizeFile = SPIFFS.open(payloadPath, FILE_READ);
    g_luaPrompt.sizeBytes = jsonExtractIntFieldFromFile(sizeFile, "sizeBytes", 0);
    sizeFile.close();

    File f;
    f = SPIFFS.open(payloadPath, FILE_READ); g_luaPrompt.requestId = jsonExtractStringFieldFromFile(f, "requestId"); f.close();
    f = SPIFFS.open(payloadPath, FILE_READ); g_luaPrompt.scriptId = jsonExtractStringFieldFromFile(f, "scriptId"); f.close();
    f = SPIFFS.open(payloadPath, FILE_READ); g_luaPrompt.title = jsonExtractStringFieldFromFile(f, "title"); f.close();
    f = SPIFFS.open(payloadPath, FILE_READ); g_luaPrompt.fileName = jsonExtractStringFieldFromFile(f, "fileName"); f.close();
    f = SPIFFS.open(payloadPath, FILE_READ); g_luaPrompt.description = jsonExtractStringFieldFromFile(f, "description"); f.close();
    f = SPIFFS.open(payloadPath, FILE_READ); g_luaPrompt.authorUsername = jsonExtractStringFieldFromFile(f, "authorUsername"); f.close();
    f = SPIFFS.open(payloadPath, FILE_READ); g_luaPrompt.downloadUrl = jsonExtractStringFieldFromFile(f, "downloadUrl"); f.close();
    if (!g_luaPrompt.downloadUrl.length()) {
        f = SPIFFS.open(payloadPath, FILE_READ); g_luaPrompt.downloadUrl = jsonExtractStringFieldFromFile(f, "url"); f.close();
    }

    f = SPIFFS.open(payloadPath, FILE_READ);
    File outFile = SPIFFS.open(LUA_PUSH_TEMP_PATH, FILE_WRITE);
    bool hasCode = false;
    if (f && outFile) hasCode = jsonExtractStringFieldToFile(f, "code", outFile);
    if (f) f.close();
    if (outFile) outFile.close();

    if (!hasCode && !g_luaPrompt.downloadUrl.length()) return false;
    if (g_luaPrompt.sizeBytes <= 0 && hasCode) {
        File check = SPIFFS.open(LUA_PUSH_TEMP_PATH, FILE_READ);
        if (check) { g_luaPrompt.sizeBytes = check.size(); check.close(); }
    }
    g_luaPrompt.codePath = hasCode ? LUA_PUSH_TEMP_PATH : "";
    g_luaPrompt.active = true;
    g_screen = SCREEN_LUA_PROMPT;
    setLog("Lua push received");
    return true;
}

void handleMqttPayload(const String& incomingTopic, const String& payload) {
    bool isControlTopic = incomingTopic.endsWith("/handshake/accepted") ||
        incomingTopic.endsWith("/link/request") ||
        incomingTopic.endsWith("/transaction/request") ||
        incomingTopic.endsWith("/lua/request");
    if (!isControlTopic) return;

    if (incomingTopic.endsWith("/lua/request")) {
        if (!handleLuaRequestJson(payload)) setLog("Bad MQTT JSON");
        return;
    }

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
    }

    cJSON_Delete(root);
}

bool mqttTopicMatches(const char* filter, const char* t) {
    if (*filter == '#') return true;
    if (*filter == '+') {
        const char* f = filter + 1;
        while (*t && *t != '/') t++;
        if (*f == '\0') return *t == '\0';
        if (*f != '/' || *t != '/') return false;
        return mqttTopicMatches(f + 1, t + 1);
    }
    if (*t == '\0') return *filter == '\0';
    if (*filter != *t) return false;
    return mqttTopicMatches(filter + 1, t + 1);
}

void luaMqttResetSubs() {
    char subs[ONION_LUA_MQTT_MAX_SUBS][ONION_LUA_MQTT_MAX_TOPIC + 1];
    uint8_t count;
    portENTER_CRITICAL(&g_luaMqttMux);
    count = g_luaMqttSubCount;
    for (uint8_t i = 0; i < count; i++) strcpy(subs[i], g_luaMqttSubs[i]);
    g_luaMqttSubCount = 0;
    g_luaMqttQueueHead = 0;
    g_luaMqttQueueCount = 0;
    portEXIT_CRITICAL(&g_luaMqttMux);

    if (g_mqtt && g_mqttConnected) {
        for (uint8_t i = 0; i < count; i++) esp_mqtt_client_unsubscribe(g_mqtt, subs[i]);
    }
}

bool luaMqttQueuePop(MqttQueuedMessage& out) {
    bool hasMessage = false;
    portENTER_CRITICAL(&g_luaMqttMux);
    if (g_luaMqttQueueCount > 0) {
        out = g_luaMqttQueue[g_luaMqttQueueHead];
        g_luaMqttQueueHead = (g_luaMqttQueueHead + 1) % ONION_LUA_MQTT_QUEUE_LEN;
        g_luaMqttQueueCount--;
        hasMessage = true;
    }
    portEXIT_CRITICAL(&g_luaMqttMux);
    return hasMessage;
}

void luaMqttMaybeQueue(const char* topicStr, uint16_t topicLen, const char* payload, uint16_t payloadLen) {
    if (topicLen > ONION_LUA_MQTT_MAX_TOPIC) return;
    if (payloadLen > ONION_LUA_MQTT_MAX_PAYLOAD) payloadLen = ONION_LUA_MQTT_MAX_PAYLOAD;

    char topicCopy[ONION_LUA_MQTT_MAX_TOPIC + 1];
    memcpy(topicCopy, topicStr, topicLen);
    topicCopy[topicLen] = '\0';

    portENTER_CRITICAL(&g_luaMqttMux);
    bool matched = false;
    for (uint8_t i = 0; i < g_luaMqttSubCount; i++) {
        if (mqttTopicMatches(g_luaMqttSubs[i], topicCopy)) { matched = true; break; }
    }
    if (matched) {
        uint8_t slot = (g_luaMqttQueueHead + g_luaMqttQueueCount) % ONION_LUA_MQTT_QUEUE_LEN;
        if (g_luaMqttQueueCount == ONION_LUA_MQTT_QUEUE_LEN) {
            slot = g_luaMqttQueueHead;
            g_luaMqttQueueHead = (g_luaMqttQueueHead + 1) % ONION_LUA_MQTT_QUEUE_LEN;
        } else {
            g_luaMqttQueueCount++;
        }
        MqttQueuedMessage& msg = g_luaMqttQueue[slot];
        memcpy(msg.topic, topicCopy, topicLen);
        msg.topic[topicLen] = '\0';
        msg.topicLen = topicLen;
        memcpy(msg.payload, payload, payloadLen);
        msg.payload[payloadLen] = '\0';
        msg.payloadLen = payloadLen;
        msg.receivedAt = millis();
    }
    portEXIT_CRITICAL(&g_luaMqttMux);
}

void processMqttMessage(const String& incomingTopic, const String& payload) {
    uint16_t topicLen = incomingTopic.length() > 65535 ? 65535 : (uint16_t)incomingTopic.length();
    uint16_t payloadLen = payload.length() > 65535 ? 65535 : (uint16_t)payload.length();
    luaMqttMaybeQueue(incomingTopic.c_str(), topicLen, payload.c_str(), payloadLen);
    handleMqttPayload(incomingTopic, payload);
}

void processMqttMessageFile(const String& incomingTopic, const char* payloadPath) {
    if (incomingTopic.endsWith("/lua/request")) {
        if (!handleLuaRequestJsonFile(payloadPath)) setLog("Bad MQTT JSON");
        return;
    }

    File file = SPIFFS.open(payloadPath, FILE_READ);
    if (!file) {
        setLog("Bad MQTT JSON");
        return;
    }
    String payload;
    if (!payload.reserve(file.size() + 1)) {
        file.close();
        setLog("MQTT alloc failed");
        return;
    }
    while (file.available()) payload += (char)file.read();
    file.close();
    processMqttMessage(incomingTopic, payload);
}

void resetMqttFragmentBuffer() {
    g_mqttRxTopic = "";
    if (g_mqttRxFile) g_mqttRxFile.close();
    if (g_mqttRxPresent) {
        free(g_mqttRxPresent);
        g_mqttRxPresent = nullptr;
    }
    SPIFFS.remove(MQTT_RX_TEMP_JSON_PATH);
    g_mqttRxExpectedLen = 0;
    g_mqttRxReceivedLen = 0;
    g_mqttRxPresentBytes = 0;
    g_mqttRxMsgId = 0;
}

static bool mqttFragmentBytePresent(int idx) {
    return (g_mqttRxPresent[(size_t)idx >> 3] & (1 << (idx & 7))) != 0;
}

static void markMqttFragmentBytePresent(int idx) {
    g_mqttRxPresent[(size_t)idx >> 3] |= (1 << (idx & 7));
}

bool beginMqttFragmentBuffer(const String& incomingTopic, int totalLen, int msgId) {
    resetMqttFragmentBuffer();
    g_mqttRxTopic = incomingTopic;
    g_mqttRxExpectedLen = totalLen;
    g_mqttRxReceivedLen = 0;
    g_mqttRxMsgId = msgId;

    g_mqttRxPresentBytes = ((size_t)totalLen + 7) / 8;
    g_mqttRxPresent = (uint8_t*)heap_caps_calloc(g_mqttRxPresentBytes, 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!g_mqttRxPresent) g_mqttRxPresent = (uint8_t*)heap_caps_calloc(g_mqttRxPresentBytes, 1, MALLOC_CAP_8BIT);
    SPIFFS.remove(MQTT_RX_TEMP_JSON_PATH);
    g_mqttRxFile = SPIFFS.open(MQTT_RX_TEMP_JSON_PATH, FILE_WRITE);

    if (!g_mqttRxFile || !g_mqttRxPresent) {
        resetMqttFragmentBuffer();
        setLog("MQTT alloc failed");
        return false;
    }
    return true;
}

bool ensureMqttRxFileSize(size_t size) {
    if (!g_mqttRxFile) return false;
    size_t current = g_mqttRxFile.size();
    if (current >= size) return true;
    if (!g_mqttRxFile.seek(current, SeekSet)) return false;

    uint8_t zeros[64] = {};
    while (current < size) {
        size_t n = size - current;
        if (n > sizeof(zeros)) n = sizeof(zeros);
        if (g_mqttRxFile.write(zeros, n) != n) return false;
        current += n;
    }
    return true;
}

void processMqttEventData(esp_mqtt_event_handle_t event) {
    String incomingTopic = (event->topic && event->topic_len > 0)
        ? String(event->topic, event->topic_len)
        : String();
    String payloadChunk = (event->data && event->data_len > 0)
        ? String(event->data, event->data_len)
        : String();

    int totalLen = event->total_data_len > 0 ? event->total_data_len : event->data_len;
    int offset = event->current_data_offset;
    if (offset < 0 || event->data_len < 0 || totalLen < event->data_len) {
        resetMqttFragmentBuffer();
        setLog("Bad MQTT fragment");
        return;
    }
    if (totalLen > MQTT_RX_MAX_BYTES) {
        resetMqttFragmentBuffer();
        setLog("MQTT payload too large");
        return;
    }

    bool fragmented = totalLen > event->data_len || offset > 0;
    if (!fragmented) {
        processMqttMessage(incomingTopic, payloadChunk);
        return;
    }

    if (offset == 0) {
        if (!beginMqttFragmentBuffer(incomingTopic, totalLen, event->msg_id)) return;
    } else if (!g_mqttRxExpectedLen || (g_mqttRxMsgId && event->msg_id && event->msg_id != g_mqttRxMsgId)) {
        resetMqttFragmentBuffer();
        setLog("Bad MQTT fragment");
        return;
    } else if (incomingTopic.length() && g_mqttRxTopic.length() && incomingTopic != g_mqttRxTopic) {
        resetMqttFragmentBuffer();
        setLog("Bad MQTT fragment");
        return;
    }

    if (offset + event->data_len > g_mqttRxExpectedLen) {
        resetMqttFragmentBuffer();
        setLog("Bad MQTT fragment");
        return;
    }

    if (!ensureMqttRxFileSize((size_t)offset) ||
        !g_mqttRxFile.seek(offset, SeekSet) ||
        g_mqttRxFile.write((const uint8_t*)event->data, event->data_len) != (size_t)event->data_len) {
        resetMqttFragmentBuffer();
        setLog("MQTT write failed");
        return;
    }
    for (int i = 0; i < event->data_len; i++) {
        int idx = offset + i;
        if (!mqttFragmentBytePresent(idx)) {
            markMqttFragmentBytePresent(idx);
            g_mqttRxReceivedLen++;
        }
    }

    if (g_mqttRxReceivedLen < g_mqttRxExpectedLen) return;

    if (g_mqttRxFile) g_mqttRxFile.close();
    processMqttMessageFile(g_mqttRxTopic, MQTT_RX_TEMP_JSON_PATH);
    resetMqttFragmentBuffer();
}

void mqttEventHandler(void*, esp_event_base_t, int32_t eventId, void* eventData) {
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
        processMqttEventData(event);
        break;
    }
    default:
        break;
    }
}

void ensureMqtt() {
    if (g_mqttConnected || !g_config.mqttUri.length() || WiFi.status() != WL_CONNECTED) return;
    if (millis() - g_lastMqttAttempt < MQTT_RECONNECT_INTERVAL_MS) return;
    g_lastMqttAttempt = millis();

    if (!g_mqtt) {
        esp_mqtt_client_config_t cfg = {};
        cfg.broker.address.uri = g_config.mqttUri.c_str();
        cfg.credentials.username = g_config.mqttUsername.length() ? g_config.mqttUsername.c_str() : nullptr;
        cfg.credentials.authentication.password = g_config.mqttPassword.length() ? g_config.mqttPassword.c_str() : nullptr;
        cfg.buffer.size = MQTT_CLIENT_BUFFER_BYTES;
        cfg.buffer.out_size = MQTT_CLIENT_OUT_BUFFER_BYTES;
        g_mqtt = esp_mqtt_client_init(&cfg);
        esp_mqtt_client_register_event(g_mqtt, MQTT_EVENT_ANY, mqttEventHandler, nullptr);
        esp_mqtt_client_start(g_mqtt);
    } else {
        esp_mqtt_client_reconnect(g_mqtt);
    }
}

bool sendLinkResponse(bool approved) {
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

bool signSolanaTransaction(const String& transactionBase64, String& signedTransaction, String& error) {
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

bool sendTransactionResponse(bool approved) {
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

bool refreshPublicProfile(bool quiet) {
    if (!g_identity.onionId && !g_identity.username.length()) {
        if (!quiet) setLog("No profile identity");
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
        if (!quiet) setLog("Profile GET failed " + String(code));
        return false;
    }

    cJSON* root = cJSON_Parse(response.c_str());
    if (!root) {
        if (!quiet) setLog("Bad profile JSON");
        return false;
    }
    String beforeCount = g_identity.onionCount;
    updateProfileFromJson(root);
    String wallet = jsonString(root, "solanaWalletAddress");
    if (wallet.length() && wallet != g_identity.solanaPublicKey) {
        g_identity.solanaPublicKey = wallet;
        g_prefs.putString("sol_pub", g_identity.solanaPublicKey);
    }
    cJSON_Delete(root);
    if (quiet) {
        if (g_identity.onionCount != beforeCount && !g_luaDisplayActive) g_needsRedraw = true;
    } else {
        setLog("Profile refreshed");
    }
    return true;
}

bool downloadFile(const String& url, const String& path, size_t maxBytes, const String& label) {
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

bool downloadScriptFile(const String& url, const String& path) {
    return downloadFile(url, path, MAX_SCRIPT_BYTES, "Script");
}

bool downloadImageFile(const String& url, const String& path) {
    return downloadFile(url, path, MAX_IMAGE_BYTES, "Image");
}

String resolveServerUrl(const String& url) {
    if (url.startsWith("http://") || url.startsWith("https://")) return url;
    String base = g_config.serverBaseUrl;
    if (base.endsWith("/")) base.remove(base.length() - 1);
    if (url.startsWith("/")) return base + url;
    return base + "/" + url;
}
