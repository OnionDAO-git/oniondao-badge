#pragma once

#include <Arduino.h>

void restoreWifiProtocol();
bool ensureWifi();
void triggerWifiReconnect();
void startWifiScan();
void startWifiConnect(const char* ssid, const char* pass);
