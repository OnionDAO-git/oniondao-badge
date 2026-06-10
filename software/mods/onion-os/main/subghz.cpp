#include "subghz.h"
#include "badge_state.h"
#include <SPI.h>

static const ModuleVariantPins kModuleVariants[] = {
    {"L1", {48, 47, 19, 42, 41}},
    {"L2", {40, 41, 42, 19, 47}},
    {"R",  {38, 39, 16, 15, 7}},
};

static SPIClass g_subghzSpi(HSPI);
static int g_subghzCs = -1;
static int g_subghzMiso = -1;
static int g_subghzGdo0 = -1;

const ModuleVariantPins& resolveModuleVariant() {
    for (const ModuleVariantPins& v : kModuleVariants) {
        if (g_config.moduleVariant.equalsIgnoreCase(v.name)) return v;
    }
    return kModuleVariants[0];
}

void modulePowerOn(int powerPin) {
    g_modulePowerPin = powerPin;
    if (powerPin >= 0) {
        pinMode(powerPin, OUTPUT);
        digitalWrite(powerPin, HIGH);
        delay(10);
    }
}

void modulePowerOff() {
    if (g_modulePowerPin >= 0) {
        digitalWrite(g_modulePowerPin, LOW);
        g_modulePowerPin = -1;
    }
}

int luaModulePin(lua_State* L, int idx, const char* key, int fallback) {
    if (idx) {
        lua_getfield(L, idx, key);
        if (lua_isnumber(L, -1)) {
            int v = (int)lua_tointeger(L, -1);
            lua_pop(L, 1);
            return v;
        }
        lua_pop(L, 1);
    }
    return fallback;
}

static inline void cc1101Select() {
    digitalWrite(g_subghzCs, LOW);
    if (g_subghzMiso >= 0) {
        uint32_t start = micros();
        while (digitalRead(g_subghzMiso) && (micros() - start) < 5000) {}
    }
}

static inline void cc1101Deselect() {
    digitalWrite(g_subghzCs, HIGH);
}

uint8_t cc1101Strobe(uint8_t cmd) {
    g_subghzSpi.beginTransaction(SPISettings(CC1101_SPI_HZ, MSBFIRST, SPI_MODE0));
    cc1101Select();
    uint8_t status = g_subghzSpi.transfer(cmd);
    cc1101Deselect();
    g_subghzSpi.endTransaction();
    return status;
}

void cc1101WriteReg(uint8_t addr, uint8_t value) {
    g_subghzSpi.beginTransaction(SPISettings(CC1101_SPI_HZ, MSBFIRST, SPI_MODE0));
    cc1101Select();
    g_subghzSpi.transfer(addr & 0x3F);
    g_subghzSpi.transfer(value);
    cc1101Deselect();
    g_subghzSpi.endTransaction();
}

uint8_t cc1101ReadReg(uint8_t addr) {
    g_subghzSpi.beginTransaction(SPISettings(CC1101_SPI_HZ, MSBFIRST, SPI_MODE0));
    cc1101Select();
    g_subghzSpi.transfer((addr & 0x3F) | 0x80);
    uint8_t value = g_subghzSpi.transfer(0x00);
    cc1101Deselect();
    g_subghzSpi.endTransaction();
    return value;
}

uint8_t cc1101ReadStatus(uint8_t addr) {
    g_subghzSpi.beginTransaction(SPISettings(CC1101_SPI_HZ, MSBFIRST, SPI_MODE0));
    cc1101Select();
    g_subghzSpi.transfer((addr & 0x3F) | 0xC0);
    uint8_t value = g_subghzSpi.transfer(0x00);
    cc1101Deselect();
    g_subghzSpi.endTransaction();
    return value;
}

void cc1101WriteBurst(uint8_t addr, const uint8_t* data, size_t len) {
    g_subghzSpi.beginTransaction(SPISettings(CC1101_SPI_HZ, MSBFIRST, SPI_MODE0));
    cc1101Select();
    g_subghzSpi.transfer((addr & 0x3F) | 0x40);
    for (size_t i = 0; i < len; i++) g_subghzSpi.transfer(data[i]);
    cc1101Deselect();
    g_subghzSpi.endTransaction();
}

void cc1101ReadBurst(uint8_t addr, uint8_t* data, size_t len) {
    g_subghzSpi.beginTransaction(SPISettings(CC1101_SPI_HZ, MSBFIRST, SPI_MODE0));
    cc1101Select();
    g_subghzSpi.transfer((addr & 0x3F) | 0xC0);
    for (size_t i = 0; i < len; i++) data[i] = g_subghzSpi.transfer(0x00);
    cc1101Deselect();
    g_subghzSpi.endTransaction();
}

static void cc1101Reset() {
    cc1101Deselect();
    delayMicroseconds(5);
    digitalWrite(g_subghzCs, LOW);
    delayMicroseconds(10);
    cc1101Deselect();
    delayMicroseconds(45);
    cc1101Strobe(CC1101_SRES);
    delay(1);
}

static void cc1101ApplyBaseConfig() {
    static const uint8_t kBase[][2] = {
        {0x02, 0x06}, {0x03, 0x47}, {0x04, 0xD3}, {0x05, 0x91}, {0x06, 0x3D},
        {0x07, 0x04}, {0x08, 0x05}, {0x09, 0x00}, {0x0A, 0x00}, {0x0B, 0x06},
        {0x0C, 0x00}, {0x10, 0xF6}, {0x11, 0x83}, {0x13, 0x22}, {0x14, 0xF8},
        {0x16, 0x07}, {0x17, 0x30}, {0x18, 0x18}, {0x19, 0x16}, {0x1A, 0x6C},
        {0x1B, 0x03}, {0x1C, 0x40}, {0x1D, 0x91}, {0x20, 0xFB}, {0x21, 0x56},
        {0x23, 0xE9}, {0x24, 0x2A}, {0x25, 0x00}, {0x26, 0x1F}, {0x29, 0x59},
        {0x2C, 0x81}, {0x2D, 0x35}, {0x2E, 0x09},
    };
    for (const auto& reg : kBase) cc1101WriteReg(reg[0], reg[1]);
}

static void cc1101SetModulation(const char* mod) {
    uint8_t mdmcfg2 = 0x13;
    uint8_t frend0 = 0x10;
    uint8_t deviatn = 0x47;
    bool ook = false;
    if (mod) {
        if (strcasecmp(mod, "ook") == 0 || strcasecmp(mod, "ask") == 0) {
            mdmcfg2 = 0x33; frend0 = 0x11; ook = true;
        } else if (strcasecmp(mod, "2fsk") == 0 || strcasecmp(mod, "fsk") == 0) {
            mdmcfg2 = 0x03;
        } else if (strcasecmp(mod, "msk") == 0) {
            mdmcfg2 = 0x73;
        }
    }
    cc1101WriteReg(0x12, mdmcfg2);
    cc1101WriteReg(0x15, deviatn);
    cc1101WriteReg(0x22, frend0);
    if (ook) {
        uint8_t pa[2] = {0x00, 0xC0};
        cc1101WriteBurst(CC1101_PATABLE, pa, 2);
    } else {
        uint8_t pa = 0xC0;
        cc1101WriteBurst(CC1101_PATABLE, &pa, 1);
    }
}

void cc1101SetFrequency(double mhz) {
    uint32_t freqWord = (uint32_t)((mhz * 65536.0) / CC1101_XOSC_MHZ + 0.5);
    cc1101WriteReg(0x0D, (freqWord >> 16) & 0xFF);
    cc1101WriteReg(0x0E, (freqWord >> 8) & 0xFF);
    cc1101WriteReg(0x0F, freqWord & 0xFF);
    g_subghzFreq = mhz;
}

bool cc1101Transmit(const uint8_t* data, size_t len) {
    if (len == 0 || len > SUBGHZ_MAX_PACKET) return false;
    cc1101Strobe(CC1101_SIDLE);
    cc1101Strobe(CC1101_SFTX);
    uint8_t fifo[SUBGHZ_MAX_PACKET + 1];
    fifo[0] = (uint8_t)len;
    memcpy(fifo + 1, data, len);
    cc1101WriteBurst(CC1101_FIFO, fifo, len + 1);
    cc1101Strobe(CC1101_STX);
    uint32_t start = millis();
    while (millis() - start < 1000) {
        if ((cc1101ReadStatus(CC1101_MARCSTATE) & 0x1F) == 0x01) break;
        delay(1);
    }
    cc1101Strobe(CC1101_SFTX);
    return true;
}

int cc1101Receive(uint8_t* out, size_t maxLen, int* rssiRaw, uint32_t timeoutMs) {
    cc1101Strobe(CC1101_SIDLE);
    cc1101Strobe(CC1101_SFRX);
    cc1101Strobe(CC1101_SRX);
    uint32_t start = millis();
    while ((uint32_t)(millis() - start) < timeoutMs) {
        uint8_t rxBytes = cc1101ReadStatus(CC1101_RXBYTES);
        if (rxBytes & 0x80) {
            cc1101Strobe(CC1101_SIDLE);
            cc1101Strobe(CC1101_SFRX);
            cc1101Strobe(CC1101_SRX);
            delay(2);
            continue;
        }
        if ((rxBytes & 0x7F) > 0) {
            uint8_t len = cc1101ReadReg(CC1101_FIFO);
            if (len == 0 || len > maxLen) {
                cc1101Strobe(CC1101_SIDLE);
                cc1101Strobe(CC1101_SFRX);
                cc1101Strobe(CC1101_SRX);
                delay(2);
                continue;
            }
            cc1101ReadBurst(CC1101_FIFO, out, len);
            uint8_t status[2];
            cc1101ReadBurst(CC1101_FIFO, status, 2);
            if (rssiRaw) *rssiRaw = status[0];
            cc1101Strobe(CC1101_SIDLE);
            cc1101Strobe(CC1101_SFRX);
            return (int)len;
        }
        delay(2);
    }
    cc1101Strobe(CC1101_SIDLE);
    cc1101Strobe(CC1101_SFRX);
    return 0;
}

void subghzBegin() {
    const ModuleVariantPins& v = resolveModuleVariant();
    String error;
    subghzBegin(433.92, "gfsk", v.line[1], v.line[3], v.line[0], v.line[2], v.line[4], PIN_PWR, error);
}

bool subghzBegin(double freq, const char* mod, int sck, int miso, int mosi,
                 int cs, int gdo0, int powerPin, String& error) {
    if (g_activeModule != MODULE_NONE && g_activeModule != MODULE_SUBGHZ) {
        error = "module busy; end it first";
        return false;
    }
    modulePowerOn(powerPin);
    g_subghzCs = cs;
    g_subghzMiso = miso;
    g_subghzGdo0 = gdo0;
    pinMode(cs, OUTPUT);
    digitalWrite(cs, HIGH);
    if (gdo0 >= 0) pinMode(gdo0, INPUT);
    g_subghzSpi.end();
    g_subghzSpi.begin(sck, miso, mosi, -1);

    cc1101Reset();
    uint8_t version = cc1101ReadStatus(CC1101_VERSION);
    if (version == 0x00 || version == 0xFF) {
        error = "CC1101 not detected (version 0x" + String(version, HEX) + ")";
        g_subghzSpi.end();
        modulePowerOff();
        g_activeModule = MODULE_NONE;
        return false;
    }
    cc1101ApplyBaseConfig();
    cc1101SetModulation(mod);
    cc1101SetFrequency(freq);
    cc1101Strobe(CC1101_SIDLE);
    g_activeModule = MODULE_SUBGHZ;
    return true;
}

void subghzEnd() {
    if (g_activeModule != MODULE_SUBGHZ) return;
    cc1101Strobe(CC1101_SIDLE);
    cc1101Strobe(CC1101_SPWD);
    g_subghzSpi.end();
    modulePowerOff();
    g_activeModule = MODULE_NONE;
}

bool subghzTransmit(const uint8_t* data, size_t len, String& error) {
    if (g_activeModule != MODULE_SUBGHZ) {
        error = "subghz not started";
        return false;
    }
    return cc1101Transmit(data, len);
}

bool subghzReceive(uint8_t* buf, size_t bufLen, size_t& received, uint32_t timeoutMs, String& error) {
    if (g_activeModule != MODULE_SUBGHZ) {
        error = "subghz not started";
        return false;
    }
    int rssi = 0;
    int len = cc1101Receive(buf, bufLen, &rssi, timeoutMs);
    received = (size_t)(len > 0 ? len : 0);
    return len > 0;
}

bool subghzSetFrequency(uint32_t hz, String& error) {
    if (g_activeModule != MODULE_SUBGHZ) {
        error = "subghz not started";
        return false;
    }
    cc1101SetFrequency((double)hz / 1000000.0);
    return true;
}
