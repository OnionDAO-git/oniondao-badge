#pragma once

#include <Arduino.h>

extern "C" {
#include "lua.h"
}

// CC1101 register/strobe defines
#define CC1101_SRES     0x30
#define CC1101_SRX      0x34
#define CC1101_STX      0x35
#define CC1101_SIDLE    0x36
#define CC1101_SPWD     0x39
#define CC1101_SFRX     0x3A
#define CC1101_SFTX     0x3B
#define CC1101_PARTNUM  0x30
#define CC1101_VERSION  0x31
#define CC1101_MARCSTATE 0x35
#define CC1101_RXBYTES  0x3B
#define CC1101_PATABLE  0x3E
#define CC1101_FIFO     0x3F

struct ModuleVariantPins {
    const char* name;
    int line[5];
};

const ModuleVariantPins& resolveModuleVariant();
void modulePowerOn(int powerPin);
void modulePowerOff();
int luaModulePin(lua_State* L, int idx, const char* key, int fallback);

void subghzBegin();
bool subghzBegin(double freq, const char* mod, int sck, int miso, int mosi,
                 int cs, int gdo0, int powerPin, String& error);
void subghzEnd();
bool subghzTransmit(const uint8_t* data, size_t len, String& error);
bool subghzReceive(uint8_t* buf, size_t bufLen, size_t& received, uint32_t timeoutMs, String& error);
bool subghzSetFrequency(uint32_t hz, String& error);

// Internal CC1101 functions exposed for Lua bindings
uint8_t cc1101Strobe(uint8_t cmd);
void cc1101WriteReg(uint8_t addr, uint8_t value);
uint8_t cc1101ReadReg(uint8_t addr);
uint8_t cc1101ReadStatus(uint8_t addr);
void cc1101WriteBurst(uint8_t addr, const uint8_t* data, size_t len);
void cc1101ReadBurst(uint8_t addr, uint8_t* data, size_t len);
bool cc1101Transmit(const uint8_t* data, size_t len);
int cc1101Receive(uint8_t* out, size_t maxLen, int* rssiRaw, uint32_t timeoutMs);
void cc1101SetFrequency(double mhz);
