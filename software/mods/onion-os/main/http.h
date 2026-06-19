#pragma once

#include <Arduino.h>
#include <SPIFFS.h>
#include <cJSON.h>
#include <esp_http_client.h>
#include <vector>

esp_err_t httpCaptureEvent(esp_http_client_event_t* event);
int httpPostJson(const String& path, const String& body, String* response = nullptr);
int httpGetString(const String& url, String* response);
String urlEncode(const String& value);

// JSON utilities
String jsonString(cJSON* obj, const char* key);
String jsonValueString(cJSON* val);
int jsonInt(cJSON* obj, const char* key, int fallback);
bool jsonBool(cJSON* obj, const char* key, bool fallback);
int jsonHexNibble(char ch);
bool appendUtf8Codepoint(String& out, uint32_t cp);
bool writeUtf8Codepoint(String& out, const char*& p, const char* end);
String jsonExtractStringField(const String& json, const char* fieldName);
int jsonExtractIntField(const String& json, const char* fieldName, int fallback);
String jsonExtractStringFieldFromFile(File& file, const char* fieldName);
int jsonExtractIntFieldFromFile(File& file, const char* fieldName, int fallback);
bool jsonExtractStringFieldToFile(File& file, const char* fieldName, File& outFile);
uint64_t jsonUint64(cJSON* obj, const char* key, uint64_t fallback);

// Profile state
void persistBadgeState();
void updateProfileFromJson(cJSON* root);
void updateProfileExtendedFromJson(cJSON* root);
