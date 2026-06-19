#include "serial_cmd.h"
#include "badge_state.h"
#include "solana.h"
#include "mqtt.h"
#include "lua_api.h"

void printHelp() {
    Serial.println();
    Serial.println("Onion OS serial commands:");
    Serial.println("  api-key <badge_api_key>");
    Serial.println("  mqtt-auth [username] [password] [prefix]");
    Serial.println("  scripts-url <manifest_url>");
    Serial.println("  module <L1|L2|R>");
    Serial.println("  wallet");
    Serial.println("  keygen confirm");
    Serial.println("  handshake");
    Serial.println("  scripts");
    Serial.println("  run <script_name.lua>");
    Serial.println("  delete <script_name.lua>");
    Serial.println("  state");
    Serial.println("  help");
    Serial.println();
}

static std::vector<String> splitCommand(const String& line) {
    std::vector<String> parts;
    int start = 0;
    while (start < (int)line.length()) {
        while (start < (int)line.length() && line[start] == ' ') start++;
        if (start >= (int)line.length()) break;
        int end = line.indexOf(' ', start);
        if (end < 0) end = line.length();
        parts.push_back(line.substring(start, end));
        start = end + 1;
    }
    return parts;
}

void handleSerial() {
    static String line;
    while (Serial.available()) {
        char ch = (char)Serial.read();
        if (ch == '\r') continue;
        if (ch != '\n') {
            line += ch;
            continue;
        }

        line.trim();
        std::vector<String> args = splitCommand(line);
        line = "";
        if (args.empty()) return;

        if (args[0] == "wifi") {
            setLog("WiFi is hardcoded");
        } else if (args[0] == "server") {
            g_config.serverBaseUrl = ONION_HARDCODED_SERVER_BASE_URL;
            if (args.size() >= 3) {
                g_config.badgeApiKey = args[2];
                saveConfigValue("api_key", g_config.badgeApiKey);
                setLog("API key saved; URL hardcoded");
            } else {
                setLog("Server URL is hardcoded");
            }
        } else if (args[0] == "api-key" && args.size() >= 2) {
            g_config.badgeApiKey = args[1];
            saveConfigValue("api_key", g_config.badgeApiKey);
            setLog("API key saved");
        } else if (args[0] == "mqtt" || args[0] == "mqtt-auth") {
            g_config.mqttUri = ONION_HARDCODED_MQTT_URI;
            size_t firstAuthArg = 1;
            if (args[0] == "mqtt" && args.size() >= 2 &&
                (args[1].indexOf("://") >= 0 || args[1].indexOf(':') >= 0)) {
                firstAuthArg = 2;
            }
            g_config.mqttUsername = args.size() > firstAuthArg ? args[firstAuthArg] : "";
            g_config.mqttPassword = args.size() > firstAuthArg + 1 ? args[firstAuthArg + 1] : "";
            g_config.mqttTopicPrefix = args.size() > firstAuthArg + 2 ? args[firstAuthArg + 2] : "oniondao";
            g_prefs.remove("mqtt_uri");
            saveConfigValue("mqtt_user", g_config.mqttUsername);
            saveConfigValue("mqtt_pass", g_config.mqttPassword);
            saveConfigValue("mqtt_prefix", g_config.mqttTopicPrefix);
            if (g_mqtt) {
                esp_mqtt_client_stop(g_mqtt);
                esp_mqtt_client_destroy(g_mqtt);
                g_mqtt = nullptr;
                g_mqttConnected = false;
            }
            setLog("MQTT auth saved; URL hardcoded");
        } else if (args[0] == "scripts-url" && args.size() >= 2) {
            g_config.scriptManifestUrl = args[1];
            saveConfigValue("script_url", g_config.scriptManifestUrl);
            setLog("Script URL saved");
        } else if (args[0] == "module" && args.size() >= 2) {
            String variant = args[1];
            variant.toUpperCase();
            if (variant == "L1" || variant == "L2" || variant == "R") {
                g_config.moduleVariant = variant;
                saveConfigValue("mod_variant", variant);
                setLog("Module variant " + variant);
            } else {
                setLog("Variant must be L1, L2, or R");
            }
        } else if (args[0] == "wallet") {
            String keyError;
            if (loadOrCreateSolanaKey(false, keyError)) {
                Serial.printf("wallet=%s\n", g_identity.solanaPublicKey.c_str());
                setLog("Wallet ready");
            } else {
                Serial.printf("wallet_error=%s\n", keyError.c_str());
                setLog(keyError);
            }
        } else if (args[0] == "keygen" && args.size() >= 2 && args[1] == "confirm") {
            if (g_identity.linked) {
                setLog("Refusing linked key rotation");
            } else {
                clearSolanaKey();
                String keyError;
                if (loadOrCreateSolanaKey(true, keyError)) {
                    Serial.printf("wallet=%s\n", g_identity.solanaPublicKey.c_str());
                    setLog("Wallet rotated");
                } else {
                    Serial.printf("wallet_error=%s\n", keyError.c_str());
                    setLog(keyError);
                }
            }
        } else if (args[0] == "handshake") {
            doHttpHandshake();
            doMqttHandshake();
        } else if (args[0] == "scripts") {
            syncScripts();
        } else if (args[0] == "run" && args.size() >= 2) {
            runScriptByName(args[1]);
        } else if ((args[0] == "delete" || args[0] == "rm") && args.size() >= 2) {
            deleteScriptByName(args[1]);
        } else if (args[0] == "state") {
            Serial.printf("hardwareId=%s\n", g_identity.hardwareId.c_str());
            Serial.printf("onionId=%llu\n", (unsigned long long)g_identity.onionId);
            Serial.printf("status=%s\n", g_identity.status.c_str());
            Serial.printf("username=%s\n", g_identity.username.c_str());
            Serial.printf("onions=%s\n", g_identity.onionCount.c_str());
            Serial.printf("wallet=%s\n", g_identity.solanaPublicKey.c_str());
            Serial.printf("wifi=%s\n", g_config.wifiSsid.c_str());
            Serial.printf("server=%s\n", g_config.serverBaseUrl.c_str());
            Serial.printf("mqtt=%s\n", g_config.mqttUri.c_str());
            Serial.printf("module=%s\n", g_config.moduleVariant.c_str());
        } else {
            printHelp();
        }
    }
}
