#include "lua_api.h"
#include "badge_state.h"
#include "display.h"
#include "http.h"
#include "wifi.h"
#include "espnow.h"
#include "subghz.h"
#include "sound.h"
#include "atecc.h"
#include "mqtt.h"
#include <SPIFFS.h>
#include <esp_heap_caps.h>
#include <esp_random.h>
#include <esp_wifi.h>
#include <esp_http_client.h>
#include <esp_crt_bundle.h>
#include <sodium.h>
#include <esp_sntp.h>

struct LoadedBitmap {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> bits;
};

static void setBitmapPixel(LoadedBitmap& bitmap, int x, int y, bool black) {
    if (!black || x < 0 || y < 0 || x >= bitmap.width || y >= bitmap.height) return;
    size_t rowBytes = (bitmap.width + 7) / 8;
    bitmap.bits[y * rowBytes + x / 8] |= 0x80 >> (x & 7);
}

static uint16_t readLe16(const std::vector<uint8_t>& data, size_t offset) {
    if (offset + 2 > data.size()) return 0;
    return (uint16_t)data[offset] | ((uint16_t)data[offset + 1] << 8);
}

static uint32_t readLe32(const std::vector<uint8_t>& data, size_t offset) {
    if (offset + 4 > data.size()) return 0;
    return (uint32_t)data[offset] | ((uint32_t)data[offset + 1] << 8) |
        ((uint32_t)data[offset + 2] << 16) | ((uint32_t)data[offset + 3] << 24);
}

static int32_t readLeS32(const std::vector<uint8_t>& data, size_t offset) {
    return (int32_t)readLe32(data, offset);
}

static bool isBlackRgb(uint8_t r, uint8_t g, uint8_t b) {
    return ((uint16_t)r * 30 + (uint16_t)g * 59 + (uint16_t)b * 11) < 12800;
}

static bool loadFileBytes(const String& path, std::vector<uint8_t>& bytes, size_t maxBytes, String& error) {
    File file = SPIFFS.open(path, FILE_READ);
    if (!file) {
        error = "Image missing";
        return false;
    }
    size_t size = file.size();
    if (!size || size > maxBytes) {
        file.close();
        error = "Image too large";
        return false;
    }
    bytes.assign(size, 0);
    size_t read = file.read(bytes.data(), bytes.size());
    file.close();
    if (read != size) {
        error = "Image read failed";
        return false;
    }
    return true;
}

static bool readPbmToken(const std::vector<uint8_t>& data, size_t& offset, String& token) {
    token = "";
    while (offset < data.size()) {
        char ch = (char)data[offset];
        if (ch == '#') {
            while (offset < data.size() && data[offset] != '\n') offset++;
        } else if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') {
            offset++;
        } else {
            break;
        }
    }
    while (offset < data.size()) {
        char ch = (char)data[offset];
        if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n' || ch == '#') break;
        token += ch;
        offset++;
    }
    return token.length() > 0;
}

static bool loadPbmBitmap(const std::vector<uint8_t>& data, LoadedBitmap& bitmap, String& error) {
    size_t offset = 0;
    String magic;
    String widthToken;
    String heightToken;
    if (!readPbmToken(data, offset, magic) || !readPbmToken(data, offset, widthToken) ||
        !readPbmToken(data, offset, heightToken)) {
        error = "Bad PBM header";
        return false;
    }

    bitmap.width = widthToken.toInt();
    bitmap.height = heightToken.toInt();
    if (bitmap.width <= 0 || bitmap.height <= 0 ||
        bitmap.width > display.width() || bitmap.height > display.height()) {
        error = "PBM size unsupported";
        return false;
    }

    size_t rowBytes = (bitmap.width + 7) / 8;
    bitmap.bits.assign(rowBytes * bitmap.height, 0);

    if (magic == "P4") {
        if (offset < data.size() &&
            (data[offset] == ' ' || data[offset] == '\t' || data[offset] == '\r' || data[offset] == '\n')) offset++;
        size_t needed = rowBytes * bitmap.height;
        if (offset + needed > data.size()) {
            error = "PBM data short";
            return false;
        }
        memcpy(bitmap.bits.data(), data.data() + offset, needed);
        return true;
    }

    if (magic == "P1") {
        String pixel;
        for (int y = 0; y < bitmap.height; ++y) {
            for (int x = 0; x < bitmap.width; ++x) {
                if (!readPbmToken(data, offset, pixel)) {
                    error = "PBM data short";
                    return false;
                }
                setBitmapPixel(bitmap, x, y, pixel == "1");
            }
        }
        return true;
    }

    error = "Unsupported PBM";
    return false;
}

static bool loadBmpBitmap(const std::vector<uint8_t>& data, LoadedBitmap& bitmap, String& error) {
    if (data.size() < 54 || data[0] != 'B' || data[1] != 'M') {
        error = "Bad BMP header";
        return false;
    }

    uint32_t pixelOffset = readLe32(data, 10);
    uint32_t dibSize = readLe32(data, 14);
    int32_t width = readLeS32(data, 18);
    int32_t rawHeight = readLeS32(data, 22);
    uint16_t planes = readLe16(data, 26);
    uint16_t bpp = readLe16(data, 28);
    uint32_t compression = readLe32(data, 30);
    uint32_t colorsUsed = readLe32(data, 46);
    if (dibSize < 40 || width <= 0 || rawHeight == 0 || planes != 1 || compression != 0 ||
        (bpp != 1 && bpp != 4 && bpp != 8 && bpp != 24 && bpp != 32)) {
        error = "Unsupported BMP";
        return false;
    }

    int height = rawHeight < 0 ? -rawHeight : rawHeight;
    bool topDown = rawHeight < 0;
    if (width > display.width() || height > display.height()) {
        error = "BMP size unsupported";
        return false;
    }

    bitmap.width = width;
    bitmap.height = height;
    size_t outRowBytes = (bitmap.width + 7) / 8;
    bitmap.bits.assign(outRowBytes * bitmap.height, 0);

    uint32_t paletteEntries = bpp <= 8 ? (colorsUsed ? colorsUsed : (1UL << bpp)) : 0;
    size_t paletteOffset = 14 + dibSize;
    if (paletteEntries && paletteOffset + paletteEntries * 4 > data.size()) {
        error = "BMP palette bad";
        return false;
    }

    uint32_t rowBytes = ((uint32_t)width * bpp + 31) / 32 * 4;
    if (pixelOffset + rowBytes * height > data.size()) {
        error = "BMP data short";
        return false;
    }

    for (int y = 0; y < height; ++y) {
        int srcY = topDown ? y : (height - 1 - y);
        size_t rowOffset = pixelOffset + rowBytes * srcY;
        for (int x = 0; x < width; ++x) {
            uint8_t r = 255;
            uint8_t g = 255;
            uint8_t b = 255;
            if (bpp == 24 || bpp == 32) {
                size_t pixel = rowOffset + x * (bpp / 8);
                b = data[pixel];
                g = data[pixel + 1];
                r = data[pixel + 2];
            } else {
                uint8_t index = 0;
                if (bpp == 8) {
                    index = data[rowOffset + x];
                } else if (bpp == 4) {
                    uint8_t packed = data[rowOffset + x / 2];
                    index = (x & 1) ? (packed & 0x0F) : (packed >> 4);
                } else {
                    uint8_t packed = data[rowOffset + x / 8];
                    index = (packed >> (7 - (x & 7))) & 0x01;
                }
                if (index >= paletteEntries) {
                    error = "BMP palette index";
                    return false;
                }
                size_t color = paletteOffset + index * 4;
                b = data[color];
                g = data[color + 1];
                r = data[color + 2];
            }
            setBitmapPixel(bitmap, x, y, isBlackRgb(r, g, b));
        }
    }
    return true;
}

static bool loadStoredBitmap(const String& name, LoadedBitmap& bitmap, String& error) {
    String path = imagePathForName(name);
    if (!path.length()) {
        error = "Bad image name";
        return false;
    }

    std::vector<uint8_t> bytes;
    if (!loadFileBytes(path, bytes, MAX_IMAGE_BYTES, error)) return false;
    if (name.endsWith(".pbm")) return loadPbmBitmap(bytes, bitmap, error);
    if (name.endsWith(".bmp")) return loadBmpBitmap(bytes, bitmap, error);
    error = "Unsupported image";
    return false;
}

static uint16_t canvasColor(uint16_t displayColor) {
    return displayColor == GxEPD_BLACK ? 1 : 0;
}

static void refreshLuaCanvas() {
    const uint8_t* src = g_luaCanvas.getBuffer();
    uint8_t*       dst = g_frame.getBuffer();
    for (int i = 0; i < FRAME_BYTES; ++i) dst[i] = ~src[i];
    g_luaDisplayActive = true;
    g_needsRedraw = false;
    flushFrame();
}

static void luaFlushOrDefer() {
    if (g_luaDeferFlush) { g_luaFramePending = true; return; }
    refreshLuaCanvas();
}

static void renderBitmap(const LoadedBitmap& bitmap, int x, int y, bool clearScreen) {
    if (clearScreen) g_luaCanvas.fillScreen(0);
    g_luaCanvas.drawBitmap(x, y, bitmap.bits.data(), bitmap.width, bitmap.height, 1);
    luaFlushOrDefer();
}

static bool luaCanReadGpio(int pin) {
    for (int allowed : kLuaReadableGpios) {
        if (allowed == pin) return true;
    }
    return false;
}

static bool luaConfigureInputGpio(lua_State* L, int pin, int modeArg) {
    if (!luaCanReadGpio(pin)) {
        lua_pushboolean(L, false);
        lua_pushfstring(L, "GPIO %d is not readable by Lua", pin);
        return false;
    }

    const char* mode = luaL_optstring(L, modeArg, "input");
    if (strcmp(mode, "input") == 0 || strcmp(mode, "floating") == 0) {
        pinMode(pin, INPUT);
    } else if (strcmp(mode, "pullup") == 0 || strcmp(mode, "up") == 0) {
        pinMode(pin, INPUT_PULLUP);
    } else if (strcmp(mode, "pulldown") == 0 || strcmp(mode, "down") == 0) {
        pinMode(pin, INPUT_PULLDOWN);
    } else {
        lua_pushboolean(L, false);
        lua_pushfstring(L, "Bad GPIO mode: %s", mode);
        return false;
    }
    return true;
}

static bool luaTableBool(lua_State* L, int index, const char* key, bool fallback) {
    if (!lua_istable(L, index)) return fallback;
    lua_getfield(L, index, key);
    bool value = lua_isnil(L, -1) ? fallback : lua_toboolean(L, -1);
    lua_pop(L, 1);
    return value;
}

static int luaTableInt(lua_State* L, int index, const char* key, int fallback) {
    if (!lua_istable(L, index)) return fallback;
    lua_getfield(L, index, key);
    int value = lua_isnumber(L, -1) ? (int)lua_tointeger(L, -1) : fallback;
    lua_pop(L, 1);
    return value;
}

static const char* luaTableString(lua_State* L, int index, const char* key, const char* fallback) {
    if (!lua_istable(L, index)) return fallback;
    lua_getfield(L, index, key);
    const char* value = lua_isstring(L, -1) ? lua_tostring(L, -1) : fallback;
    lua_pop(L, 1);
    return value;
}

static uint16_t displayColorFromName(const char* value, uint16_t fallback) {
    if (!value) return fallback;
    if (strcmp(value, "white") == 0 || strcmp(value, "bg") == 0 || strcmp(value, "background") == 0) {
        return GxEPD_WHITE;
    }
    return GxEPD_BLACK;
}

static const GFXfont* displayFontFromName(const char* value) {
    if (!value || strcmp(value, "small") == 0 || strcmp(value, "regular") == 0) return &FreeMono9pt7b;
    if (strcmp(value, "bold") == 0) return &FreeMonoBold9pt7b;
    if (strcmp(value, "large") == 0 || strcmp(value, "title") == 0) return &FreeMonoBold18pt7b;
    return &FreeMono9pt7b;
}

static bool luaDisplayClearArg(lua_State* L, int index, bool fallback = true) {
    if (lua_gettop(L) < index) return fallback;
    if (lua_istable(L, index)) return luaTableBool(L, index, "clear", fallback);
    return lua_toboolean(L, index);
}

// --- Lua bindings ----------------------------------------------------------

static int luaOnionSubghzBegin(lua_State* L) {
    int opt = lua_istable(L, 1) ? 1 : 0;
    const ModuleVariantPins& v = resolveModuleVariant();
    double freq = 433.92;
    const char* mod = "gfsk";
    if (opt) {
        lua_getfield(L, opt, "freq");
        if (lua_isnumber(L, -1)) freq = lua_tonumber(L, -1);
        lua_pop(L, 1);
        lua_getfield(L, opt, "modulation");
        if (lua_type(L, -1) == LUA_TSTRING) mod = lua_tostring(L, -1);
        lua_pop(L, 1);
    }
    int mosi = luaModulePin(L, opt, "mosi", v.line[0]);
    int sck = luaModulePin(L, opt, "sck", v.line[1]);
    int cs = luaModulePin(L, opt, "cs", v.line[2]);
    int miso = luaModulePin(L, opt, "miso", v.line[3]);
    int gdo0 = luaModulePin(L, opt, "gdo0", v.line[4]);
    int powerPin = luaModulePin(L, opt, "power_pin", PIN_PWR);

    String error;
    if (!subghzBegin(freq, mod, sck, miso, mosi, cs, gdo0, powerPin, error)) {
        lua_pushnil(L);
        lua_pushstring(L, error.c_str());
        return 2;
    }
    lua_pushboolean(L, true);
    return 1;
}

static int luaOnionSubghzTransmit(lua_State* L) {
    if (g_activeModule != MODULE_SUBGHZ) {
        lua_pushnil(L);
        lua_pushstring(L, "subghz not started");
        return 2;
    }
    size_t len = 0;
    const char* data = luaL_checklstring(L, 1, &len);
    if (len == 0 || len > SUBGHZ_MAX_PACKET) {
        lua_pushnil(L);
        lua_pushstring(L, "payload must be 1-61 bytes");
        return 2;
    }
    if (!cc1101Transmit((const uint8_t*)data, len)) {
        lua_pushnil(L);
        lua_pushstring(L, "transmit failed");
        return 2;
    }
    lua_pushboolean(L, true);
    return 1;
}

static int luaOnionSubghzReceive(lua_State* L) {
    if (g_activeModule != MODULE_SUBGHZ) {
        lua_pushnil(L);
        lua_pushstring(L, "subghz not started");
        return 2;
    }
    uint32_t timeoutMs = (uint32_t)luaL_optinteger(L, 1, 0);
    if (timeoutMs > SUBGHZ_RX_MAX_MS) timeoutMs = SUBGHZ_RX_MAX_MS;

    uint8_t buf[SUBGHZ_MAX_PACKET];
    int rssiRaw = 0;
    int n = cc1101Receive(buf, sizeof(buf), &rssiRaw, timeoutMs);
    if (n <= 0) {
        lua_pushnil(L);
        return 1;
    }
    int rssiDbm = (rssiRaw >= 128 ? (rssiRaw - 256) : rssiRaw) / 2 - 74;
    lua_newtable(L);
    lua_pushlstring(L, (const char*)buf, n);
    lua_setfield(L, -2, "payload");
    lua_pushlstring(L, (const char*)buf, n);
    lua_setfield(L, -2, "message");
    lua_pushinteger(L, n);
    lua_setfield(L, -2, "len");
    lua_pushinteger(L, rssiRaw);
    lua_setfield(L, -2, "rssi");
    lua_pushinteger(L, rssiDbm);
    lua_setfield(L, -2, "rssi_dbm");
    return 1;
}

static int luaOnionSubghzSetFrequency(lua_State* L) {
    if (g_activeModule != MODULE_SUBGHZ) {
        lua_pushnil(L);
        lua_pushstring(L, "subghz not started");
        return 2;
    }
    double mhz = luaL_checknumber(L, 1);
    cc1101Strobe(CC1101_SIDLE);
    cc1101SetFrequency(mhz);
    lua_pushboolean(L, true);
    return 1;
}

static int luaOnionSubghzInfo(lua_State* L) {
    lua_newtable(L);
    lua_pushstring(L, resolveModuleVariant().name);
    lua_setfield(L, -2, "variant");
    lua_pushboolean(L, g_activeModule == MODULE_SUBGHZ);
    lua_setfield(L, -2, "active");
    if (g_activeModule == MODULE_SUBGHZ) {
        lua_pushnumber(L, g_subghzFreq);
        lua_setfield(L, -2, "frequency");
        lua_pushinteger(L, cc1101ReadStatus(CC1101_VERSION));
        lua_setfield(L, -2, "version");
        lua_pushinteger(L, cc1101ReadStatus(CC1101_PARTNUM));
        lua_setfield(L, -2, "partnum");
    }
    return 1;
}

static int luaOnionSubghzEnd(lua_State* L) {
    subghzEnd();
    lua_pushboolean(L, true);
    return 1;
}

static int luaOnionSoundSpeakerBegin(lua_State* L) {
    int opt = lua_istable(L, 1) ? 1 : 0;
    const ModuleVariantPins& v = resolveModuleVariant();
    int bclk = luaModulePin(L, opt, "bclk", v.line[1]);
    int ws = luaModulePin(L, opt, "ws", v.line[2]);
    int dout = luaModulePin(L, opt, "dout", v.line[3]);
    int ctrl = luaModulePin(L, opt, "ctrl", v.line[4]);
    int powerPin = luaModulePin(L, opt, "power_pin", PIN_PWR);
    int sampleRate = (int)luaModulePin(L, opt, "sample_rate", SOUND_SPK_SAMPLE_RATE);

    String error;
    if (!soundSpeakerBegin(bclk, ws, dout, ctrl, sampleRate, powerPin, error)) {
        lua_pushnil(L);
        lua_pushstring(L, error.c_str());
        return 2;
    }
    lua_pushboolean(L, true);
    return 1;
}

static int luaOnionSoundPlayTone(lua_State* L) {
    if (g_activeModule != MODULE_SOUND_SPK) {
        lua_pushnil(L);
        lua_pushstring(L, "speaker not started");
        return 2;
    }
    double freq = luaL_checknumber(L, 1);
    int durationMs = (int)luaL_optinteger(L, 2, 200);
    double volume = (double)luaL_optnumber(L, 3, 0.6);
    if (durationMs < 0) durationMs = 0;
    if (durationMs > SOUND_TONE_MAX_MS) durationMs = SOUND_TONE_MAX_MS;
    soundPlayTone(freq, durationMs, volume);
    lua_pushboolean(L, true);
    return 1;
}

static int luaOnionSoundPlay(lua_State* L) {
    if (g_activeModule != MODULE_SOUND_SPK) {
        lua_pushnil(L);
        lua_pushstring(L, "speaker not started");
        return 2;
    }
    size_t len = 0;
    const char* pcm = luaL_checklstring(L, 1, &len);
    if (len > SOUND_PLAY_MAX_BYTES) len = SOUND_PLAY_MAX_BYTES;
    size_t off = 0;
    while (off < len) {
        size_t written = 0;
        if (i2s_channel_write(g_i2sTx, pcm + off, len - off, &written, 1000) != ESP_OK) break;
        if (written == 0) break;
        off += written;
    }
    lua_pushinteger(L, (lua_Integer)off);
    return 1;
}

static int luaOnionSoundSpeakerEnd(lua_State* L) {
    if (g_activeModule == MODULE_SOUND_SPK) soundStop();
    lua_pushboolean(L, true);
    return 1;
}

static int luaOnionSoundMicBegin(lua_State* L) {
    int opt = lua_istable(L, 1) ? 1 : 0;
    const ModuleVariantPins& v = resolveModuleVariant();
    int clk = luaModulePin(L, opt, "clk", v.line[1]);
    int din = luaModulePin(L, opt, "din", v.line[0]);
    int ws = luaModulePin(L, opt, "ws", v.line[2]);
    int ctrl = luaModulePin(L, opt, "ctrl", v.line[4]);
    int powerPin = luaModulePin(L, opt, "power_pin", PIN_PWR);
    int sampleRate = (int)luaModulePin(L, opt, "sample_rate", SOUND_MIC_SAMPLE_RATE);
    int dmaDesc = luaTableInt(L, opt, "dma_desc", 0);
    int dmaFrame = luaTableInt(L, opt, "dma_frame", 0);
    int discardMs = luaTableInt(L, opt, "discard_ms", 0);

    String error;
    if (!soundMicBegin(clk, din, ws, ctrl, sampleRate, dmaDesc, dmaFrame,
                       discardMs, powerPin, error)) {
        lua_pushnil(L);
        lua_pushstring(L, error.c_str());
        return 2;
    }
    lua_pushboolean(L, true);
    return 1;
}

static int luaOnionSoundMicRead(lua_State* L) {
    if (g_activeModule != MODULE_SOUND_MIC) {
        lua_pushnil(L);
        lua_pushstring(L, "mic not started");
        return 2;
    }
    int numSamples = (int)luaL_optinteger(L, 1, 256);
    if (numSamples < 1) numSamples = 1;
    if (numSamples > SOUND_MIC_MAX_SAMPLES) numSamples = SOUND_MIC_MAX_SAMPLES;
    int timeoutMs = (int)luaL_optinteger(L, 2, 1000);
    if (timeoutMs < 0) timeoutMs = 0;
    if (timeoutMs > SOUND_MIC_READ_MAX_TIMEOUT_MS) timeoutMs = SOUND_MIC_READ_MAX_TIMEOUT_MS;
    static int16_t buf[SOUND_MIC_MAX_SAMPLES];
    size_t bytesRead = 0;
    esp_err_t rc = i2s_channel_read(g_i2sRx, buf, numSamples * sizeof(int16_t),
                                    &bytesRead, timeoutMs);
    bool timedOut = rc == ESP_ERR_TIMEOUT; // a timeout still returns what arrived
    if (timedOut) g_soundMicTimeouts++;
    if (rc != ESP_OK && !timedOut) {
        lua_pushnil(L);
        lua_pushstring(L, "mic read failed");
        return 2;
    }
    g_soundMicSamples += bytesRead / sizeof(int16_t);
    g_soundMicBytes += bytesRead;

    lua_pushlstring(L, (const char*)buf, bytesRead);
    lua_newtable(L);
    lua_pushinteger(L, (lua_Integer)(bytesRead / sizeof(int16_t)));
    lua_setfield(L, -2, "samples");
    lua_pushinteger(L, (lua_Integer)bytesRead);
    lua_setfield(L, -2, "bytes");
    lua_pushinteger(L, g_soundSampleRate);
    lua_setfield(L, -2, "sample_rate");
    lua_pushboolean(L, timedOut);
    lua_setfield(L, -2, "timeout");
    lua_pushinteger(L, g_soundMicSamples);
    lua_setfield(L, -2, "total_samples");
    lua_pushinteger(L, g_soundMicBytes);
    lua_setfield(L, -2, "total_bytes");
    lua_pushinteger(L, g_soundMicTimeouts);
    lua_setfield(L, -2, "timeouts");
    lua_pushinteger(L, (lua_Integer)(millis() - g_soundMicStartedAt));
    lua_setfield(L, -2, "elapsed_ms");
    return 2;
}

static int luaOnionSoundMicLevel(lua_State* L) {
    if (g_activeModule != MODULE_SOUND_MIC) {
        lua_pushnil(L);
        lua_pushstring(L, "mic not started");
        return 2;
    }
    int durationMs = (int)luaL_optinteger(L, 1, 100);
    if (durationMs < 1) durationMs = 1;
    if (durationMs > 1000) durationMs = 1000;
    int wantSamples = g_soundSampleRate * durationMs / 1000;
    static int16_t buf[512];
    double sumSq = 0.0;
    int counted = 0;
    int peak = 0;
    while (counted < wantSamples) {
        int n = wantSamples - counted;
        if (n > 512) n = 512;
        size_t bytesRead = 0;
        if (i2s_channel_read(g_i2sRx, buf, n * sizeof(int16_t), &bytesRead, 1000) != ESP_OK) break;
        int got = (int)(bytesRead / sizeof(int16_t));
        if (got == 0) break;
        for (int i = 0; i < got; i++) {
            int s = buf[i];
            sumSq += (double)s * s;
            int a = s < 0 ? -s : s;
            if (a > peak) peak = a;
        }
        counted += got;
    }
    double rms = counted ? sqrt(sumSq / counted) : 0.0;
    lua_newtable(L);
    lua_pushnumber(L, rms);
    lua_setfield(L, -2, "rms");
    lua_pushinteger(L, peak);
    lua_setfield(L, -2, "peak");
    lua_pushinteger(L, counted);
    lua_setfield(L, -2, "samples");
    return 1;
}

static int luaOnionSoundMicEnd(lua_State* L) {
    if (g_activeModule != MODULE_SOUND_MIC) {
        lua_pushboolean(L, true);
        return 1;
    }
    uint32_t durationMs = millis() - g_soundMicStartedAt;
    uint32_t samples = g_soundMicSamples;
    uint32_t bytes = g_soundMicBytes;
    uint32_t timeouts = g_soundMicTimeouts;
    int sampleRate = g_soundSampleRate;
    soundStop();
    Serial.printf("[onion-os] sound mic stop samples=%u bytes=%u duration_ms=%u timeouts=%u\n",
                  (unsigned)samples, (unsigned)bytes, (unsigned)durationMs,
                  (unsigned)timeouts);

    lua_pushboolean(L, true);
    lua_newtable(L);
    lua_pushinteger(L, samples);
    lua_setfield(L, -2, "samples");
    lua_pushinteger(L, bytes);
    lua_setfield(L, -2, "bytes");
    lua_pushinteger(L, durationMs);
    lua_setfield(L, -2, "duration_ms");
    lua_pushinteger(L, timeouts);
    lua_setfield(L, -2, "timeouts");
    lua_pushinteger(L, sampleRate);
    lua_setfield(L, -2, "sample_rate");
    return 2;
}

static int luaOnionLog(lua_State* L) {
    const char* message = luaL_checkstring(L, 1);
    setLog("Lua: " + String(message));
    return 0;
}

static int luaOnionHardwareId(lua_State* L) {
    lua_pushstring(L, g_identity.hardwareId.c_str());
    return 1;
}

static int luaOnionOnionId(lua_State* L) {
    lua_pushinteger(L, (lua_Integer)g_identity.onionId);
    return 1;
}

static int luaOnionWallet(lua_State* L) {
    lua_pushstring(L, g_identity.solanaPublicKey.c_str());
    return 1;
}

static int luaOnionUsername(lua_State* L) {
    lua_pushstring(L, g_identity.username.c_str());
    return 1;
}

// onion.secure_random([count]) -> string of `count` random bytes from the
// ATECC608A hardware RNG (default 32, max 256). Returns nil + error on failure.
static int luaOnionSecureRandom(lua_State* L) {
    lua_Integer count = luaL_optinteger(L, 1, 32);
    if (count < 1) count = 1;
    if (count > 256) count = 256;

    uint8_t buf[256];
    String error;
    if (!ateccRandom(buf, (size_t)count, error)) {
        lua_pushnil(L);
        lua_pushstring(L, error.c_str());
        return 2;
    }

    lua_pushlstring(L, reinterpret_cast<const char*>(buf), (size_t)count);
    return 1;
}

// Shared worker for onion.http_get / onion.http_post. `optionsIdx` is the Lua
// stack index of an options table ({ headers = {...}, content_type = ...,
// timeout_ms = ... }) or 0 when none was passed. On success returns one table
// { status, body }; on failure returns nil plus an error string.
static int luaHttpDo(lua_State* L, esp_http_client_method_t method, const char* url,
                     const char* body, size_t bodyLen, int optionsIdx) {
    if (!ensureWifi()) {
        lua_pushnil(L);
        lua_pushstring(L, "wifi unavailable");
        return 2;
    }

    String responseBuffer;
    esp_http_client_config_t cfg = {};
    cfg.url = url;
    cfg.timeout_ms = LUA_HTTP_DEFAULT_TIMEOUT_MS;
    cfg.crt_bundle_attach = esp_crt_bundle_attach;
    cfg.event_handler = httpCaptureEvent;
    cfg.user_data = &responseBuffer;

    const char* contentType = "application/json";
    if (optionsIdx) {
        lua_getfield(L, optionsIdx, "timeout_ms");
        if (lua_isnumber(L, -1)) {
            int t = (int)lua_tointeger(L, -1);
            if (t > 0) cfg.timeout_ms = t > LUA_HTTP_MAX_TIMEOUT_MS ? LUA_HTTP_MAX_TIMEOUT_MS : t;
        }
        lua_pop(L, 1);
        lua_getfield(L, optionsIdx, "content_type");
        if (lua_type(L, -1) == LUA_TSTRING) contentType = lua_tostring(L, -1);
        lua_pop(L, 1);
    }

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        lua_pushnil(L);
        lua_pushstring(L, "http init failed");
        return 2;
    }

    esp_http_client_set_method(client, method);
    if (body) {
        esp_http_client_set_header(client, "Content-Type", contentType);
        esp_http_client_set_post_field(client, body, bodyLen);
    }

    if (optionsIdx) {
        lua_getfield(L, optionsIdx, "headers");
        if (lua_istable(L, -1)) {
            int headersTable = lua_gettop(L);
            lua_pushnil(L);
            while (lua_next(L, headersTable) != 0) {
                if (lua_type(L, -2) == LUA_TSTRING && lua_type(L, -1) == LUA_TSTRING) {
                    esp_http_client_set_header(client, lua_tostring(L, -2), lua_tostring(L, -1));
                }
                lua_pop(L, 1);
            }
        }
        lua_pop(L, 1);
    }

    esp_err_t err = esp_http_client_perform(client);
    int code = err == ESP_OK ? esp_http_client_get_status_code(client) : -1;
    esp_http_client_cleanup(client);

    if (err != ESP_OK) {
        lua_pushnil(L);
        lua_pushstring(L, esp_err_to_name(err));
        return 2;
    }

    lua_newtable(L);
    lua_pushinteger(L, code);
    lua_setfield(L, -2, "status");
    lua_pushlstring(L, responseBuffer.c_str(), responseBuffer.length());
    lua_setfield(L, -2, "body");
    return 1;
}

// onion.http_get(url [, options]) -> { status, body } | nil, err
static int luaOnionHttpGet(lua_State* L) {
    const char* url = luaL_checkstring(L, 1);
    int optionsIdx = lua_istable(L, 2) ? 2 : 0;
    return luaHttpDo(L, HTTP_METHOD_GET, url, nullptr, 0, optionsIdx);
}

// onion.http_post(url, body [, options]) -> { status, body } | nil, err
static int luaOnionHttpPost(lua_State* L) {
    const char* url = luaL_checkstring(L, 1);
    size_t bodyLen = 0;
    const char* body = luaL_optlstring(L, 2, "", &bodyLen);
    int optionsIdx = lua_istable(L, 3) ? 3 : 0;
    return luaHttpDo(L, HTTP_METHOD_POST, url, body, bodyLen, optionsIdx);
}

// onion.mqtt_connected() -> bool
static int luaOnionMqttConnected(lua_State* L) {
    lua_pushboolean(L, g_mqttConnected);
    return 1;
}

// onion.mqtt_subscribe(topic [, qos]) -> true | nil, err
static int luaOnionMqttSubscribe(lua_State* L) {
    const char* sub = luaL_checkstring(L, 1);
    int qos = (int)luaL_optinteger(L, 2, 1);
    if (qos < 0) qos = 0;
    if (qos > 2) qos = 2;
    if (strlen(sub) > ONION_LUA_MQTT_MAX_TOPIC) {
        lua_pushnil(L);
        lua_pushstring(L, "topic too long");
        return 2;
    }

    ensureMqtt();
    if (!g_mqtt || !g_mqttConnected) {
        lua_pushnil(L);
        lua_pushstring(L, "mqtt not connected");
        return 2;
    }

    if (esp_mqtt_client_subscribe(g_mqtt, sub, qos) < 0) {
        lua_pushnil(L);
        lua_pushstring(L, "subscribe failed");
        return 2;
    }

    // Track the filter so inbound matches are queued for onion.mqtt_receive().
    portENTER_CRITICAL(&g_luaMqttMux);
    bool known = false;
    for (uint8_t i = 0; i < g_luaMqttSubCount; i++) {
        if (strcmp(g_luaMqttSubs[i], sub) == 0) { known = true; break; }
    }
    if (!known && g_luaMqttSubCount < ONION_LUA_MQTT_MAX_SUBS) {
        strncpy(g_luaMqttSubs[g_luaMqttSubCount], sub, ONION_LUA_MQTT_MAX_TOPIC);
        g_luaMqttSubs[g_luaMqttSubCount][ONION_LUA_MQTT_MAX_TOPIC] = '\0';
        g_luaMqttSubCount++;
    }
    portEXIT_CRITICAL(&g_luaMqttMux);

    lua_pushboolean(L, true);
    return 1;
}

// onion.mqtt_unsubscribe(topic) -> true | nil, err
static int luaOnionMqttUnsubscribe(lua_State* L) {
    const char* sub = luaL_checkstring(L, 1);
    if (g_mqtt && g_mqttConnected) esp_mqtt_client_unsubscribe(g_mqtt, sub);

    portENTER_CRITICAL(&g_luaMqttMux);
    for (uint8_t i = 0; i < g_luaMqttSubCount; i++) {
        if (strcmp(g_luaMqttSubs[i], sub) == 0) {
            for (uint8_t j = i + 1; j < g_luaMqttSubCount; j++) {
                strcpy(g_luaMqttSubs[j - 1], g_luaMqttSubs[j]);
            }
            g_luaMqttSubCount--;
            break;
        }
    }
    portEXIT_CRITICAL(&g_luaMqttMux);

    lua_pushboolean(L, true);
    return 1;
}

// onion.mqtt_publish(topic, payload [, qos [, retain]]) -> true | nil, err
static int luaOnionMqttPublish(lua_State* L) {
    const char* mqttTopic = luaL_checkstring(L, 1);
    size_t payloadLen = 0;
    const char* payload = luaL_checklstring(L, 2, &payloadLen);
    int qos = (int)luaL_optinteger(L, 3, 1);
    if (qos < 0) qos = 0;
    if (qos > 2) qos = 2;
    int retain = lua_toboolean(L, 4) ? 1 : 0;

    ensureMqtt();
    if (!g_mqtt || !g_mqttConnected) {
        lua_pushnil(L);
        lua_pushstring(L, "mqtt not connected");
        return 2;
    }

    if (esp_mqtt_client_publish(g_mqtt, mqttTopic, payload, (int)payloadLen, qos, retain) < 0) {
        lua_pushnil(L);
        lua_pushstring(L, "publish failed");
        return 2;
    }
    lua_pushboolean(L, true);
    return 1;
}

// onion.mqtt_receive([timeout_ms]) -> { topic, payload, message, len, received_at } | nil
static int luaOnionMqttReceive(lua_State* L) {
    uint32_t timeoutMs = (uint32_t)luaL_optinteger(L, 1, 0);
    if (timeoutMs > LUA_MQTT_RECV_MAX_MS) timeoutMs = LUA_MQTT_RECV_MAX_MS;

    uint32_t start = millis();
    MqttQueuedMessage msg;
    while (!luaMqttQueuePop(msg)) {
        if ((uint32_t)(millis() - start) >= timeoutMs) {
            lua_pushnil(L);
            return 1;
        }
        delay(10);
    }

    lua_newtable(L);
    lua_pushlstring(L, msg.topic, msg.topicLen);
    lua_setfield(L, -2, "topic");
    lua_pushlstring(L, msg.payload, msg.payloadLen);
    lua_setfield(L, -2, "payload");
    lua_pushlstring(L, msg.payload, msg.payloadLen);
    lua_setfield(L, -2, "message");
    lua_pushinteger(L, msg.payloadLen);
    lua_setfield(L, -2, "len");
    lua_pushinteger(L, (lua_Integer)msg.receivedAt);
    lua_setfield(L, -2, "received_at");
    return 1;
}

// onion.mqtt_info() -> { connected, uri, prefix, subscriptions, queued }
static int luaOnionMqttInfo(lua_State* L) {
    lua_newtable(L);
    lua_pushboolean(L, g_mqttConnected);
    lua_setfield(L, -2, "connected");
    lua_pushstring(L, g_config.mqttUri.c_str());
    lua_setfield(L, -2, "uri");
    lua_pushstring(L, (g_config.mqttTopicPrefix.length() ? g_config.mqttTopicPrefix : String("oniondao")).c_str());
    lua_setfield(L, -2, "prefix");
    portENTER_CRITICAL(&g_luaMqttMux);
    uint8_t subCount = g_luaMqttSubCount;
    uint8_t queued = g_luaMqttQueueCount;
    portEXIT_CRITICAL(&g_luaMqttMux);
    lua_pushinteger(L, subCount);
    lua_setfield(L, -2, "subscriptions");
    lua_pushinteger(L, queued);
    lua_setfield(L, -2, "queued");
    return 1;
}

static int luaOnionDisplaySize(lua_State* L) {
    lua_newtable(L);
    lua_pushinteger(L, display.width());
    lua_setfield(L, -2, "width");
    lua_pushinteger(L, display.height());
    lua_setfield(L, -2, "height");
    return 1;
}

static int luaOnionClearDisplay(lua_State*) {
    g_luaCanvas.fillScreen(0);
    // Scripts use clear_display as the explicit ghost-clear, so always full.
    g_forceFullRefresh = true;
    luaFlushOrDefer();
    return 0;
}

static int luaOnionReleaseDisplay(lua_State*) {
    g_luaDisplayActive = false;
    g_forceFullRefresh = true;  // guarantee clean full refresh over Lua content
    g_needsRedraw = true;
    return 0;
}

static int luaOnionDisplayText(lua_State* L) {
    const char* text = luaL_checkstring(L, 1);
    int x = (int)luaL_optinteger(L, 2, 6);
    int y = (int)luaL_optinteger(L, 3, 22);
    bool clearScreen = luaDisplayClearArg(L, 4, true);
    const GFXfont* font = &FreeMono9pt7b;
    uint16_t color = GxEPD_BLACK;
    uint16_t bg = GxEPD_WHITE;

    if (lua_istable(L, 4)) {
        font = displayFontFromName(luaTableString(L, 4, "font", "small"));
        color = displayColorFromName(luaTableString(L, 4, "color", "black"), GxEPD_BLACK);
        bg = displayColorFromName(luaTableString(L, 4, "background", "white"), GxEPD_WHITE);
    } else if (lua_gettop(L) >= 5) {
        font = displayFontFromName(luaL_optstring(L, 5, "small"));
    }

    if (clearScreen) g_luaCanvas.fillScreen(canvasColor(bg));
    g_luaCanvas.setFont(font);
    g_luaCanvas.setTextColor(canvasColor(color));
    g_luaCanvas.setCursor(x, y);
    g_luaCanvas.print(text);
    luaFlushOrDefer();
    return 0;
}

static int luaOnionDisplayLines(lua_State* L) {
    int x = (int)luaL_optinteger(L, 2, 6);
    int y = (int)luaL_optinteger(L, 3, 22);
    int lineHeight = (int)luaL_optinteger(L, 4, 18);
    bool clearScreen = luaDisplayClearArg(L, 5, true);
    const GFXfont* font = &FreeMono9pt7b;
    uint16_t color = GxEPD_BLACK;
    uint16_t bg = GxEPD_WHITE;
    if (lua_istable(L, 5)) {
        font = displayFontFromName(luaTableString(L, 5, "font", "small"));
        color = displayColorFromName(luaTableString(L, 5, "color", "black"), GxEPD_BLACK);
        bg = displayColorFromName(luaTableString(L, 5, "background", "white"), GxEPD_WHITE);
        lineHeight = luaTableInt(L, 5, "line_height", lineHeight);
    }
    if (lineHeight < 8) lineHeight = 8;
    if (lineHeight > 64) lineHeight = 64;

    if (clearScreen) g_luaCanvas.fillScreen(canvasColor(bg));
    g_luaCanvas.setFont(font);
    g_luaCanvas.setTextColor(canvasColor(color));
    if (lua_istable(L, 1)) {
        int count = (int)lua_rawlen(L, 1);
        for (int i = 1; i <= count; ++i) {
            lua_rawgeti(L, 1, i);
            const char* line = lua_tostring(L, -1);
            if (line) {
                g_luaCanvas.setCursor(x, y + (i - 1) * lineHeight);
                g_luaCanvas.print(line);
            }
            lua_pop(L, 1);
        }
    } else {
        String text = luaL_checkstring(L, 1);
        int line = 0;
        int start = 0;
        while (start <= (int)text.length()) {
            int end = text.indexOf('\n', start);
            if (end < 0) end = text.length();
            g_luaCanvas.setCursor(x, y + line * lineHeight);
            g_luaCanvas.print(text.substring(start, end));
            if (end == (int)text.length()) break;
            start = end + 1;
            line++;
        }
    }
    luaFlushOrDefer();
    return 0;
}

static int luaOnionDisplayLine(lua_State* L) {
    int x0 = (int)luaL_checkinteger(L, 1);
    int y0 = (int)luaL_checkinteger(L, 2);
    int x1 = (int)luaL_checkinteger(L, 3);
    int y1 = (int)luaL_checkinteger(L, 4);
    bool clearScreen = luaDisplayClearArg(L, 5, false);
    uint16_t color = GxEPD_BLACK;
    if (lua_istable(L, 5)) color = displayColorFromName(luaTableString(L, 5, "color", "black"), GxEPD_BLACK);

    if (clearScreen) g_luaCanvas.fillScreen(0);
    g_luaCanvas.drawLine(x0, y0, x1, y1, canvasColor(color));
    luaFlushOrDefer();
    return 0;
}

static int luaOnionDisplayRect(lua_State* L) {
    int x = (int)luaL_checkinteger(L, 1);
    int y = (int)luaL_checkinteger(L, 2);
    int w = (int)luaL_checkinteger(L, 3);
    int h = (int)luaL_checkinteger(L, 4);
    bool clearScreen = luaDisplayClearArg(L, 5, false);
    bool fill = lua_istable(L, 5) ? luaTableBool(L, 5, "fill", false) : false;
    uint16_t color = lua_istable(L, 5)
        ? displayColorFromName(luaTableString(L, 5, "color", "black"), GxEPD_BLACK)
        : GxEPD_BLACK;

    if (clearScreen) g_luaCanvas.fillScreen(0);
    if (fill) g_luaCanvas.fillRect(x, y, w, h, canvasColor(color));
    else g_luaCanvas.drawRect(x, y, w, h, canvasColor(color));
    luaFlushOrDefer();
    return 0;
}

// onion.display_buffer() -> string (5808 raw bytes, 264×176 1-bpp, bit=1=black)
// Returns the current Lua canvas so scripts can upload the display state to a server.
static int luaOnionDisplayBuffer(lua_State* L) {
    lua_pushlstring(L, (const char*)g_luaCanvas.getBuffer(), FRAME_BYTES);
    return 1;
}

// ── Deferred flush (display_begin / display_commit / display_flush) ────────────

static int luaOnionDisplayBegin(lua_State*) {
    g_luaDeferFlush = true;
    return 0;
}

static int luaOnionDisplayCommit(lua_State*) {
    g_luaDeferFlush = false;
    if (g_luaFramePending) { g_luaFramePending = false; refreshLuaCanvas(); }
    return 0;
}

static int luaOnionDisplayFlush(lua_State*) {
    if (g_luaFramePending) { g_luaFramePending = false; refreshLuaCanvas(); }
    return 0;
}

// ── Vector drawing primitives ─────────────────────────────────────────────────

static int luaOnionDisplayCircle(lua_State* L) {
    int cx = (int)luaL_checkinteger(L, 1);
    int cy = (int)luaL_checkinteger(L, 2);
    int r  = (int)luaL_checkinteger(L, 3);
    bool clearScreen = luaDisplayClearArg(L, 4, false);
    bool fill  = lua_istable(L, 4) ? luaTableBool(L, 4, "fill",  false) : false;
    uint16_t color = lua_istable(L, 4)
        ? displayColorFromName(luaTableString(L, 4, "color", "black"), GxEPD_BLACK)
        : GxEPD_BLACK;
    if (clearScreen) g_luaCanvas.fillScreen(0);
    if (fill) g_luaCanvas.fillCircle(cx, cy, r, canvasColor(color));
    else      g_luaCanvas.drawCircle(cx, cy, r, canvasColor(color));
    luaFlushOrDefer();
    return 0;
}

static int luaOnionDisplayTriangle(lua_State* L) {
    int x0 = (int)luaL_checkinteger(L, 1), y0 = (int)luaL_checkinteger(L, 2);
    int x1 = (int)luaL_checkinteger(L, 3), y1 = (int)luaL_checkinteger(L, 4);
    int x2 = (int)luaL_checkinteger(L, 5), y2 = (int)luaL_checkinteger(L, 6);
    bool clearScreen = luaDisplayClearArg(L, 7, false);
    bool fill  = lua_istable(L, 7) ? luaTableBool(L, 7, "fill",  false) : false;
    uint16_t color = lua_istable(L, 7)
        ? displayColorFromName(luaTableString(L, 7, "color", "black"), GxEPD_BLACK)
        : GxEPD_BLACK;
    if (clearScreen) g_luaCanvas.fillScreen(0);
    if (fill) g_luaCanvas.fillTriangle(x0, y0, x1, y1, x2, y2, canvasColor(color));
    else      g_luaCanvas.drawTriangle(x0, y0, x1, y1, x2, y2, canvasColor(color));
    luaFlushOrDefer();
    return 0;
}

static int luaOnionDisplayRoundRect(lua_State* L) {
    int x = (int)luaL_checkinteger(L, 1), y = (int)luaL_checkinteger(L, 2);
    int w = (int)luaL_checkinteger(L, 3), h = (int)luaL_checkinteger(L, 4);
    int r = (int)luaL_checkinteger(L, 5);
    bool clearScreen = luaDisplayClearArg(L, 6, false);
    bool fill  = lua_istable(L, 6) ? luaTableBool(L, 6, "fill",  false) : false;
    uint16_t color = lua_istable(L, 6)
        ? displayColorFromName(luaTableString(L, 6, "color", "black"), GxEPD_BLACK)
        : GxEPD_BLACK;
    if (clearScreen) g_luaCanvas.fillScreen(0);
    if (fill) g_luaCanvas.fillRoundRect(x, y, w, h, r, canvasColor(color));
    else      g_luaCanvas.drawRoundRect(x, y, w, h, r, canvasColor(color));
    luaFlushOrDefer();
    return 0;
}

static int luaOnionDisplayPixel(lua_State* L) {
    int x = (int)luaL_checkinteger(L, 1);
    int y = (int)luaL_checkinteger(L, 2);
    bool clearScreen = luaDisplayClearArg(L, 3, false);
    uint16_t color = lua_istable(L, 3)
        ? displayColorFromName(luaTableString(L, 3, "color", "black"), GxEPD_BLACK)
        : GxEPD_BLACK;
    if (clearScreen) g_luaCanvas.fillScreen(0);
    g_luaCanvas.drawPixel(x, y, canvasColor(color));
    luaFlushOrDefer();
    return 0;
}

static int luaOnionKvList(lua_State* L) {
    lua_newtable(L);
    return 1;
}

// ── Timing helpers ────────────────────────────────────────────────────────────

static int luaOnionMillis(lua_State* L) {
    lua_pushinteger(L, (lua_Integer)millis());
    return 1;
}

static int luaOnionTime(lua_State* L) {
    time_t t = time(nullptr);
    if (t < 1000000000L) t = 0;
    lua_pushinteger(L, (lua_Integer)t);
    return 1;
}

static int luaOnionTimeSynced(lua_State* L) {
    lua_pushboolean(L, sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED);
    return 1;
}

static int luaOnionDisplayBitmap(lua_State* L) {
    String name = luaL_checkstring(L, 1);
    int x = (int)luaL_optinteger(L, 2, 0);
    int y = (int)luaL_optinteger(L, 3, 0);
    bool clearScreen = lua_gettop(L) < 4 || lua_toboolean(L, 4);

    LoadedBitmap bitmap;
    String error;
    if (!loadStoredBitmap(name, bitmap, error)) {
        lua_pushboolean(L, false);
        lua_pushstring(L, error.c_str());
        setLog(error);
        return 2;
    }

    if (x < 0) x = (display.width() - bitmap.width) / 2;
    if (y < 0) y = (display.height() - bitmap.height) / 2;
    renderBitmap(bitmap, x, y, clearScreen);
    lua_pushboolean(L, true);
    return 1;
}

static int luaOnionImages(lua_State* L) {
    lua_newtable(L);

    File root = SPIFFS.open("/");
    if (!root) return 1;

    int index = 1;
    File file = root.openNextFile();
    while (file) {
        String name = normalizedSpiffsPath(file.name());
        if (!file.isDirectory() && name.startsWith("/images_")) {
            name.remove(0, 8);
            if (validImageFileName(name)) {
                lua_pushstring(L, name.c_str());
                lua_rawseti(L, -2, index++);
            }
        }
        file = root.openNextFile();
    }

    return 1;
}

static bool luaButtonMaskForName(const char* name, uint8_t& mask) {
    for (const LuaButton& button : kLuaButtons) {
        if (strcmp(name, button.name) == 0) {
            mask = button.mask;
            return true;
        }
    }
    return false;
}

static int luaOnionButtonMask(lua_State* L) {
    const char* name = luaL_checkstring(L, 1);
    uint8_t mask = 0;
    if (!luaButtonMaskForName(name, mask)) {
        lua_pushnil(L);
        lua_pushfstring(L, "Bad button name: %s", name);
        return 2;
    }
    lua_pushinteger(L, mask);
    return 1;
}

static int luaOnionButtons(lua_State* L) {
    uint8_t buttons = readButtons();
    lua_newtable(L);

    lua_pushinteger(L, buttons);
    lua_setfield(L, -2, "mask");

    for (const LuaButton& button : kLuaButtons) {
        lua_pushboolean(L, (buttons & button.mask) != 0);
        lua_setfield(L, -2, button.name);
    }

    return 1;
}

static int luaOnionSleep(lua_State* L) {
    uint32_t ms = (uint32_t)luaL_optinteger(L, 1, 0);
    if (ms > LUA_SLEEP_MAX_MS) ms = LUA_SLEEP_MAX_MS;
    delay(ms);
    return 0;
}

static int luaOnionGpioRead(lua_State* L) {
    int pin = (int)luaL_checkinteger(L, 1);
    if (!luaConfigureInputGpio(L, pin, 2)) return 2;

    lua_pushinteger(L, digitalRead(pin) == HIGH ? 1 : 0);
    return 1;
}

static int luaOnionGpioPoll(lua_State* L) {
    int pin = (int)luaL_checkinteger(L, 1);
    int target = (int)luaL_checkinteger(L, 2) ? 1 : 0;
    uint32_t timeoutMs = (uint32_t)luaL_optinteger(L, 3, 1000);
    uint32_t intervalMs = (uint32_t)luaL_optinteger(L, 4, 25);
    if (timeoutMs > LUA_GPIO_POLL_MAX_MS) timeoutMs = LUA_GPIO_POLL_MAX_MS;
    if (intervalMs < 1) intervalMs = 1;
    if (intervalMs > 1000) intervalMs = 1000;
    if (!luaConfigureInputGpio(L, pin, 5)) return 2;

    uint32_t start = millis();
    int value = digitalRead(pin) == HIGH ? 1 : 0;
    while ((uint32_t)(millis() - start) <= timeoutMs) {
        value = digitalRead(pin) == HIGH ? 1 : 0;
        if (value == target) {
            lua_pushboolean(L, true);
            lua_pushinteger(L, value);
            lua_pushinteger(L, (lua_Integer)(millis() - start));
            return 3;
        }
        delay(intervalMs);
    }

    lua_pushboolean(L, false);
    lua_pushinteger(L, value);
    lua_pushinteger(L, (lua_Integer)(millis() - start));
    return 3;
}

static int luaOnionEspNowStart(lua_State* L) {
    int channel = (int)luaL_optinteger(L, 1, 0);
    String error;
    if (!ensureEspNow(channel, error)) {
        lua_pushboolean(L, false);
        lua_pushstring(L, error.c_str());
        return 2;
    }
    setLog("ESP-NOW ready");
    lua_pushboolean(L, true);
    return 1;
}

static int luaOnionEspNowStop(lua_State* L) {
    if (g_espnowStarted) {
        esp_now_unregister_recv_cb();
        esp_now_unregister_send_cb();
        esp_now_deinit();
        g_espnowStarted = false;
    }
    portENTER_CRITICAL(&g_espnowMux);
    g_espnowQueueHead = 0;
    g_espnowQueueCount = 0;
    portEXIT_CRITICAL(&g_espnowMux);
    lua_pushboolean(L, true);
    return 1;
}

static int luaOnionEspNowMac(lua_State* L) {
    uint8_t mac[6] = {};
    esp_wifi_get_mac(WIFI_IF_STA, mac);
    String macText = macToString(mac);
    lua_pushstring(L, macText.c_str());
    return 1;
}

static int luaOnionEspNowInfo(lua_State* L) {
    uint8_t mac[6] = {};
    uint8_t channel = 0;
    wifi_second_chan_t second = WIFI_SECOND_CHAN_NONE;
    esp_wifi_get_mac(WIFI_IF_STA, mac);
    esp_wifi_get_channel(&channel, &second);

    lua_newtable(L);
    lua_pushboolean(L, g_espnowStarted);
    lua_setfield(L, -2, "started");
    String macText = macToString(mac);
    lua_pushstring(L, macText.c_str());
    lua_setfield(L, -2, "mac");
    lua_pushinteger(L, channel);
    lua_setfield(L, -2, "channel");
    lua_pushinteger(L, (lua_Integer)g_espnowSent);
    lua_setfield(L, -2, "sent");
    lua_pushinteger(L, (lua_Integer)g_espnowReceived);
    lua_setfield(L, -2, "received");
    portENTER_CRITICAL(&g_espnowMux);
    lua_pushinteger(L, g_espnowQueueCount);
    portEXIT_CRITICAL(&g_espnowMux);
    lua_setfield(L, -2, "queued");
    return 1;
}

static int luaOnionEspNowSend(lua_State* L) {
    size_t len = 0;
    const char* payload = luaL_checklstring(L, 1, &len);
    if (len == 0 || len > ONION_ESPNOW_MAX_PAYLOAD) {
        lua_pushboolean(L, false);
        lua_pushfstring(L, "ESP-NOW payload must be 1-%d bytes", ONION_ESPNOW_MAX_PAYLOAD);
        return 2;
    }

    uint8_t mac[6];
    memcpy(mac, kEspNowBroadcastMac, sizeof(mac));
    if (lua_gettop(L) >= 2 && !lua_isnil(L, 2)) {
        if (!parseMacString(luaL_checkstring(L, 2), mac)) {
            lua_pushboolean(L, false);
            lua_pushstring(L, "Bad ESP-NOW MAC");
            return 2;
        }
    }

    String error;
    if (!ensureEspNow(0, error) || !espnowAddPeer(mac, 0, error)) {
        lua_pushboolean(L, false);
        lua_pushstring(L, error.c_str());
        return 2;
    }

    esp_err_t rc = esp_now_send(mac, reinterpret_cast<const uint8_t*>(payload), len);
    if (rc != ESP_OK) {
        lua_pushboolean(L, false);
        lua_pushfstring(L, "ESP-NOW send failed %d", (int)rc);
        return 2;
    }
    lua_pushboolean(L, true);
    return 1;
}

static int luaOnionEspNowReceive(lua_State* L) {
    uint32_t timeoutMs = (uint32_t)luaL_optinteger(L, 1, 0);
    if (timeoutMs > LUA_ESPNOW_RECV_MAX_MS) timeoutMs = LUA_ESPNOW_RECV_MAX_MS;

    String error;
    if (!ensureEspNow(0, error)) {
        lua_pushnil(L);
        lua_pushstring(L, error.c_str());
        return 2;
    }

    uint32_t start = millis();
    EspNowQueuedMessage msg;
    while (!espnowQueuePop(msg)) {
        if ((uint32_t)(millis() - start) >= timeoutMs) {
            lua_pushnil(L);
            return 1;
        }
        delay(10);
    }

    lua_newtable(L);
    String macText = macToString(msg.mac);
    lua_pushstring(L, macText.c_str());
    lua_setfield(L, -2, "mac");
    lua_pushlstring(L, msg.payload, msg.len);
    lua_setfield(L, -2, "payload");
    lua_pushlstring(L, msg.payload, msg.len);
    lua_setfield(L, -2, "message");
    lua_pushinteger(L, msg.len);
    lua_setfield(L, -2, "len");
    lua_pushinteger(L, msg.rssi);
    lua_setfield(L, -2, "rssi");
    lua_pushinteger(L, (lua_Integer)msg.receivedAt);
    lua_setfield(L, -2, "received_at");
    return 1;
}

// onion.espnow_set_peer_key(mac, key16) enables radio AES-128 (LMK) for one
// unicast peer; key=nil reverts the peer to plaintext. Both sides must set the
// same key or unicast frames are silently dropped by the radio, so scripts
// should confirm the encrypted path still answers and fall back if it goes
// quiet.
static int luaOnionEspNowSetPeerKey(lua_State* L) {
    uint8_t mac[6];
    if (!parseMacString(luaL_checkstring(L, 1), mac)) {
        lua_pushboolean(L, false);
        lua_pushstring(L, "Bad ESP-NOW MAC");
        return 2;
    }
    if (memcmp(mac, kEspNowBroadcastMac, sizeof(mac)) == 0) {
        lua_pushboolean(L, false);
        lua_pushstring(L, "Broadcast peer cannot be encrypted");
        return 2;
    }
    bool clear = lua_isnoneornil(L, 2);
    const char* key = nullptr;
    if (!clear) {
        size_t keyLen = 0;
        key = luaL_checklstring(L, 2, &keyLen);
        if (keyLen != ESP_NOW_KEY_LEN) {
            lua_pushboolean(L, false);
            lua_pushfstring(L, "ESP-NOW key must be %d bytes", ESP_NOW_KEY_LEN);
            return 2;
        }
    }

    String error;
    if (!ensureEspNow(0, error) || !espnowAddPeer(mac, 0, error)) {
        lua_pushboolean(L, false);
        lua_pushstring(L, error.c_str());
        return 2;
    }

    esp_now_peer_info_t peer = {};
    esp_err_t rc = esp_now_get_peer(mac, &peer);
    if (rc != ESP_OK) {
        lua_pushboolean(L, false);
        lua_pushfstring(L, "ESP-NOW get peer failed %d", (int)rc);
        return 2;
    }
    if (clear) {
        peer.encrypt = false;
        memset(peer.lmk, 0, sizeof(peer.lmk));
    } else {
        memcpy(peer.lmk, key, ESP_NOW_KEY_LEN);
        peer.encrypt = true;
    }
    rc = esp_now_mod_peer(&peer);
    if (rc != ESP_OK) {
        lua_pushboolean(L, false);
        lua_pushfstring(L, "ESP-NOW mod peer failed %d", (int)rc);
        return 2;
    }
    Serial.printf("[onion-os] ESP-NOW peer %s encrypt=%d\n",
                  macToString(peer.peer_addr).c_str(), peer.encrypt ? 1 : 0);
    lua_pushboolean(L, true);
    return 1;
}

// --- Lua KV store / crypto --------------------------------------------------
// Small script-facing primitives so downloadable Lua apps can keep state and
// do authenticated pairing without custom firmware: NVS key/value blobs in a
// namespace separate from the protected onion-os config, and SHA-256
// (libsodium, already linked for the wallet).

static Preferences g_luaPrefs;
static bool g_luaPrefsOpen = false;

static bool ensureLuaPrefs() {
    if (!g_luaPrefsOpen) g_luaPrefsOpen = g_luaPrefs.begin("lua-kv", false);
    return g_luaPrefsOpen;
}

// NVS limits keys to 15 chars; values are stored as binary-safe blobs.
static bool luaKvCheckKey(lua_State* L, const char** outKey) {
    size_t klen = 0;
    const char* key = luaL_checklstring(L, 1, &klen);
    if (klen == 0 || klen > 15) {
        lua_pushnil(L);
        lua_pushstring(L, "kv key must be 1-15 chars");
        return false;
    }
    *outKey = key;
    return true;
}

static int luaOnionKvSet(lua_State* L) {
    const char* key = nullptr;
    if (!luaKvCheckKey(L, &key)) return 2;
    if (!ensureLuaPrefs()) {
        lua_pushnil(L);
        lua_pushstring(L, "kv storage unavailable");
        return 2;
    }
    if (lua_isnoneornil(L, 2)) {
        g_luaPrefs.remove(key);
        lua_pushboolean(L, true);
        return 1;
    }
    size_t vlen = 0;
    const char* value = luaL_checklstring(L, 2, &vlen);
    if (vlen == 0 || vlen > LUA_KV_MAX_VALUE) {
        lua_pushnil(L);
        lua_pushfstring(L, "kv value must be 1-%d bytes", (int)LUA_KV_MAX_VALUE);
        return 2;
    }
    if (g_luaPrefs.putBytes(key, value, vlen) != vlen) {
        lua_pushnil(L);
        lua_pushstring(L, "kv write failed");
        return 2;
    }
    lua_pushboolean(L, true);
    return 1;
}

static int luaOnionKvGet(lua_State* L) {
    const char* key = nullptr;
    if (!luaKvCheckKey(L, &key)) return 2;
    if (!ensureLuaPrefs() || !g_luaPrefs.isKey(key)) {
        lua_pushnil(L);
        return 1;
    }
    size_t len = g_luaPrefs.getBytesLength(key);
    if (len == 0 || len > LUA_KV_MAX_VALUE) {
        lua_pushnil(L);
        return 1;
    }
    char buf[LUA_KV_MAX_VALUE];
    g_luaPrefs.getBytes(key, buf, len);
    lua_pushlstring(L, buf, len);
    return 1;
}

static int luaOnionKvDel(lua_State* L) {
    const char* key = nullptr;
    if (!luaKvCheckKey(L, &key)) return 2;
    if (!ensureLuaPrefs()) {
        lua_pushnil(L);
        lua_pushstring(L, "kv storage unavailable");
        return 2;
    }
    g_luaPrefs.remove(key);
    lua_pushboolean(L, true);
    return 1;
}

// onion.sha256(data) -> 32 raw bytes. Binary-safe input.
static int luaOnionSha256(lua_State* L) {
    size_t len = 0;
    const char* data = luaL_checklstring(L, 1, &len);
    unsigned char hash[crypto_hash_sha256_BYTES];
    crypto_hash_sha256(hash, reinterpret_cast<const unsigned char*>(data), len);
    lua_pushlstring(L, reinterpret_cast<const char*>(hash), sizeof(hash));
    return 1;
}

// --- Lua WiFi association control (fixed-channel ESP-NOW) -------------------
// ESP-NOW shares the one radio with the WiFi STA. While associated to an AP
// the radio is pinned to that AP's channel, so badges that roamed to different
// APs (e.g. a multi-AP venue) land on different channels and cannot hear each
// other. onion.wifi_disconnect() frees the radio from the AP WITHOUT turning
// it off; the script can then onion.espnow_start(channel) to pin every badge
// to one agreed channel. Only the AP association (internet/MQTT) is dropped.
// onion.wifi_reconnect() restores normal connectivity (ensureWifi also undoes
// the LR PHY automatically once the script exits and the main loop resumes).

static int luaOnionWifiDisconnect(lua_State* L) {
    WiFi.setAutoReconnect(false); // stop the Arduino layer racing us back on
    esp_wifi_disconnect();        // leave the AP; radio stays up in STA mode
    // 802.11 LR (Espressif long-range PHY, ~250 kbps) gives ~10 dB better RX
    // sensitivity, roughly 2-4x ESP-NOW range badge-to-badge. It is only legal
    // while not associated (LR cannot talk to a normal AP); every reconnect
    // path restores B/G/N first via restoreWifiProtocol().
    esp_err_t lrRc = esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_LR);
    if (lrRc != ESP_OK) {
        Serial.printf("[onion-os] LR protocol set failed %d; default PHY\n", (int)lrRc);
    }
    lua_pushboolean(L, true);
    return 1;
}

static int luaOnionWifiReconnect(lua_State* L) {
    restoreWifiProtocol();
    WiFi.setAutoReconnect(true);
    if (g_config.wifiSsid.length()) {
        WiFi.begin(g_config.wifiSsid.c_str(), g_config.wifiPassword.c_str());
    }
    lua_pushboolean(L, true);
    return 1;
}

void registerOnionLua(lua_State* L) {
    lua_newtable(L);
    lua_pushcfunction(L, luaOnionLog);
    lua_setfield(L, -2, "log");
    lua_pushcfunction(L, luaOnionHardwareId);
    lua_setfield(L, -2, "hardware_id");
    lua_pushcfunction(L, luaOnionOnionId);
    lua_setfield(L, -2, "onion_id");
    lua_pushcfunction(L, luaOnionWallet);
    lua_setfield(L, -2, "wallet");
    lua_pushcfunction(L, luaOnionUsername);
    lua_setfield(L, -2, "username");
    lua_pushcfunction(L, luaOnionSecureRandom);
    lua_setfield(L, -2, "secure_random");
    lua_pushcfunction(L, luaOnionSha256);
    lua_setfield(L, -2, "sha256");
    lua_pushcfunction(L, luaOnionKvSet);
    lua_setfield(L, -2, "kv_set");
    lua_pushcfunction(L, luaOnionKvGet);
    lua_setfield(L, -2, "kv_get");
    lua_pushcfunction(L, luaOnionKvDel);
    lua_setfield(L, -2, "kv_del");
    lua_pushcfunction(L, luaOnionHttpGet);
    lua_setfield(L, -2, "http_get");
    lua_pushcfunction(L, luaOnionHttpPost);
    lua_setfield(L, -2, "http_post");
    lua_pushcfunction(L, luaOnionMqttConnected);
    lua_setfield(L, -2, "mqtt_connected");
    lua_pushcfunction(L, luaOnionMqttSubscribe);
    lua_setfield(L, -2, "mqtt_subscribe");
    lua_pushcfunction(L, luaOnionMqttUnsubscribe);
    lua_setfield(L, -2, "mqtt_unsubscribe");
    lua_pushcfunction(L, luaOnionMqttPublish);
    lua_setfield(L, -2, "mqtt_publish");
    lua_pushcfunction(L, luaOnionMqttReceive);
    lua_setfield(L, -2, "mqtt_receive");
    lua_pushcfunction(L, luaOnionMqttInfo);
    lua_setfield(L, -2, "mqtt_info");
    lua_pushcfunction(L, luaOnionSubghzBegin);
    lua_setfield(L, -2, "subghz_begin");
    lua_pushcfunction(L, luaOnionSubghzTransmit);
    lua_setfield(L, -2, "subghz_transmit");
    lua_pushcfunction(L, luaOnionSubghzReceive);
    lua_setfield(L, -2, "subghz_receive");
    lua_pushcfunction(L, luaOnionSubghzSetFrequency);
    lua_setfield(L, -2, "subghz_set_frequency");
    lua_pushcfunction(L, luaOnionSubghzInfo);
    lua_setfield(L, -2, "subghz_info");
    lua_pushcfunction(L, luaOnionSubghzEnd);
    lua_setfield(L, -2, "subghz_end");
    lua_pushcfunction(L, luaOnionSoundSpeakerBegin);
    lua_setfield(L, -2, "sound_speaker_begin");
    lua_pushcfunction(L, luaOnionSoundPlayTone);
    lua_setfield(L, -2, "sound_play_tone");
    lua_pushcfunction(L, luaOnionSoundPlay);
    lua_setfield(L, -2, "sound_play");
    lua_pushcfunction(L, luaOnionSoundSpeakerEnd);
    lua_setfield(L, -2, "sound_speaker_end");
    lua_pushcfunction(L, luaOnionSoundMicBegin);
    lua_setfield(L, -2, "sound_mic_begin");
    lua_pushcfunction(L, luaOnionSoundMicRead);
    lua_setfield(L, -2, "sound_mic_read");
    lua_pushcfunction(L, luaOnionSoundMicLevel);
    lua_setfield(L, -2, "sound_mic_level");
    lua_pushcfunction(L, luaOnionSoundMicEnd);
    lua_setfield(L, -2, "sound_mic_end");
    lua_pushcfunction(L, luaOnionDisplaySize);
    lua_setfield(L, -2, "display_size");
    lua_pushcfunction(L, luaOnionClearDisplay);
    lua_setfield(L, -2, "clear_display");
    lua_pushcfunction(L, luaOnionReleaseDisplay);
    lua_setfield(L, -2, "release_display");
    lua_pushcfunction(L, luaOnionDisplayText);
    lua_setfield(L, -2, "display_text");
    lua_pushcfunction(L, luaOnionDisplayLines);
    lua_setfield(L, -2, "display_lines");
    lua_pushcfunction(L, luaOnionDisplayLine);
    lua_setfield(L, -2, "display_line");
    lua_pushcfunction(L, luaOnionDisplayRect);
    lua_setfield(L, -2, "display_rect");
    lua_pushcfunction(L, luaOnionDisplayBuffer);
    lua_setfield(L, -2, "display_buffer");
    lua_pushcfunction(L, luaOnionDisplayBegin);
    lua_setfield(L, -2, "display_begin");
    lua_pushcfunction(L, luaOnionDisplayCommit);
    lua_setfield(L, -2, "display_commit");
    lua_pushcfunction(L, luaOnionDisplayFlush);
    lua_setfield(L, -2, "display_flush");
    lua_pushcfunction(L, luaOnionDisplayCircle);
    lua_setfield(L, -2, "display_circle");
    lua_pushcfunction(L, luaOnionDisplayTriangle);
    lua_setfield(L, -2, "display_triangle");
    lua_pushcfunction(L, luaOnionDisplayRoundRect);
    lua_setfield(L, -2, "display_round_rect");
    lua_pushcfunction(L, luaOnionDisplayPixel);
    lua_setfield(L, -2, "display_pixel");
    lua_pushcfunction(L, luaOnionDisplayBitmap);
    lua_setfield(L, -2, "display_bitmap");
    lua_pushcfunction(L, luaOnionKvDel);
    lua_setfield(L, -2, "kv_delete");
    lua_pushcfunction(L, luaOnionKvList);
    lua_setfield(L, -2, "kv_list");
    lua_pushcfunction(L, luaOnionMillis);
    lua_setfield(L, -2, "millis");
    lua_pushcfunction(L, luaOnionTime);
    lua_setfield(L, -2, "time");
    lua_pushcfunction(L, luaOnionTimeSynced);
    lua_setfield(L, -2, "time_synced");
    lua_pushcfunction(L, luaOnionImages);
    lua_setfield(L, -2, "images");
    lua_pushcfunction(L, luaOnionButtons);
    lua_setfield(L, -2, "buttons");
    lua_pushcfunction(L, luaOnionButtonMask);
    lua_setfield(L, -2, "button_mask");
    lua_pushcfunction(L, luaOnionSleep);
    lua_setfield(L, -2, "sleep");
    lua_pushcfunction(L, luaOnionGpioRead);
    lua_setfield(L, -2, "gpio_read");
    lua_pushcfunction(L, luaOnionGpioPoll);
    lua_setfield(L, -2, "gpio_poll");
    lua_pushcfunction(L, luaOnionEspNowStart);
    lua_setfield(L, -2, "espnow_start");
    lua_pushcfunction(L, luaOnionEspNowStop);
    lua_setfield(L, -2, "espnow_stop");
    lua_pushcfunction(L, luaOnionEspNowMac);
    lua_setfield(L, -2, "espnow_mac");
    lua_pushcfunction(L, luaOnionEspNowInfo);
    lua_setfield(L, -2, "espnow_info");
    lua_pushcfunction(L, luaOnionEspNowSend);
    lua_setfield(L, -2, "espnow_send");
    lua_pushcfunction(L, luaOnionEspNowReceive);
    lua_setfield(L, -2, "espnow_receive");
    lua_pushcfunction(L, luaOnionEspNowSetPeerKey);
    lua_setfield(L, -2, "espnow_set_peer_key");
    lua_pushcfunction(L, luaOnionWifiDisconnect);
    lua_setfield(L, -2, "wifi_disconnect");
    lua_pushcfunction(L, luaOnionWifiReconnect);
    lua_setfield(L, -2, "wifi_reconnect");
    lua_setglobal(L, "onion");
}

static void* luaHeapAllocator(void*, void* ptr, size_t, size_t nsize) {
    if (nsize == 0) {
        heap_caps_free(ptr);
        return nullptr;
    }
    void* out = heap_caps_realloc(ptr, nsize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!out) out = heap_caps_realloc(ptr, nsize, MALLOC_CAP_8BIT);
    return out;
}

lua_State* newLuaState() {
    return lua_newstate(luaHeapAllocator, nullptr, esp_random());
}

void cleanupLuaRuntime() {
    luaMqttResetSubs();
    moduleShutdownActive();
    if (g_luaFramePending) { g_luaFramePending = false; refreshLuaCanvas(); }
    g_luaDeferFlush = false;
}

static bool finishLuaRun(lua_State* L, const String& name, int status, const String& logBeforeRun) {
    if (status == LUA_OK) status = lua_pcall(L, 0, 0, 0);
    if (status != LUA_OK) {
        const char* luaErr = lua_tostring(L, -1);
        String err = luaErr ? String(luaErr) : String("unknown Lua error");
        g_lastLuaError = err;
        lua_close(L);
        cleanupLuaRuntime();
        // Full error to serial; the e-paper status line only fits a stub.
        Serial.printf("[onion-os] Lua error in %s: %s\n", name.c_str(), err.c_str());
        setLog("Lua error: " + clipped(err, 22));
        return false;
    }

    lua_close(L);
    cleanupLuaRuntime();
    if (g_luaDisplayActive) {
        Serial.printf("[onion-os] Lua ran %s\n", name.c_str());
    } else {
        if (g_log == logBeforeRun) setLog("Lua ran " + name);
    }
    return true;
}

static bool prepareLuaState(lua_State*& L) {
    L = newLuaState();
    if (!L) {
        g_lastLuaError = "Lua state failed";
        setLog("Lua state failed");
        return false;
    }
    luaL_openlibs(L);
    registerOnionLua(L);
    return true;
}

bool runLuaBuffer(const char* source, size_t sourceLen, const String& name) {
    g_lastLuaError = "";
    lua_State* L = nullptr;
    if (!prepareLuaState(L)) return false;
    String logBeforeRun = g_log;
    int status = luaL_loadbuffer(L, source, sourceLen, name.c_str());
    return finishLuaRun(L, name, status, logBeforeRun);
}

bool runLuaSource(const String& source, const String& name) {
    return runLuaBuffer(source.c_str(), source.length(), name);
}

struct LuaFileReader {
    File* file;
    char buffer[512];
};

static const char* luaFileReader(lua_State*, void* data, size_t* size) {
    LuaFileReader* reader = (LuaFileReader*)data;
    if (!reader || !reader->file || !*reader->file) {
        *size = 0;
        return nullptr;
    }
    size_t n = reader->file->read((uint8_t*)reader->buffer, sizeof(reader->buffer));
    if (n == 0) {
        *size = 0;
        return nullptr;
    }
    *size = n;
    return reader->buffer;
}

bool runStoredScript(const String& path) {
    g_lastLuaError = "";
    File file = SPIFFS.open(path, FILE_READ);
    if (!file) {
        g_lastLuaError = "Lua script missing: " + path;
        setLog("Lua script missing");
        return false;
    }
    size_t size = file.size();
    if (size > MAX_SCRIPT_BYTES) {
        file.close();
        g_lastLuaError = "Lua script too large: " + String(size) + " bytes";
        setLog("Lua script too large");
        return false;
    }

    lua_State* L = nullptr;
    if (!prepareLuaState(L)) {
        file.close();
        return false;
    }

    String logBeforeRun = g_log;
    LuaFileReader reader = {&file, {}};
    int status = lua_load(L, luaFileReader, &reader, path.c_str(), nullptr);
    file.close();
    return finishLuaRun(L, path, status, logBeforeRun);
}

void runScriptByName(const String& name) {
    if (!name.length() || name.indexOf('/') >= 0) {
        setLog("Bad script name");
        return;
    }
    runStoredScript("/scripts_" + name);
}

bool validScriptFileName(const String& name) {
    return validAssetFileName(name, ".lua");
}

bool validStoredScriptPath(const String& path) {
    if (!path.startsWith("/scripts_") || !path.endsWith(".lua")) return false;
    return validScriptFileName(storedScriptDisplayName(path));
}

bool deleteStoredScript(const String& path) {
    if (!validStoredScriptPath(path)) {
        setLog("Bad script path");
        return false;
    }
    String name = storedScriptDisplayName(path);
    if (!SPIFFS.remove(path)) {
        setLog("Lua delete failed");
        return false;
    }
    setLog("Deleted " + clipped(name, 20));
    return true;
}

void deleteScriptByName(const String& name) {
    if (!validScriptFileName(name)) {
        setLog("Bad script name");
        return;
    }
    deleteStoredScript("/scripts_" + name);
}

static String pushedScriptFileName() {
    if (validScriptFileName(g_luaPrompt.fileName)) return g_luaPrompt.fileName;
    String suffix = g_luaPrompt.scriptId.length() ? g_luaPrompt.scriptId : g_luaPrompt.requestId;
    String safe;
    safe.reserve(64);
    for (size_t i = 0; i < suffix.length() && safe.length() < 64; ++i) {
        char ch = suffix[i];
        bool ok = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
            (ch >= '0' && ch <= '9') || ch == '-' || ch == '_';
        safe += ok ? ch : '_';
    }
    if (!safe.length()) safe = "script";
    return "pushed_" + safe + ".lua";
}

static bool copySpiffsFile(const String& srcPath, const String& dstPath) {
    File src = SPIFFS.open(srcPath, FILE_READ);
    if (!src) return false;
    SPIFFS.remove(dstPath);
    File dst = SPIFFS.open(dstPath, FILE_WRITE);
    if (!dst) {
        src.close();
        return false;
    }
    uint8_t buf[512];
    bool ok = true;
    while (src.available()) {
        size_t n = src.read(buf, sizeof(buf));
        if (n == 0) break;
        if (dst.write(buf, n) != n) {
            ok = false;
            break;
        }
    }
    src.close();
    dst.close();
    if (!ok) SPIFFS.remove(dstPath);
    return ok;
}

static bool stringIsDigits(const String& value) {
    if (!value.length()) return false;
    for (size_t i = 0; i < value.length(); i++) {
        if (value[i] < '0' || value[i] > '9') return false;
    }
    return true;
}

static String luaInstallError() {
    if (!g_lastLuaError.length()) return "Lua runtime error";

    int firstColon = g_lastLuaError.indexOf(':');
    int secondColon = firstColon >= 0 ? g_lastLuaError.indexOf(':', firstColon + 1) : -1;
    if (firstColon >= 0 && secondColon > firstColon) {
        String line = g_lastLuaError.substring(firstColon + 1, secondColon);
        if (stringIsDigits(line)) {
            String message = g_lastLuaError.substring(secondColon + 1);
            message.trim();
            return "Lua line " + line + ": " + clipped(message, 160);
        }
    }

    return "Lua: " + clipped(g_lastLuaError, 180);
}

bool installAndRunPushedScript(String& error) {
    if (!g_luaPrompt.requestId.length()) {
        error = "Missing Lua request";
        return false;
    }
    bool fileBacked = g_luaPrompt.codePath.length() > 0;
    bool httpBacked = g_luaPrompt.downloadUrl.length() > 0;
    if (!httpBacked && !fileBacked && !g_luaPrompt.code.length()) {
        error = "Empty Lua script";
        return false;
    }
    if ((!httpBacked && !fileBacked && g_luaPrompt.code.length() > MAX_SCRIPT_BYTES) ||
        g_luaPrompt.sizeBytes > MAX_SCRIPT_BYTES) {
        error = "Lua script too large";
        return false;
    }

    String name = pushedScriptFileName();
    String path = "/scripts_" + name;
    if (httpBacked) {
        String url = resolveServerUrl(g_luaPrompt.downloadUrl);
        SPIFFS.remove(path);
        if (!downloadScriptFile(url, path)) {
            error = "Lua download failed";
            return false;
        }
        if (!runStoredScript(path)) {
            error = luaInstallError();
            return false;
        }
        return true;
    }

    if (fileBacked) {
        File src = SPIFFS.open(g_luaPrompt.codePath, FILE_READ);
        if (!src) {
            error = "Lua script missing";
            return false;
        }
        size_t scriptSize = src.size();
        src.close();
        if (scriptSize == 0) {
            error = "Empty Lua script";
            return false;
        }
        if (scriptSize > MAX_SCRIPT_BYTES) {
            error = "Lua script too large";
            return false;
        }
        SPIFFS.remove(path);
        if (!SPIFFS.rename(g_luaPrompt.codePath, path) && !copySpiffsFile(g_luaPrompt.codePath, path)) {
            error = "Lua write failed";
            return false;
        }
        if (!runStoredScript(path)) {
            error = luaInstallError();
            return false;
        }
        return true;
    }

    File file = SPIFFS.open(path, FILE_WRITE);
    if (!file) {
        error = "Lua write failed";
        return false;
    }
    size_t written = file.print(g_luaPrompt.code);
    file.close();
    if (written != g_luaPrompt.code.length()) {
        SPIFFS.remove(path);
        error = "Lua write short";
        return false;
    }

    if (!runLuaSource(g_luaPrompt.code, name)) {
        error = luaInstallError();
        return false;
    }
    return true;
}

bool sendLuaPushResponse(bool approved) {
    if (!g_identity.onionId || !g_luaPrompt.requestId.length()) {
        setLog("No Lua push active");
        return false;
    }

    String error;
    bool accepted = approved;
    if (approved && !installAndRunPushedScript(error)) {
        accepted = false;
    }
    if (!approved) error = "User denied";

    String body = "{\"onionId\":" + String((unsigned long long)g_identity.onionId) +
        ",\"requestId\":\"" + jsonEscape(g_luaPrompt.requestId) +
        "\",\"approved\":" + String(accepted ? "true" : "false");
    if (!accepted && error.length()) {
        body += ",\"error\":\"" + jsonEscape(error) + "\"";
    }
    body += "}";

    bool sentOverMqtt = publishMqtt(topic("badge/" + String((unsigned long long)g_identity.onionId) + "/lua/response"), body);
    String response;
    int code = sentOverMqtt ? 200 : httpPostJson("/api/badge/lua-response", body, &response);
    if (code >= 200 && code < 300) {
        setLog(accepted ? "Lua installed" : (error.length() ? error : "Lua denied"));
        g_luaPrompt.active = false;
        g_luaPrompt.code = "";
        g_luaPrompt.downloadUrl = "";
        if (g_luaPrompt.codePath.length()) {
            SPIFFS.remove(g_luaPrompt.codePath);
            g_luaPrompt.codePath = "";
        }
        g_screen = SCREEN_STATUS;
    } else {
        setLog("Lua response HTTP " + String(code));
    }
    return code >= 200 && code < 300;
}

void syncScripts() {
    if (!g_config.scriptManifestUrl.length()) {
        setLog("No script manifest URL");
        return;
    }
    if (!ensureWifi()) return;

    String payload;
    int code = httpGetString(g_config.scriptManifestUrl, &payload);

    if (code < 200 || code >= 300) {
        setLog("Manifest GET failed " + String(code));
        return;
    }

    cJSON* root = cJSON_Parse(payload.c_str());
    cJSON* scripts = root ? cJSON_GetObjectItemCaseSensitive(root, "scripts") : nullptr;
    cJSON* images = root ? cJSON_GetObjectItemCaseSensitive(root, "images") : nullptr;
    if (!cJSON_IsArray(scripts) && !cJSON_IsArray(images)) {
        if (root) cJSON_Delete(root);
        setLog("Bad script manifest");
        return;
    }

    int count = 0;
    if (cJSON_IsArray(scripts)) {
        cJSON* script = nullptr;
        cJSON_ArrayForEach(script, scripts) {
            String name = jsonString(script, "name");
            String url = jsonString(script, "url");
            bool autorun = jsonBool(script, "autorun", false);
            if (!validScriptFileName(name) || !url.length()) continue;
            String path = "/scripts_" + name;
            if (downloadScriptFile(url, path)) {
                count++;
                if (autorun) runStoredScript(path);
            }
        }
    }
    int imageCount = 0;
    if (cJSON_IsArray(images)) {
        cJSON* image = nullptr;
        cJSON_ArrayForEach(image, images) {
            String name = jsonString(image, "name");
            String url = jsonString(image, "url");
            if (!validImageFileName(name) || !url.length()) continue;
            if (downloadImageFile(url, imagePathForName(name))) imageCount++;
        }
    }
    cJSON_Delete(root);
    setLog("Synced S:" + String(count) + " I:" + String(imageCount));
}
