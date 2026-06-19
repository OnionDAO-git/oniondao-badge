#pragma once

#include <Arduino.h>
#include <vector>

void restartSharedI2cBus();
bool ateccHmac(const std::vector<uint8_t>& message, uint8_t digest[32], String* serialHex, String& error);
bool ateccRandom(uint8_t* out, size_t count, String& error);
