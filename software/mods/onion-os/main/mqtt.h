#pragma once

#include <Arduino.h>
#include <cJSON.h>
#include <mqtt_client.h>
#include "badge_types.h"

String topic(const String& suffix);
uint64_t handshakeOnionIdFromTopic(const String& incomingTopic);
void subscribeBadgeTopics();
bool mqttHandshakeResponseMatchesBadge(cJSON* root, const String& incomingTopic, uint64_t onionId);
bool handleHandshakeResponse(const String& response, const String& incomingTopic = String());
void doHttpHandshake();
bool publishMqtt(const String& mqttTopic, const String& payload);
void doMqttHandshake();
void handleLinkRequest(cJSON* root);
void handleTransactionRequest(cJSON* root);
bool handleLuaRequestJson(const String& payload);
bool handleLuaRequestJsonFile(const char* payloadPath);
void handleMqttPayload(const String& incomingTopic, const String& payload);
bool mqttTopicMatches(const char* filter, const char* topic);
void luaMqttResetSubs();
bool luaMqttQueuePop(MqttQueuedMessage& out);
void luaMqttMaybeQueue(const char* topic, uint16_t topicLen, const char* payload, uint16_t payloadLen);
void processMqttMessage(const String& incomingTopic, const String& payload);
void processMqttMessageFile(const String& incomingTopic, const char* payloadPath);
void resetMqttFragmentBuffer();
bool beginMqttFragmentBuffer(const String& incomingTopic, int totalLen, int msgId);
bool ensureMqttRxFileSize(size_t size);
void processMqttEventData(esp_mqtt_event_handle_t event);
void mqttEventHandler(void*, esp_event_base_t, int32_t eventId, void* eventData);
void ensureMqtt();

// Link/transaction responses
bool sendLinkResponse(bool approved);
bool signSolanaTransaction(const String& transactionBase64, String& signedTransaction, String& error);
bool sendTransactionResponse(bool approved);
bool refreshPublicProfile(bool quiet = false);

// File download
bool downloadFile(const String& url, const String& path, size_t maxBytes, const String& label);
bool downloadScriptFile(const String& url, const String& path);
bool downloadImageFile(const String& url, const String& path);
String resolveServerUrl(const String& url);
