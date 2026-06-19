#include "espnow.h"
#include "badge_state.h"
#include "display.h"
#include "mqtt.h"
#include <WiFi.h>
#include <esp_wifi.h>

String macToString(const uint8_t mac[6]) {
    char buf[18];
    snprintf(buf, sizeof(buf), "%02x:%02x:%02x:%02x:%02x:%02x",
        mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return String(buf);
}

bool parseMacString(const char* value, uint8_t mac[6]) {
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

bool espnowQueuePop(EspNowQueuedMessage& out) {
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

static void copyBounded(char* dst, size_t dstLen, const char* src, size_t srcLen) {
    if (!dst || dstLen == 0) return;
    size_t n = 0;
    if (src && srcLen) {
        while (n + 1 < dstLen && n < srcLen && src[n] != '\0') {
            dst[n] = src[n];
            n++;
        }
    }
    dst[n] = '\0';
}

static void copyStringToField(char* dst, size_t dstLen, const String& value) {
    if (!dst || dstLen == 0) return;
    size_t n = value.length();
    if (n >= dstLen) n = dstLen - 1;
    memcpy(dst, value.c_str(), n);
    dst[n] = '\0';
    if (n + 1 < dstLen) memset(dst + n + 1, 0, dstLen - n - 1);
}

bool checkInHeaderMatches(const uint8_t* data, int len, uint8_t type) {
    if (!data || len < (int)sizeof(CheckInPacketHeader)) return false;
    const CheckInPacketHeader* header = reinterpret_cast<const CheckInPacketHeader*>(data);
    return memcmp(header->magic, kCheckInMagic, sizeof(kCheckInMagic)) == 0 &&
        header->version == kCheckInVersion && header->type == type;
}

void checkInMaybeCapturePacket(const esp_now_recv_info_t* info, const uint8_t* data, int len, int8_t rssi) {
    if (!info || !info->src_addr || !data) return;

    if (len == (int)sizeof(CheckInAdvertisePacket) &&
        checkInHeaderMatches(data, len, CHECKIN_PACKET_ADVERTISE)) {
        CheckInAdvertisePacket packet;
        memcpy(&packet, data, sizeof(packet));
        int8_t minRssi = packet.minRssi ? packet.minRssi : ONION_CHECKIN_DEFAULT_MIN_RSSI;
        if (rssi < minRssi) return;

        portENTER_CRITICAL(&g_checkinMux);
        memcpy(g_checkinPendingOffer.beaconMac, info->src_addr, 6);
        copyBounded(g_checkinPendingOffer.beaconId, sizeof(g_checkinPendingOffer.beaconId),
            packet.beaconId, sizeof(packet.beaconId));
        copyBounded(g_checkinPendingOffer.room, sizeof(g_checkinPendingOffer.room),
            packet.room, sizeof(packet.room));
        copyBounded(g_checkinPendingOffer.label, sizeof(g_checkinPendingOffer.label),
            packet.label, sizeof(packet.label));
        memcpy(g_checkinPendingOffer.nonce, packet.nonce, sizeof(packet.nonce));
        g_checkinPendingOffer.rssi = rssi;
        g_checkinPendingOffer.minRssi = minRssi;
        g_checkinPendingOffer.seenAt = millis();
        g_checkinOfferPending = true;
        portEXIT_CRITICAL(&g_checkinMux);
    } else if (len == (int)sizeof(CheckInResultPacket) &&
        checkInHeaderMatches(data, len, CHECKIN_PACKET_RESULT)) {
        CheckInResultPacket packet;
        memcpy(&packet, data, sizeof(packet));
        portENTER_CRITICAL(&g_checkinMux);
        copyBounded(g_checkinPendingResult.beaconId, sizeof(g_checkinPendingResult.beaconId),
            packet.beaconId, sizeof(packet.beaconId));
        memcpy(g_checkinPendingResult.nonce, packet.nonce, sizeof(packet.nonce));
        g_checkinPendingResult.awarded = packet.awarded != 0;
        g_checkinPendingResult.points = packet.points;
        copyBounded(g_checkinPendingResult.message, sizeof(g_checkinPendingResult.message),
            packet.message, sizeof(packet.message));
        g_checkinResultPending = true;
        portEXIT_CRITICAL(&g_checkinMux);
    }
}

void espnowQueuePush(const uint8_t mac[6], const uint8_t* data, int len, int8_t rssi) {
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

void onEspNowReceive(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
    int8_t rssi = 0;
    if (info && info->rx_ctrl) rssi = info->rx_ctrl->rssi;
    checkInMaybeCapturePacket(info, data, len, rssi);
    if (info && info->src_addr) espnowQueuePush(info->src_addr, data, len, rssi);
}

void onEspNowSend(const esp_now_send_info_t*, esp_now_send_status_t status) {
    if (status == ESP_NOW_SEND_SUCCESS) g_espnowSent++;
}

bool espnowAddPeer(const uint8_t mac[6], uint8_t channel, String& error) {
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

bool ensureEspNow(int requestedChannel, String& error) {
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
        static const uint8_t kEspNowPmk[ESP_NOW_KEY_LEN] = {
            'o', 'n', 'i', 'o', 'n', '-', 'o', 's', '-', 'p', 'm', 'k', '-', '0', '0', '1'
        };
        esp_err_t pmkRc = esp_now_set_pmk(kEspNowPmk);
        if (pmkRc != ESP_OK) {
            Serial.printf("[onion-os] ESP-NOW set_pmk failed %d\n", (int)pmkRc);
        }
        g_espnowStarted = true;
        Serial.printf("[onion-os] ESP-NOW started queue=%d free_internal=%u\n",
                      ONION_ESPNOW_QUEUE_LEN,
                      (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    }

    return espnowAddPeer(kEspNowBroadcastMac, 0, error);
}

bool screenAllowsCheckInPrompt() {
    return g_screen == SCREEN_STATUS && !g_luaDisplayActive &&
        !g_linkPrompt.active && !g_txPrompt.active && !g_luaPrompt.active;
}

void showCheckInResult(const String& message, bool awarded, int points) {
    g_checkinResult.message = message;
    g_checkinResult.awarded = awarded;
    g_checkinResult.points = points;
    g_checkinResult.shownAt = millis();
    g_screen = SCREEN_CHECKIN_RESULT;
    g_forceFullRefresh = true;
    g_needsRedraw = true;
}

void sendCheckInApproval(bool approve) {
    g_checkinPrompt.active = false;
    g_lastCheckInBeaconId = g_checkinPrompt.beaconId;
    g_lastCheckInPromptAt = millis();

    if (!approve) {
        setLog("Check-in skipped");
        g_screen = SCREEN_STATUS;
        return;
    }

    CheckInApprovePacket packet = {};
    memcpy(packet.header.magic, kCheckInMagic, sizeof(kCheckInMagic));
    packet.header.version = kCheckInVersion;
    packet.header.type = CHECKIN_PACKET_APPROVE;
    copyStringToField(packet.beaconId, sizeof(packet.beaconId), g_checkinPrompt.beaconId);
    memcpy(packet.nonce, g_checkinPrompt.nonce, sizeof(packet.nonce));
    copyStringToField(packet.hardwareId, sizeof(packet.hardwareId), g_identity.hardwareId);
    packet.onionId = g_identity.onionId;
    copyStringToField(packet.username, sizeof(packet.username), g_identity.username);
    copyStringToField(packet.wallet, sizeof(packet.wallet), g_identity.solanaPublicKey);
    packet.rssi = g_checkinPrompt.rssi;
    packet.approvedAt = millis();
    esp_wifi_get_mac(WIFI_IF_STA, packet.badgeMac);

    String error;
    if (!ensureEspNow(0, error) || !espnowAddPeer(g_checkinPrompt.beaconMac, 0, error)) {
        showCheckInResult("Radio failed: " + clipped(error, 15));
        return;
    }

    esp_err_t rc = esp_now_send(g_checkinPrompt.beaconMac,
        reinterpret_cast<const uint8_t*>(&packet), sizeof(packet));
    if (rc != ESP_OK) {
        showCheckInResult("Send failed " + String((int)rc));
        return;
    }

    setLog("Check-in sent");
    showCheckInResult("Waiting for server...");
}

void processCheckInService() {
    uint32_t now = millis();
    if (WiFi.status() == WL_CONNECTED &&
        g_wifiWorkerResult.load() != WIFI_WORKER_RUNNING &&
        now - g_lastCheckInScan >= ONION_CHECKIN_SCAN_INTERVAL_MS) {
        g_lastCheckInScan = now;
        String error;
        if (!ensureEspNow(0, error)) {
            Serial.printf("[onion-os] check-in ESP-NOW unavailable: %s\n", error.c_str());
        }
    }

    CheckInPendingResult pendingResult;
    bool hasResult = false;
    portENTER_CRITICAL(&g_checkinMux);
    if (g_checkinResultPending) {
        pendingResult = g_checkinPendingResult;
        g_checkinResultPending = false;
        hasResult = true;
    }
    portEXIT_CRITICAL(&g_checkinMux);
    if (hasResult) {
        String msg = String(pendingResult.message);
        if (!msg.length()) msg = pendingResult.awarded ? "Attendance recorded" : "No active workshop";
        showCheckInResult(msg, pendingResult.awarded, pendingResult.points);
        if (pendingResult.awarded) refreshPublicProfile(true);
    }

    if (g_screen == SCREEN_CHECKIN_RESULT &&
        g_checkinResult.shownAt &&
        now - g_checkinResult.shownAt > ONION_CHECKIN_RESULT_MS) {
        g_screen = SCREEN_STATUS;
        g_needsRedraw = true;
    }

    CheckInPendingOffer offer;
    bool hasOffer = false;
    portENTER_CRITICAL(&g_checkinMux);
    if (g_checkinOfferPending) {
        offer = g_checkinPendingOffer;
        g_checkinOfferPending = false;
        hasOffer = true;
    }
    portEXIT_CRITICAL(&g_checkinMux);
    if (!hasOffer || !screenAllowsCheckInPrompt()) return;

    String beaconId = String(offer.beaconId);
    if (!beaconId.length()) beaconId = macToString(offer.beaconMac);
    if (beaconId == g_lastCheckInBeaconId &&
        now - g_lastCheckInPromptAt < ONION_CHECKIN_PROMPT_COOLDOWN_MS) {
        return;
    }

    g_checkinPrompt.beaconId = beaconId;
    g_checkinPrompt.room = String(offer.room);
    g_checkinPrompt.label = String(offer.label);
    memcpy(g_checkinPrompt.beaconMac, offer.beaconMac, sizeof(g_checkinPrompt.beaconMac));
    memcpy(g_checkinPrompt.nonce, offer.nonce, sizeof(g_checkinPrompt.nonce));
    g_checkinPrompt.rssi = offer.rssi;
    g_checkinPrompt.minRssi = offer.minRssi;
    g_checkinPrompt.active = true;
    g_screen = SCREEN_CHECKIN_PROMPT;
    g_forceFullRefresh = true;
    setLog("Check-in beacon nearby");
}
