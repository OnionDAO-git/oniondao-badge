#include "atecc.h"
#include "badge_state.h"
#include <Wire.h>

void restartSharedI2cBus() {
    Wire.begin(PIN_SDA, PIN_SCL);
    Wire.setClock(100000);
}

bool ateccHmac(const std::vector<uint8_t>& message, uint8_t digest[32], String* serialHex, String& error) {
    Wire.end();
    delay(5);
    atcab_release();

    ATCA_IFACECFG_I2C_ADDRESS(&cfg_ateccx08a_i2c_default) = ATECC_I2C_ADDRESS_8BIT;
    cfg_ateccx08a_i2c_default.atcai2c.bus = 0;
    cfg_ateccx08a_i2c_default.atcai2c.baud = 100000;

    ATCA_STATUS status = atcab_init(&cfg_ateccx08a_i2c_default);
    if (status != ATCA_SUCCESS) {
        error = "ATECC init failed " + String((int)status);
        restartSharedI2cBus();
        return false;
    }

    status = atcab_read_serial_number(g_ateccSerial);
    if (status != ATCA_SUCCESS) {
        error = "ATECC serial failed " + String((int)status);
        atcab_release();
        restartSharedI2cBus();
        return false;
    }
    g_ateccReady = true;
    if (serialHex) *serialHex = bytesToHex(g_ateccSerial, sizeof(g_ateccSerial));

    status = atcab_sha_hmac(message.data(), message.size(), ATECC_HMAC_SLOT, digest, SHA_MODE_TARGET_OUT_ONLY);
    atcab_release();
    restartSharedI2cBus();
    if (status != ATCA_SUCCESS) {
        error = "ATECC HMAC failed " + String((int)status);
        return false;
    }
    return true;
}

bool ateccRandom(uint8_t* out, size_t count, String& error) {
    if (count == 0) return true;

    Wire.end();
    delay(5);
    atcab_release();

    ATCA_IFACECFG_I2C_ADDRESS(&cfg_ateccx08a_i2c_default) = ATECC_I2C_ADDRESS_8BIT;
    cfg_ateccx08a_i2c_default.atcai2c.bus = 0;
    cfg_ateccx08a_i2c_default.atcai2c.baud = 100000;

    ATCA_STATUS status = atcab_init(&cfg_ateccx08a_i2c_default);
    if (status != ATCA_SUCCESS) {
        error = "ATECC init failed " + String((int)status);
        restartSharedI2cBus();
        return false;
    }

    size_t produced = 0;
    while (produced < count) {
        uint8_t block[32];
        status = atcab_random(block);
        if (status != ATCA_SUCCESS) {
            error = "ATECC random failed " + String((int)status);
            atcab_release();
            restartSharedI2cBus();
            return false;
        }
        size_t chunk = count - produced;
        if (chunk > sizeof(block)) chunk = sizeof(block);
        memcpy(out + produced, block, chunk);
        produced += chunk;
    }

    atcab_release();
    restartSharedI2cBus();
    return true;
}
