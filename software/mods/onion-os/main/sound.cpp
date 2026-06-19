#include "sound.h"
#include "badge_state.h"
#include "subghz.h"
#include <driver/i2s_std.h>
#include <driver/i2s_pdm.h>
#include <cmath>

static void soundWriteSilence(int frames) {
    if (!g_i2sTx) return;
    static const int16_t kZeros[256] = {};
    while (frames > 0) {
        int n = frames > 256 ? 256 : frames;
        size_t written = 0;
        if (i2s_channel_write(g_i2sTx, kZeros, n * sizeof(int16_t), &written, 500) != ESP_OK) return;
        if (written == 0) return;
        frames -= n;
    }
}

void soundStop() {
    if (g_i2sTx) {
        soundWriteSilence(g_soundSampleRate / 8);
        i2s_channel_disable(g_i2sTx);
        i2s_del_channel(g_i2sTx);
        g_i2sTx = nullptr;
    }
    if (g_i2sRx) {
        i2s_channel_disable(g_i2sRx);
        i2s_del_channel(g_i2sRx);
        g_i2sRx = nullptr;
    }
    if (g_soundCtrlPin >= 0) {
        digitalWrite(g_soundCtrlPin, LOW);
        g_soundCtrlPin = -1;
    }
    // modulePowerOff handled by caller
}

void soundSpeakerBegin() {
    const ModuleVariantPins& v = resolveModuleVariant();
    String error;
    soundSpeakerBegin(v.line[1], v.line[2], v.line[3], v.line[4], SOUND_SPK_SAMPLE_RATE, PIN_PWR, error);
}

bool soundSpeakerBegin(int bclk, int ws, int dout, int ctrl, int sampleRate,
                       int powerPin, String& error) {
    if (g_activeModule != MODULE_NONE) {
        error = "module busy; end it first";
        return false;
    }
    modulePowerOn(powerPin);
    g_soundCtrlPin = ctrl;
    if (ctrl >= 0) {
        pinMode(ctrl, OUTPUT);
        digitalWrite(ctrl, HIGH);
        delay(SOUND_AMP_UNMUTE_MS);
    }

    i2s_chan_config_t chanCfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    if (i2s_new_channel(&chanCfg, &g_i2sTx, nullptr) != ESP_OK) {
        error = "i2s alloc failed";
        modulePowerOff();
        return false;
    }
    i2s_std_config_t stdCfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG((uint32_t)sampleRate),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = (gpio_num_t)bclk,
            .ws = (gpio_num_t)ws,
            .dout = (gpio_num_t)dout,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {0, 0, 0},
        },
    };
    if (i2s_channel_init_std_mode(g_i2sTx, &stdCfg) != ESP_OK) {
        error = "i2s std init failed";
        i2s_del_channel(g_i2sTx);
        g_i2sTx = nullptr;
        modulePowerOff();
        return false;
    }
    i2s_channel_enable(g_i2sTx);
    g_soundSampleRate = sampleRate;
    g_activeModule = MODULE_SOUND_SPK;
    soundWriteSilence(sampleRate / 32);
    return true;
}

void soundSpeakerEnd() {
    soundStop();
    modulePowerOff();
    g_activeModule = MODULE_NONE;
}

void soundPlayTone(uint32_t freqHz, uint32_t durationMs) {
    soundPlayTone((double)freqHz, (int)durationMs, 0.6);
}

void soundPlayTone(double freq, int durationMs, double volume) {
    const int sr = g_soundSampleRate;
    int64_t total = (int64_t)sr * durationMs / 1000;
    if (volume < 0) volume = 0;
    if (volume > 1) volume = 1;
    const double twoPi = 6.283185307179586;
    double step = freq > 0 ? twoPi * freq / sr : 0.0;
    double phase = 0.0;
    int16_t buf[256];
    int64_t produced = 0;
    while (produced < total) {
        int n = (int)((total - produced) > 256 ? 256 : (total - produced));
        for (int i = 0; i < n; i++) {
            buf[i] = (int16_t)(sin(phase) * 32000.0 * volume);
            phase += step;
            if (phase >= twoPi) phase -= twoPi;
        }
        size_t written = 0;
        i2s_channel_write(g_i2sTx, buf, n * sizeof(int16_t), &written, 1000);
        produced += n;
    }
}

void soundPlay(const int16_t* samples, size_t count) {
    if (!g_i2sTx || !samples || !count) return;
    size_t written = 0;
    i2s_channel_write(g_i2sTx, samples, count * sizeof(int16_t), &written, 1000);
}

void soundMicBegin() {
    const ModuleVariantPins& v = resolveModuleVariant();
    String error;
    soundMicBegin(v.line[1], v.line[0], v.line[2], v.line[4], SOUND_MIC_SAMPLE_RATE, 0, 0, 100, PIN_PWR, error);
}

bool soundMicBegin(int clk, int din, int ws, int ctrl, int sampleRate,
                   int dmaDesc, int dmaFrame, int discardMs,
                   int powerPin, String& error) {
    if (g_activeModule != MODULE_NONE) {
        error = "module busy; end it first";
        return false;
    }
    modulePowerOn(powerPin);
    if (ctrl >= 0) {
        pinMode(ctrl, OUTPUT);
        digitalWrite(ctrl, LOW);
    }
    if (ws >= 0) {
        pinMode(ws, OUTPUT);
        digitalWrite(ws, LOW);
    }
    delay(10);

    i2s_chan_config_t chanCfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    if (dmaDesc > 0) {
        if (dmaDesc > 64) dmaDesc = 64;
        chanCfg.dma_desc_num = dmaDesc;
    }
    if (dmaFrame > 0) {
        if (dmaFrame > 2046) dmaFrame = 2046;
        chanCfg.dma_frame_num = dmaFrame;
    }
    if (i2s_new_channel(&chanCfg, nullptr, &g_i2sRx) != ESP_OK) {
        error = "i2s alloc failed";
        modulePowerOff();
        return false;
    }
    i2s_pdm_rx_config_t pdmCfg = {
        .clk_cfg = I2S_PDM_RX_CLK_DEFAULT_CONFIG((uint32_t)sampleRate),
        .slot_cfg = I2S_PDM_RX_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .clk = (gpio_num_t)clk,
            .din = (gpio_num_t)din,
            .invert_flags = {0},
        },
    };
    pdmCfg.clk_cfg.dn_sample_mode = I2S_PDM_DSR_16S;
    pdmCfg.slot_cfg.slot_mask = I2S_PDM_SLOT_LEFT;
    if (i2s_channel_init_pdm_rx_mode(g_i2sRx, &pdmCfg) != ESP_OK) {
        error = "i2s pdm init failed";
        i2s_del_channel(g_i2sRx);
        g_i2sRx = nullptr;
        modulePowerOff();
        return false;
    }
    i2s_channel_enable(g_i2sRx);

    if (discardMs > SOUND_MIC_MAX_DISCARD_MS) discardMs = SOUND_MIC_MAX_DISCARD_MS;
    if (discardMs > 0) {
        int remaining = sampleRate * discardMs / 1000;
        int16_t scratch[256];
        uint32_t deadline = millis() + discardMs + 80;
        while (remaining > 0 && (int32_t)(deadline - millis()) > 0) {
            size_t toRead = remaining >= 256 ? sizeof(scratch) : remaining * sizeof(scratch[0]);
            size_t bytesRead = 0;
            esp_err_t rc = i2s_channel_read(g_i2sRx, scratch, toRead, &bytesRead, 120);
            if (rc != ESP_OK && rc != ESP_ERR_TIMEOUT) break;
            remaining -= (int)(bytesRead / sizeof(scratch[0]));
        }
    }

    g_soundSampleRate = sampleRate;
    g_soundMicStartedAt = millis();
    g_soundMicSamples = 0;
    g_soundMicBytes = 0;
    g_soundMicTimeouts = 0;
    g_activeModule = MODULE_SOUND_MIC;
    return true;
}

void soundMicEnd() {
    soundStop();
    modulePowerOff();
    g_activeModule = MODULE_NONE;
}

int soundMicRead(int16_t* buf, size_t maxSamples, uint32_t timeoutMs) {
    if (!g_i2sRx || !buf || !maxSamples) return 0;
    size_t bytesRead = 0;
    esp_err_t rc = i2s_channel_read(g_i2sRx, buf, maxSamples * sizeof(int16_t), &bytesRead, timeoutMs);
    if (rc == ESP_ERR_TIMEOUT) {
        g_soundMicTimeouts++;
        return 0;
    }
    if (rc != ESP_OK) return 0;
    int samples = (int)(bytesRead / sizeof(int16_t));
    g_soundMicSamples += samples;
    g_soundMicBytes += bytesRead;
    return samples;
}

int soundMicLevel(uint32_t durationMs) {
    if (!g_i2sRx) return 0;
    int16_t buf[256];
    int32_t peak = 0;
    uint32_t deadline = millis() + durationMs;
    while ((int32_t)(deadline - millis()) > 0) {
        size_t bytesRead = 0;
        esp_err_t rc = i2s_channel_read(g_i2sRx, buf, sizeof(buf), &bytesRead, 100);
        if (rc != ESP_OK && rc != ESP_ERR_TIMEOUT) break;
        int samples = (int)(bytesRead / sizeof(int16_t));
        for (int i = 0; i < samples; i++) {
            int32_t v = buf[i];
            if (v < 0) v = -v;
            if (v > peak) peak = v;
        }
    }
    return (int)peak;
}

void moduleShutdownActive() {
    if (g_activeModule == MODULE_SUBGHZ) subghzEnd();
    else if (g_activeModule != MODULE_NONE) soundStop();
}
