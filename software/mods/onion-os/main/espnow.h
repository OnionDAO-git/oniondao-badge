#pragma once

#include <Arduino.h>
#include <esp_now.h>
#include "badge_types.h"

String macToString(const uint8_t mac[6]);
bool parseMacString(const char* value, uint8_t mac[6]);
bool espnowQueuePop(EspNowQueuedMessage& out);
void espnowQueuePush(const uint8_t mac[6], const uint8_t* data, int len, int8_t rssi);
void onEspNowReceive(const esp_now_recv_info_t* info, const uint8_t* data, int len);
void onEspNowSend(const esp_now_send_info_t*, esp_now_send_status_t status);
bool espnowAddPeer(const uint8_t mac[6], uint8_t channel, String& error);
bool ensureEspNow(int requestedChannel, String& error);

// Check-in
bool checkInHeaderMatches(const uint8_t* data, int len, uint8_t type);
void checkInMaybeCapturePacket(const esp_now_recv_info_t* info, const uint8_t* data, int len, int8_t rssi);
bool screenAllowsCheckInPrompt();
void showCheckInResult(const String& message, bool awarded = false, int points = 0);
void sendCheckInApproval(bool approve);
void processCheckInService();
