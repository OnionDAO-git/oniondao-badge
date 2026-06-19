#pragma once

#include <Arduino.h>
#include <GxEPD2_BW.h>
#include <gdey/GxEPD2_270_GDEY027T91.h>
#include <Fonts/FreeMono9pt7b.h>
#include <Fonts/FreeMonoBold9pt7b.h>
#include <Fonts/FreeMonoBold18pt7b.h>

void initPeripherals();
uint8_t readButtons();
void printLine(const char* text, int y, const GFXfont* font = &FreeMono9pt7b);
void printString(const String& text, int y, const GFXfont* font = &FreeMono9pt7b);
String clipped(const String& value, size_t len);
void sampleBattery(bool force = false);
String storedScriptDisplayName(const String& path);
bool validAssetFileName(const String& name, const char* requiredSuffix = nullptr);
bool validImageFileName(const String& name);
String imagePathForName(const String& name);
String normalizedSpiffsPath(const String& path);
void refreshScriptList();
void flushFrame();
void redraw();
