#include "http.h"
#include "badge_state.h"
#include "wifi.h"
#include <esp_http_client.h>
#include <esp_crt_bundle.h>
#include <SPIFFS.h>

esp_err_t httpCaptureEvent(esp_http_client_event_t* event) {
    if (event->event_id == HTTP_EVENT_ON_DATA && event->user_data && event->data && event->data_len > 0) {
        String* response = static_cast<String*>(event->user_data);
        response->concat(static_cast<const char*>(event->data), event->data_len);
    }
    return ESP_OK;
}

int httpPostJson(const String& path, const String& body, String* response) {
    if (!ensureWifi()) return -1;
    String url = g_config.serverBaseUrl;
    if (url.endsWith("/")) url.remove(url.length() - 1);
    url += path;

    String responseBuffer;
    esp_http_client_config_t cfg = {};
    cfg.url = url.c_str();
    cfg.timeout_ms = 10000;
    cfg.crt_bundle_attach = esp_crt_bundle_attach;
    cfg.event_handler = httpCaptureEvent;
    cfg.user_data = &responseBuffer;

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) return -1;
    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    if (g_config.badgeApiKey.length()) {
        String auth = "Bearer " + g_config.badgeApiKey;
        esp_http_client_set_header(client, "Authorization", auth.c_str());
    }
    esp_http_client_set_post_field(client, body.c_str(), body.length());
    esp_err_t err = esp_http_client_perform(client);
    int code = err == ESP_OK ? esp_http_client_get_status_code(client) : -1;
    if (response) *response = responseBuffer;
    esp_http_client_cleanup(client);
    return code;
}

int httpGetString(const String& url, String* response) {
    if (!ensureWifi()) return -1;
    String responseBuffer;
    esp_http_client_config_t cfg = {};
    cfg.url = url.c_str();
    cfg.timeout_ms = 10000;
    cfg.crt_bundle_attach = esp_crt_bundle_attach;
    cfg.event_handler = httpCaptureEvent;
    cfg.user_data = &responseBuffer;

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) return -1;
    esp_err_t err = esp_http_client_perform(client);
    int code = err == ESP_OK ? esp_http_client_get_status_code(client) : -1;
    if (response) *response = responseBuffer;
    esp_http_client_cleanup(client);
    return code;
}

String urlEncode(const String& value) {
    String out;
    out.reserve(value.length() + 8);
    for (size_t i = 0; i < value.length(); ++i) {
        char ch = value[i];
        bool safe = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                    (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' || ch == '.' || ch == '~';
        if (safe) {
            out += ch;
        } else {
            char buf[4];
            snprintf(buf, sizeof(buf), "%%%02X", (uint8_t)ch);
            out += buf;
        }
    }
    return out;
}

// ── cJSON-based JSON helpers ────────────────────────────────────────────────

String jsonString(cJSON* obj, const char* key) {
    cJSON* item = cJSON_GetObjectItemCaseSensitive(obj, key);
    return cJSON_IsString(item) && item->valuestring ? String(item->valuestring) : String();
}

String jsonValueString(cJSON* obj, const char* key, String& out) {
    cJSON* item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsString(item) && item->valuestring) {
        out = item->valuestring;
        return out;
    }
    if (cJSON_IsNumber(item)) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%.0f", item->valuedouble);
        out = buf;
        return out;
    }
    return String();
}

int jsonInt(cJSON* obj, const char* key, int fallback) {
    cJSON* item = cJSON_GetObjectItemCaseSensitive(obj, key);
    return cJSON_IsNumber(item) ? item->valueint : fallback;
}

bool jsonBool(cJSON* obj, const char* key, bool fallback) {
    cJSON* item = cJSON_GetObjectItemCaseSensitive(obj, key);
    return cJSON_IsBool(item) ? cJSON_IsTrue(item) : fallback;
}

uint64_t jsonUint64(cJSON* obj, const char* key, uint64_t fallback) {
    cJSON* item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsNumber(item)) return (uint64_t)item->valuedouble;
    if (cJSON_IsString(item) && item->valuestring) {
        char* end = nullptr;
        uint64_t value = strtoull(item->valuestring, &end, 10);
        return (end && *end == '\0') ? value : fallback;
    }
    return fallback;
}

// ── Raw string JSON parsing (no cJSON allocation) ──────────────────────────

int jsonHexNibble(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

bool appendUtf8Codepoint(String& out, uint32_t cp) {
    if (cp <= 0x7F) {
        out += (char)cp;
    } else if (cp <= 0x7FF) {
        out += (char)(0xC0 | (cp >> 6));
        out += (char)(0x80 | (cp & 0x3F));
    } else {
        out += (char)(0xE0 | (cp >> 12));
        out += (char)(0x80 | ((cp >> 6) & 0x3F));
        out += (char)(0x80 | (cp & 0x3F));
    }
    return true;
}

static bool writeUtf8CodepointToFile(File& out, uint32_t cp, size_t& written) {
    uint8_t bytes[3];
    size_t len = 0;
    if (cp <= 0x7F) {
        bytes[len++] = (uint8_t)cp;
    } else if (cp <= 0x7FF) {
        bytes[len++] = (uint8_t)(0xC0 | (cp >> 6));
        bytes[len++] = (uint8_t)(0x80 | (cp & 0x3F));
    } else {
        bytes[len++] = (uint8_t)(0xE0 | (cp >> 12));
        bytes[len++] = (uint8_t)(0x80 | ((cp >> 6) & 0x3F));
        bytes[len++] = (uint8_t)(0x80 | (cp & 0x3F));
    }
    if (out.write(bytes, len) != len) return false;
    written += len;
    return true;
}

static int jsonRawFieldStart(const String& json, const char* key) {
    String needle = "\"" + String(key) + "\"";
    int pos = json.indexOf(needle);
    while (pos >= 0) {
        int prev = pos - 1;
        while (prev >= 0 && (json[prev] == ' ' || json[prev] == '\n' || json[prev] == '\r' || json[prev] == '\t')) prev--;
        bool fieldBoundary = prev < 0 || json[prev] == '{' || json[prev] == ',';

        int i = pos + needle.length();
        while (i < (int)json.length() && (json[i] == ' ' || json[i] == '\n' || json[i] == '\r' || json[i] == '\t')) i++;
        if (fieldBoundary && i < (int)json.length() && json[i] == ':') {
            i++;
            while (i < (int)json.length() && (json[i] == ' ' || json[i] == '\n' || json[i] == '\r' || json[i] == '\t')) i++;
            return i;
        }

        pos = json.indexOf(needle, pos + 1);
    }
    return -1;
}

String jsonExtractStringField(const String& json, const char* fieldName) {
    String out;
    int i = jsonRawFieldStart(json, fieldName);
    if (i < 0 || i >= (int)json.length() || json[i] != '"') return out;
    i++;

    while (i < (int)json.length()) {
        char ch = json[i++];
        if (ch == '"') return out;
        if (ch != '\\') {
            out += ch;
            continue;
        }
        if (i >= (int)json.length()) return out;
        char esc = json[i++];
        switch (esc) {
            case '"': out += '"'; break;
            case '\\': out += '\\'; break;
            case '/': out += '/'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'u': {
                if (i + 4 > (int)json.length()) return out;
                uint32_t cp = 0;
                for (int n = 0; n < 4; n++) {
                    int v = jsonHexNibble(json[i++]);
                    if (v < 0) return out;
                    cp = (cp << 4) | (uint32_t)v;
                }
                appendUtf8Codepoint(out, cp);
                break;
            }
            default:
                return out;
        }
    }
    return out;
}

int jsonExtractIntField(const String& json, const char* fieldName, int fallback) {
    int i = jsonRawFieldStart(json, fieldName);
    if (i < 0 || i >= (int)json.length()) return fallback;
    bool neg = false;
    if (json[i] == '-') {
        neg = true;
        i++;
    }
    long value = 0;
    bool any = false;
    while (i < (int)json.length() && json[i] >= '0' && json[i] <= '9') {
        any = true;
        value = value * 10 + (json[i++] - '0');
        if (value > INT32_MAX) return fallback;
    }
    if (!any) return fallback;
    return neg ? -(int)value : (int)value;
}

static bool jsonFileSeekFieldValue(File& file, const char* key) {
    if (!file.seek(0, SeekSet)) return false;
    String needle = "\"" + String(key) + "\"";
    int matched = 0;
    while (file.available()) {
        char ch = (char)file.read();
        if (ch == needle[matched]) {
            matched++;
            if (matched == (int)needle.length()) {
                do {
                    if (!file.available()) return false;
                    ch = (char)file.read();
                } while (ch == ' ' || ch == '\n' || ch == '\r' || ch == '\t');
                if (ch != ':') {
                    matched = 0;
                    continue;
                }
                do {
                    if (!file.available()) return false;
                    ch = (char)file.peek();
                    if (ch == ' ' || ch == '\n' || ch == '\r' || ch == '\t') file.read();
                } while (ch == ' ' || ch == '\n' || ch == '\r' || ch == '\t');
                return true;
            }
        } else {
            matched = (ch == needle[0]) ? 1 : 0;
        }
    }
    return false;
}

String jsonExtractStringFieldFromFile(File& file, const char* fieldName) {
    String out;
    if (!jsonFileSeekFieldValue(file, fieldName) || file.read() != '"') return out;

    while (file.available()) {
        char ch = (char)file.read();
        if (ch == '"') return out;
        if (ch != '\\') {
            out += ch;
            continue;
        }
        if (!file.available()) break;
        char esc = (char)file.read();
        switch (esc) {
            case '"': out += '"'; break;
            case '\\': out += '\\'; break;
            case '/': out += '/'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'u': {
                uint32_t cp = 0;
                for (int n = 0; n < 4; n++) {
                    if (!file.available()) return out;
                    int v = jsonHexNibble((char)file.read());
                    if (v < 0) return out;
                    cp = (cp << 4) | (uint32_t)v;
                }
                appendUtf8Codepoint(out, cp);
                break;
            }
            default:
                return out;
        }
    }
    return out;
}

int jsonExtractIntFieldFromFile(File& file, const char* fieldName, int fallback) {
    if (!jsonFileSeekFieldValue(file, fieldName)) return fallback;
    bool neg = false;
    int ch = file.peek();
    if (ch == '-') {
        neg = true;
        file.read();
    }
    long value = 0;
    bool any = false;
    while (file.available()) {
        ch = file.peek();
        if (ch < '0' || ch > '9') break;
        any = true;
        value = value * 10 + (file.read() - '0');
        if (value > INT32_MAX) return fallback;
    }
    if (!any) return fallback;
    return neg ? -(int)value : (int)value;
}

bool jsonExtractStringFieldToFile(File& file, const char* fieldName, File& outFile) {
    if (!jsonFileSeekFieldValue(file, fieldName) || file.read() != '"') return false;

    while (file.available()) {
        char ch = (char)file.read();
        if (ch == '"') return true;
        if (ch != '\\') {
            outFile.write((const uint8_t*)&ch, 1);
            continue;
        }
        if (!file.available()) break;
        char esc = (char)file.read();
        switch (esc) {
            case '"': { uint8_t c = '"'; outFile.write(&c, 1); break; }
            case '\\': { uint8_t c = '\\'; outFile.write(&c, 1); break; }
            case '/': { uint8_t c = '/'; outFile.write(&c, 1); break; }
            case 'b': { uint8_t c = '\b'; outFile.write(&c, 1); break; }
            case 'f': { uint8_t c = '\f'; outFile.write(&c, 1); break; }
            case 'n': { uint8_t c = '\n'; outFile.write(&c, 1); break; }
            case 'r': { uint8_t c = '\r'; outFile.write(&c, 1); break; }
            case 't': { uint8_t c = '\t'; outFile.write(&c, 1); break; }
            case 'u': {
                uint32_t cp = 0;
                for (int n = 0; n < 4; n++) {
                    if (!file.available()) return false;
                    int v = jsonHexNibble((char)file.read());
                    if (v < 0) return false;
                    cp = (cp << 4) | (uint32_t)v;
                }
                size_t written = 0;
                if (!writeUtf8CodepointToFile(outFile, cp, written)) return false;
                break;
            }
            default:
                return false;
        }
    }
    return false;
}

void persistBadgeState() {
    g_prefs.putULong64("onion_id", g_identity.onionId);
    g_prefs.putString("status", g_identity.status);
    g_prefs.putString("username", g_identity.username);
    g_prefs.putString("onions", g_identity.onionCount);
    g_prefs.putBool("linked", g_identity.linked);
}

bool updateProfileFromObject(cJSON* obj) {
    if (!cJSON_IsObject(obj)) return false;

    bool changed = false;
    String value;
    const char* usernameKeys[] = {"username", "userName", "linkedUsername", "displayName", "name"};
    for (const char* key : usernameKeys) {
        if (jsonValueString(obj, key, value).length() && value.length()) {
            if (g_identity.username != value) {
                g_identity.username = value;
                changed = true;
            }
            break;
        }
    }

    bool gotBalance = false;
    if (jsonValueString(obj, "currentOnionBalance", value).length() && value.length()) {
        gotBalance = true;
    } else {
        String balanceType;
        if (jsonValueString(obj, "balanceType", balanceType).length()) {
            const char* sel = balanceType == "points" ? "currentOnionPoints" : "currentOnionTokens";
            if (jsonValueString(obj, sel, value).length() && value.length()) gotBalance = true;
        }
    }
    if (!gotBalance) {
        const char* onionKeys[] = {
            "onionCount", "onions", "currentOnionTokens", "tokenBalance",
            "onionTokens", "currentOnionPoints", "onionPoints", "points", "balance"
        };
        for (const char* key : onionKeys) {
            if (jsonValueString(obj, key, value).length() && value.length()) { gotBalance = true; break; }
        }
    }
    if (gotBalance && g_identity.onionCount != value) {
        g_identity.onionCount = value;
        changed = true;
    }

    return changed;
}

void updateProfileFromJson(cJSON* root) {
    bool changed = updateProfileFromObject(root);
    const char* objectKeys[] = {"profile", "user", "account"};
    for (const char* key : objectKeys) {
        changed = updateProfileFromObject(cJSON_GetObjectItemCaseSensitive(root, key)) || changed;
    }
    if (changed) persistBadgeState();
}
