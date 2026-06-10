#include "ui.h"
#include "badge_state.h"
#include "display.h"
#include <WiFi.h>

static void drawHomeItem(HomeItem item, const String& label, int y) {
    String prefix = g_homeSelection == item ? "> " : "  ";
    printString(prefix + label, y, g_homeSelection == item ? &FreeMonoBold9pt7b : &FreeMono9pt7b);
}

void drawBootSplash() {
    g_frame.fillScreen(GxEPD_WHITE);
    int x = (ONION_DISPLAY_WIDTH  - ONION_LOGO_WIDTH)  / 2;
    int y = (ONION_DISPLAY_HEIGHT - ONION_LOGO_HEIGHT) / 2;
    g_frame.drawBitmap(x, y, ONION_LOGO_BLACK_BITMAP, ONION_LOGO_WIDTH, ONION_LOGO_HEIGHT, GxEPD_BLACK);
}

void drawStatus() {
    g_frame.fillScreen(GxEPD_WHITE);
    printLine("ONION OS", 22, &FreeMonoBold18pt7b);

    String user = g_identity.username.length() ? g_identity.username : (g_identity.linked ? "linked" : "not linked");
    printString("User: " + clipped(user, 21), 48, &FreeMonoBold9pt7b);
    printString("Onions: " + clipped(g_identity.onionCount, 18), 66);
    printString("ID: " + String(g_identity.onionId ? String(g_identity.onionId) : "pending") +
        "  " + String(g_mqttConnected ? "MQTT" : (WiFi.status() == WL_CONNECTED ? "WiFi" : "offline")), 84);
    drawHomeItem(HOME_ITEM_SCRIPTS, "Scripts Explorer", 100);
    drawHomeItem(HOME_ITEM_REFRESH, "Refresh Profile", 116);
    drawHomeItem(HOME_ITEM_SETTINGS, "Settings", 132);
    printString(clipped(g_log, 30), 168);
}

void drawScriptExplorer() {
    g_frame.fillScreen(GxEPD_WHITE);
    printLine("SCRIPTS", 22, &FreeMonoBold18pt7b);

    int start = 0;
    const int visibleRows = 5;
    if (g_scriptSelection >= visibleRows) start = g_scriptSelection - visibleRows + 1;
    int itemCount = (int)g_scripts.size() + 1;
    for (int row = 0; row < visibleRows && start + row < itemCount; ++row) {
        int idx = start + row;
        String prefix = idx == g_scriptSelection ? "> " : "  ";
        String label = idx == 0 ? "Update Scripts" : clipped(storedScriptDisplayName(g_scripts[idx - 1]), 24);
        printString(prefix + label, 52 + row * 20,
            idx == g_scriptSelection ? &FreeMonoBold9pt7b : &FreeMono9pt7b);
    }
    if (g_scripts.empty()) printString("No scripts installed", 154);
}

void drawDeleteConfirm() {
    g_frame.fillScreen(GxEPD_WHITE);
    printLine("DELETE LUA?", 24, &FreeMonoBold18pt7b);
    printString(clipped(storedScriptDisplayName(g_pendingDeletePath), 24), 58, &FreeMonoBold9pt7b);
    printString("This removes it from badge", 86);
    printString(String(g_deleteChoice ? "  CANCEL    > DELETE" : "> CANCEL      DELETE"), 128,
        &FreeMonoBold9pt7b);
}

void drawLinkPrompt() {
    g_frame.fillScreen(GxEPD_WHITE);
    printLine("LINK BADGE?", 24, &FreeMonoBold18pt7b);
    printString("User:", 54, &FreeMonoBold9pt7b);
    printString(clipped(g_linkPrompt.username, 22), 74);
    printString("Wallet: " + String(g_identity.solanaPublicKey.length() ? "ready" : "create on approve"), 112);
}

void drawTransactionPrompt() {
    g_frame.fillScreen(GxEPD_WHITE);
    printLine("SIGN ONIONS?", 24, &FreeMonoBold18pt7b);
    printString("Type: " + clipped(g_txPrompt.type, 18), 54);
    printString("Amount: " + String(g_txPrompt.amount), 74);
    printString("Signer: Ed25519 + ATECC", 112);
}

void drawLuaPrompt() {
    g_frame.fillScreen(GxEPD_WHITE);
    printLine("INSTALL LUA?", 24, &FreeMonoBold18pt7b);
    printString(clipped(g_luaPrompt.title.length() ? g_luaPrompt.title : g_luaPrompt.fileName, 24), 54, &FreeMonoBold9pt7b);
    printString("By: " + clipped(g_luaPrompt.authorUsername, 18), 76);
    printString(clipped(g_luaPrompt.description, 28), 96);
    printString("Size: " + String(g_luaPrompt.sizeBytes) + " bytes", 116);
}

void drawCheckInPrompt() {
    g_frame.fillScreen(GxEPD_WHITE);
    printLine("CHECK IN?", 24, &FreeMonoBold18pt7b);
    printString(clipped(g_checkinPrompt.label.length() ? g_checkinPrompt.label : "Workshop attendance", 25),
        54, &FreeMonoBold9pt7b);
    printString("Room: " + clipped(g_checkinPrompt.room.length() ? g_checkinPrompt.room : g_checkinPrompt.beaconId, 21), 78);
    printString("Signal: " + String((int)g_checkinPrompt.rssi) + " dBm", 100);
    printString("SELECT yes", 138, &FreeMonoBold9pt7b);
    printString("CANCEL no", 158);
}

void drawCheckInResult() {
    g_frame.fillScreen(GxEPD_WHITE);
    printLine(g_checkinResult.awarded ? "CHECKED IN" : "CHECK IN", 24, &FreeMonoBold18pt7b);
    if (g_checkinResult.points > 0) {
        printString("Points: +" + String(g_checkinResult.points), 54, &FreeMonoBold9pt7b);
        printString(clipped(g_checkinResult.message, 27), 78);
    } else {
        printString(clipped(g_checkinResult.message.length() ? g_checkinResult.message : "Waiting for beacon...", 27),
            58, &FreeMonoBold9pt7b);
    }
    printString("SELECT/CANCEL to close", 142);
}

int kbRowLen(int row) {
    if (row < 5) return (int)strlen(kKbNormal[row]);
    return 4;
}

static int kbCellW(int row) {
    if (row == 2) return 29;
    if (row == 3) return 37;
    if (row == 4) return 22;
    return 26;
}

static int kbStartX(int row) {
    if (row == 2) return 2;
    if (row == 3) return 3;
    return 2;
}

static int kbBoxY(int row) { return 36 + row * 23; }

void drawSettingsScreen() {
    g_frame.fillScreen(GxEPD_WHITE);
    printLine("SETTINGS", 22, &FreeMonoBold18pt7b);
    printString((g_settingsSel == 0 ? "> " : "  ") + String("WiFi"), 58,
        g_settingsSel == 0 ? &FreeMonoBold9pt7b : &FreeMono9pt7b);
    printString((g_settingsSel == 1 ? "> " : "  ") + String("About"), 78,
        g_settingsSel == 1 ? &FreeMonoBold9pt7b : &FreeMono9pt7b);
}

void drawWifiOverview() {
    g_frame.fillScreen(GxEPD_WHITE);
    printLine("WIFI", 22, &FreeMonoBold18pt7b);
    bool conn = WiFi.status() == WL_CONNECTED;
    printString(String("Status: ") + (conn ? "Connected" : "Offline"), 48, &FreeMonoBold9pt7b);
    if (conn) {
        printString("SSID: " + clipped(WiFi.SSID(), 22), 66);
        printString("IP: " + WiFi.localIP().toString(), 84);
    } else if (g_config.wifiSsid.length()) {
        printString("Last: " + clipped(g_config.wifiSsid, 22), 66);
    }
    const char* items[] = {"Scan Networks", "Disconnect", "Back"};
    for (int i = 0; i < 3; i++) {
        printString((g_wifiOverviewSel == i ? "> " : "  ") + String(items[i]),
            108 + i * 18, g_wifiOverviewSel == i ? &FreeMonoBold9pt7b : &FreeMono9pt7b);
    }
}

void drawWifiScanning() {
    g_frame.fillScreen(GxEPD_WHITE);
    printLine("WIFI SCAN", 22, &FreeMonoBold18pt7b);
    printString("Scanning networks...", 60, &FreeMonoBold9pt7b);
    printString("Please wait (1-3s)", 80);
    printString("CANCEL to abort", 140);
}

void drawWifiList() {
    g_frame.fillScreen(GxEPD_WHITE);
    printLine("NETWORKS", 22, &FreeMonoBold18pt7b);
    if (g_wifiNetworks.empty()) {
        printString("No networks found", 60, &FreeMonoBold9pt7b);
        return;
    }
    const int vis = 5;
    int start = g_wifiListSel >= vis ? g_wifiListSel - vis + 1 : 0;
    for (int r = 0; r < vis && start + r < (int)g_wifiNetworks.size(); r++) {
        int idx = start + r;
        const WifiNetwork& net = g_wifiNetworks[idx];
        char buf[32];
        snprintf(buf, sizeof(buf), "%ddB%s", net.rssi, net.secured ? "*" : " ");
        String line = (idx == g_wifiListSel ? "> " : "  ") +
                      clipped(String(net.ssid), 14) + " " + buf;
        printString(line, 48 + r * 20,
            idx == g_wifiListSel ? &FreeMonoBold9pt7b : &FreeMono9pt7b);
    }
}

void drawWifiPassword() {
    g_frame.fillScreen(GxEPD_WHITE);

    g_frame.setFont(&FreeMono9pt7b);
    g_frame.setTextColor(GxEPD_BLACK);
    g_frame.setCursor(4, 12);
    g_frame.print("Net: " + g_wifiConnectSsid);
    g_frame.setCursor(4, 28);
    String passLine = "Pass: ";
    passLine.reserve(g_wifiPassLen + 8);
    for (int i = 0; i < g_wifiPassLen; i++) passLine += '*';
    passLine += '_';
    g_frame.print(passLine);

    const int kCellH    = 22;
    const int kBaseline = 16;

    const int         kCtrlW[4]     = {58, 90, 58, 58};
    const char* const kCtrlLabel[4] = {"CAPS", "SPACE", "DEL", "OK"};

    for (int row = 0; row < kKbTotalRows; row++) {
        const int boxY = kbBoxY(row);

        if (row < 5) {
            const char* rowStr = g_kbCaps ? kKbCaps[row] : kKbNormal[row];
            const int   len    = (int)strlen(rowStr);
            const int   cw     = (row == 4) ? 22 : kbCellW(row);
            const int   sx     = (row == 4) ?  0 : kbStartX(row);

            for (int col = 0; col < len; col++) {
                const int  cx  = sx + col * cw;
                const bool sel = (g_kbRow == row && g_kbCol == col);
                if (sel) {
                    g_frame.fillRect(cx, boxY, cw - 1, kCellH, GxEPD_BLACK);
                    g_frame.setTextColor(GxEPD_WHITE);
                } else {
                    g_frame.fillRect(cx, boxY, cw - 1, kCellH, GxEPD_WHITE);
                    g_frame.drawRect(cx, boxY, cw - 1, kCellH, GxEPD_BLACK);
                    g_frame.setTextColor(GxEPD_BLACK);
                }
                g_frame.setFont(&FreeMono9pt7b);
                char ch[2] = {rowStr[col], '\0'};
                int16_t tx1, ty1; uint16_t tw, th;
                g_frame.getTextBounds(ch, 0, 0, &tx1, &ty1, &tw, &th);
                g_frame.setCursor(cx + ((cw - 1) - (int)tw) / 2 - tx1, boxY + kBaseline);
                g_frame.print(ch);
            }
        } else {
            int x = 0;
            for (int col = 0; col < 4; col++) {
                const int  w       = kCtrlW[col];
                const bool sel     = (g_kbRow == 5 && g_kbCol == col);
                const bool capsLit = (col == 0 && g_kbCaps);
                const bool inv     = sel || capsLit;
                if (inv) {
                    g_frame.fillRect(x, boxY, w - 1, kCellH, GxEPD_BLACK);
                    g_frame.setTextColor(GxEPD_WHITE);
                } else {
                    g_frame.fillRect(x, boxY, w - 1, kCellH, GxEPD_WHITE);
                    g_frame.drawRect(x, boxY, w - 1, kCellH, GxEPD_BLACK);
                    g_frame.setTextColor(GxEPD_BLACK);
                }
                g_frame.setFont(&FreeMono9pt7b);
                int16_t tx1, ty1; uint16_t tw, th;
                g_frame.getTextBounds(kCtrlLabel[col], 0, 0, &tx1, &ty1, &tw, &th);
                g_frame.setCursor(x + ((w - 1) - (int)tw) / 2 - tx1, boxY + kBaseline);
                g_frame.print(kCtrlLabel[col]);
                x += w;
            }
        }
    }
    g_frame.setTextColor(GxEPD_BLACK);
}

void drawWifiConnecting() {
    g_frame.fillScreen(GxEPD_WHITE);
    printLine("WIFI", 22, &FreeMonoBold18pt7b);
    printString("Connecting to:", 50, &FreeMonoBold9pt7b);
    printString(clipped(g_wifiConnectSsid, 26), 68);
    printString("Please wait...", 100);
    printString("CANCEL to abort", 140);
}

void drawWifiResult() {
    g_frame.fillScreen(GxEPD_WHITE);
    printLine("WIFI", 22, &FreeMonoBold18pt7b);
    printString(g_wifiResultMsg, 58, &FreeMonoBold9pt7b);
    if (WiFi.status() == WL_CONNECTED) {
        printString("IP: " + WiFi.localIP().toString(), 78);
    }
    printString("SELECT or CANCEL to continue", 120);
}

void drawAboutScreen() {
    g_frame.fillScreen(GxEPD_WHITE);
    printLine("ABOUT", 22, &FreeMonoBold18pt7b);

    int y = 48;
    String displayName = g_identity.name.length() ? g_identity.name : g_identity.username;
    printString("name: " + clipped(displayName, 22), y);
    y += 18;

    printString("handle: " + (g_identity.handle.length() ? clipped(g_identity.handle, 20) : "-"), y);
    y += 18;

    printString("DaysCheckedIn: " + String(g_identity.daysCheckedIn), y);
    y += 18;

    printString("EventsAttended: " + String(g_identity.eventCheckedInCount), y);
    y += 18;

    if (g_identity.statusUpdateBody.length()) {
        String body = g_identity.statusUpdateBody;
        const int maxFirst = 12;  // "LastStatus: " is 12 chars, ~24 chars fit on screen
        if ((int)body.length() <= maxFirst) {
            printString("LastStatus: " + body, y);
        } else {
            printString("LastStatus: " + body.substring(0, maxFirst), y);
            y += 18;
            String rest = body.substring(maxFirst);
            printString("  " + clipped(rest, 22), y);  // "  " prefix = 2, leaving 22
        }
    } else {
        printString("LastStatus: -", y);
    }
}
