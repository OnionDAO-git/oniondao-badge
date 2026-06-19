#pragma once

#include <Arduino.h>

void soundSpeakerBegin();
void soundSpeakerEnd();
void soundPlayTone(uint32_t freqHz, uint32_t durationMs);
void soundPlay(const int16_t* samples, size_t count);
void soundMicBegin();
void soundMicEnd();
int soundMicRead(int16_t* buf, size_t maxSamples, uint32_t timeoutMs);
int soundMicLevel(uint32_t durationMs);
void moduleShutdownActive();

// Full-param versions exposed for Lua bindings
bool soundSpeakerBegin(int bclk, int ws, int dout, int ctrl, int sampleRate,
                       int powerPin, String& error);
bool soundMicBegin(int clk, int din, int ws, int ctrl, int sampleRate,
                   int dmaDesc, int dmaFrame, int discardMs,
                   int powerPin, String& error);
void soundStop();
void soundPlayTone(double freq, int durationMs, double volume);
