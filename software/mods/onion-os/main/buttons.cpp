#include "buttons.h"
#include "badge_state.h"
#include "display.h"
#include "espnow.h"
#include "mqtt.h"
#include "lua_api.h"
#include "wifi.h"
#include "ui.h"

void handleButtons() {
    uint32_t now = millis();
    if (now - g_lastButtonPoll < 50) return;
    g_lastButtonPoll = now;

    uint8_t buttons = readButtons();
    uint8_t pressed = buttons & ~g_lastButtons;
    g_lastButtons = buttons;
    if (!pressed) return;

    if (g_luaDisplayActive) {
        g_luaDisplayActive = false;
        g_forceFullRefresh = true;  // guarantee clean full refresh over Lua content
        g_screen = SCREEN_STATUS;
        g_needsRedraw = true;
        return;
    }

    if (g_screen == SCREEN_LINK_PROMPT) {
        if (pressed & BTN_SELECT) sendLinkResponse(true);
        if (pressed & BTN_CANCEL) sendLinkResponse(false);
    } else if (g_screen == SCREEN_TX_PROMPT) {
        if (pressed & BTN_SELECT) sendTransactionResponse(true);
        if (pressed & BTN_CANCEL) sendTransactionResponse(false);
    } else if (g_screen == SCREEN_LUA_PROMPT) {
        if (pressed & BTN_SELECT) sendLuaPushResponse(true);
        if (pressed & BTN_CANCEL) sendLuaPushResponse(false);
    } else if (g_screen == SCREEN_CHECKIN_PROMPT) {
        if (pressed & BTN_SELECT) sendCheckInApproval(true);
        if (pressed & BTN_CANCEL) sendCheckInApproval(false);
    } else if (g_screen == SCREEN_CHECKIN_RESULT) {
        if (pressed & (BTN_SELECT | BTN_CANCEL)) {
            g_screen = SCREEN_STATUS;
        }
    } else if (g_screen == SCREEN_DELETE_CONFIRM) {
        if (pressed & BTN_CANCEL) {
            g_screen = SCREEN_SCRIPT_EXPLORER;
        } else if (pressed & BTN_LEFT) {
            g_deleteChoice = false;
        } else if (pressed & BTN_RIGHT) {
            g_deleteChoice = true;
        } else if (pressed & BTN_SELECT) {
            if (g_deleteChoice) {
                deleteStoredScript(g_pendingDeletePath);
                refreshScriptList();
            }
            g_screen = SCREEN_SCRIPT_EXPLORER;
        }
    } else if (g_screen == SCREEN_SCRIPT_EXPLORER) {
        if (pressed & BTN_CANCEL) {
            g_screen = SCREEN_STATUS;
        } else if ((pressed & BTN_LEFT) && g_scriptSelection > 0 && g_scriptSelection <= (int)g_scripts.size()) {
            g_pendingDeletePath = g_scripts[g_scriptSelection - 1];
            g_deleteChoice = false;
            g_screen = SCREEN_DELETE_CONFIRM;
        } else if (pressed & BTN_RIGHT) {
            syncScripts();
            refreshScriptList();
        } else if ((pressed & BTN_SELECT) && g_scriptSelection == 0) {
            syncScripts();
            refreshScriptList();
        } else if ((pressed & BTN_SELECT) && g_scriptSelection <= (int)g_scripts.size()) {
            runStoredScript(g_scripts[g_scriptSelection - 1]);
        } else {
            if ((pressed & BTN_UP) && g_scriptSelection > 0) g_scriptSelection--;
            if ((pressed & BTN_DOWN) && g_scriptSelection < (int)g_scripts.size()) g_scriptSelection++;
        }
    } else if (g_screen == SCREEN_SETTINGS) {
        if (pressed & BTN_CANCEL) {
            g_screen = SCREEN_STATUS;
        } else if (pressed & (BTN_UP | BTN_DOWN)) {
            g_settingsSel = (g_settingsSel + 1) % 2;
        } else if (pressed & BTN_SELECT) {
            if (g_settingsSel == 0) {
                g_wifiOverviewSel = 0;
                g_screen = SCREEN_WIFI_OVERVIEW;
            } else if (g_settingsSel == 1) {
                g_screen = SCREEN_ABOUT;
            }
        }
    } else if (g_screen == SCREEN_ABOUT) {
        if (pressed & BTN_CANCEL) {
            g_screen = SCREEN_SETTINGS;
        }
    } else if (g_screen == SCREEN_WIFI_OVERVIEW) {
        if (pressed & BTN_CANCEL) {
            g_screen = SCREEN_SETTINGS;
        } else if (pressed & BTN_UP) {
            g_wifiOverviewSel = (g_wifiOverviewSel + 2) % 3;
        } else if (pressed & BTN_DOWN) {
            g_wifiOverviewSel = (g_wifiOverviewSel + 1) % 3;
        } else if (pressed & BTN_SELECT) {
            if (g_wifiOverviewSel == 0) {
                g_lastWifiAttempt = millis(); // prevent background reconnect during scan
                startWifiScan();
                g_screen = SCREEN_WIFI_SCANNING;
            } else if (g_wifiOverviewSel == 1) {
                WiFi.disconnect();
                setLog("WiFi disconnected");
            } else {
                g_screen = SCREEN_SETTINGS;
            }
        }
    } else if (g_screen == SCREEN_WIFI_SCANNING) {
        if (pressed & BTN_CANCEL) {
            g_wifiWorkerResult.store(WIFI_WORKER_IDLE);
            g_screen = SCREEN_WIFI_OVERVIEW;
        }
    } else if (g_screen == SCREEN_WIFI_LIST) {
        if (pressed & BTN_CANCEL) {
            g_screen = SCREEN_WIFI_OVERVIEW;
        } else if (pressed & BTN_UP) {
            if (g_wifiListSel > 0) g_wifiListSel--;
        } else if (pressed & BTN_DOWN) {
            if (g_wifiListSel + 1 < (int)g_wifiNetworks.size()) g_wifiListSel++;
        } else if ((pressed & BTN_SELECT) && !g_wifiNetworks.empty()) {
            g_wifiConnectSsid = String(g_wifiNetworks[g_wifiListSel].ssid);
            if (!g_wifiNetworks[g_wifiListSel].secured) {
                g_wifiPassBuf[0] = '\0';
                startWifiConnect(g_wifiConnectSsid.c_str(), "");
                g_screen = SCREEN_WIFI_CONNECTING;
            } else {
                g_wifiPassLen = 0;
                memset(g_wifiPassBuf, 0, sizeof(g_wifiPassBuf));
                g_kbRow = 1; g_kbCol = 0; g_kbCaps = false;
                g_screen = SCREEN_WIFI_PASSWORD;
            }
        }
    } else if (g_screen == SCREEN_WIFI_PASSWORD) {
        if (pressed & BTN_UP) {
            g_kbRow = (g_kbRow + kKbTotalRows - 1) % kKbTotalRows;
            int maxCol = kbRowLen(g_kbRow) - 1;
            if (g_kbCol > maxCol) g_kbCol = maxCol;
        } else if (pressed & BTN_DOWN) {
            g_kbRow = (g_kbRow + 1) % kKbTotalRows;
            int maxCol = kbRowLen(g_kbRow) - 1;
            if (g_kbCol > maxCol) g_kbCol = maxCol;
        } else if (pressed & BTN_LEFT) {
            int len = kbRowLen(g_kbRow);
            g_kbCol = (g_kbCol + len - 1) % len;
        } else if (pressed & BTN_RIGHT) {
            int len = kbRowLen(g_kbRow);
            g_kbCol = (g_kbCol + 1) % len;
        } else if (pressed & BTN_SELECT) {
            if (g_kbRow == 5) {
                if (g_kbCol == 0) {
                    g_kbCaps = !g_kbCaps;
                } else if (g_kbCol == 1 && g_wifiPassLen < 64) {
                    g_wifiPassBuf[g_wifiPassLen++] = ' ';
                } else if (g_kbCol == 2) {
                    if (g_wifiPassLen > 0) g_wifiPassLen--;
                } else if (g_kbCol == 3) {
                    g_wifiPassBuf[g_wifiPassLen] = '\0';
                    startWifiConnect(g_wifiConnectSsid.c_str(), g_wifiPassBuf);
                    g_screen = SCREEN_WIFI_CONNECTING;
                }
            } else if (g_wifiPassLen < 64) {
                const char* rowStr = g_kbCaps ? kKbCaps[g_kbRow] : kKbNormal[g_kbRow];
                g_wifiPassBuf[g_wifiPassLen++] = rowStr[g_kbCol];
            }
        } else if (pressed & BTN_CANCEL) {
            if (g_wifiPassLen > 0) {
                g_wifiPassLen--;
            } else {
                g_screen = SCREEN_WIFI_LIST;
            }
        }
    } else if (g_screen == SCREEN_WIFI_CONNECTING) {
        if (pressed & BTN_CANCEL) {
            g_wifiWorkerResult.store(WIFI_WORKER_IDLE);
            WiFi.disconnect();
            g_screen = SCREEN_WIFI_OVERVIEW;
        }
    } else if (g_screen == SCREEN_WIFI_RESULT) {
        if (pressed & (BTN_SELECT | BTN_CANCEL)) {
            g_wifiOverviewSel = 0;
            g_screen = SCREEN_WIFI_OVERVIEW;
        }
    } else {
        if (pressed & BTN_UP) {
            g_homeSelection = (g_homeSelection + HOME_ITEM_COUNT - 1) % HOME_ITEM_COUNT;
        }
        if (pressed & BTN_DOWN) {
            g_homeSelection = (g_homeSelection + 1) % HOME_ITEM_COUNT;
        }
        if (pressed & BTN_SELECT) {
            if (g_homeSelection == HOME_ITEM_SCRIPTS) {
                refreshScriptList();
                g_screen = SCREEN_SCRIPT_EXPLORER;
            } else if (g_homeSelection == HOME_ITEM_REFRESH) {
                doHttpHandshake();
                doMqttHandshake();
                refreshPublicProfile();
            } else if (g_homeSelection == HOME_ITEM_SETTINGS) {
                g_settingsSel = 0;
                g_screen = SCREEN_SETTINGS;
            }
        }
    }
    if (!g_luaDisplayActive) g_needsRedraw = true;
}
