#pragma once

#include <Arduino.h>

extern "C" {
#include "lua.h"
}

void registerOnionLua(lua_State* L);
lua_State* newLuaState();
void cleanupLuaRuntime();
bool runLuaBuffer(const char* source, size_t sourceLen, const String& name);
bool runLuaSource(const String& source, const String& name);
bool runStoredScript(const String& path);
void runScriptByName(const String& name);
bool validScriptFileName(const String& name);
bool validStoredScriptPath(const String& path);
bool deleteStoredScript(const String& path);
void deleteScriptByName(const String& name);
bool installAndRunPushedScript(String& error);
bool sendLuaPushResponse(bool approved);
void syncScripts();
