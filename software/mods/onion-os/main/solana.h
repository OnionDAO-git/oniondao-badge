#pragma once

#include <Arduino.h>

std::vector<uint8_t> stringBytes(const String& value);
bool deriveWrappingKey(uint8_t key[32], String& error);
bool createAteccAttestation(const String& purpose, const String& subject, String& json, String& error);
bool decryptSolanaSeed(uint8_t seed[32], String& error);
bool storeSolanaSeed(const uint8_t seed[32], const uint8_t pubkey[32], String& error);
bool loadOrCreateSolanaKey(bool rotate, String& error);
void clearSolanaKey();
