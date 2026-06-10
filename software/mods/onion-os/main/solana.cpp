#include "solana.h"
#include "badge_state.h"
#include "atecc.h"

std::vector<uint8_t> stringBytes(const String& value) {
    return std::vector<uint8_t>(
        reinterpret_cast<const uint8_t*>(value.c_str()),
        reinterpret_cast<const uint8_t*>(value.c_str()) + value.length()
    );
}

bool deriveWrappingKey(uint8_t key[32], String& error) {
    String context = "onion-os:solana-seed-wrap:v1:" + g_identity.hardwareId;
    std::vector<uint8_t> bytes = stringBytes(context);
    return ateccHmac(bytes, key, nullptr, error);
}

bool createAteccAttestation(const String& purpose, const String& subject, String& json, String& error) {
    uint8_t nonce[32];
    uint8_t mac[32];
    esp_fill_random(nonce, sizeof(nonce));

    String context = "onion-os:attestation:v1:" + purpose + ":" + subject + ":" +
        bytesToHex(nonce, sizeof(nonce)) + ":" + g_identity.solanaPublicKey;
    std::vector<uint8_t> bytes = stringBytes(context);
    String serialHex;
    if (!ateccHmac(bytes, mac, &serialHex, error)) return false;

    json = "{\"version\":1,\"slot\":" + String(ATECC_HMAC_SLOT) +
        ",\"serial\":\"" + serialHex +
        "\",\"purpose\":\"" + jsonEscape(purpose) +
        "\",\"subject\":\"" + jsonEscape(subject) +
        "\",\"nonce\":\"" + bytesToHex(nonce, sizeof(nonce)) +
        "\",\"hmac\":\"" + bytesToHex(mac, sizeof(mac)) + "\"}";
    return true;
}

bool decryptSolanaSeed(uint8_t seed[SOLANA_SEED_LEN], String& error) {
    uint8_t nonce[SOLANA_KEY_NONCE_LEN];
    uint8_t ciphertext[SOLANA_SEED_LEN + SOLANA_KEY_MAC_LEN];
    if (!prefsGetBytes("key_nonce", nonce, sizeof(nonce)) ||
        !prefsGetBytes("key_ct", ciphertext, sizeof(ciphertext))) {
        error = "No wrapped wallet seed";
        return false;
    }

    uint8_t wrapKey[32];
    if (!deriveWrappingKey(wrapKey, error)) return false;

    unsigned long long plainLen = 0;
    int rc = crypto_aead_xchacha20poly1305_ietf_decrypt(
        seed, &plainLen, nullptr,
        ciphertext, sizeof(ciphertext),
        nullptr, 0,
        nonce, wrapKey
    );
    sodium_memzero(wrapKey, sizeof(wrapKey));
    if (rc != 0 || plainLen != SOLANA_SEED_LEN) {
        error = "Wallet unwrap failed";
        return false;
    }
    return true;
}

bool storeSolanaSeed(const uint8_t seed[SOLANA_SEED_LEN], const uint8_t pubkey[SOLANA_PUBKEY_LEN], String& error) {
    uint8_t nonce[SOLANA_KEY_NONCE_LEN];
    uint8_t ciphertext[SOLANA_SEED_LEN + SOLANA_KEY_MAC_LEN];
    uint8_t wrapKey[32];
    randombytes_buf(nonce, sizeof(nonce));
    if (!deriveWrappingKey(wrapKey, error)) return false;

    unsigned long long cipherLen = 0;
    int rc = crypto_aead_xchacha20poly1305_ietf_encrypt(
        ciphertext, &cipherLen,
        seed, SOLANA_SEED_LEN,
        nullptr, 0, nullptr,
        nonce, wrapKey
    );
    sodium_memzero(wrapKey, sizeof(wrapKey));
    if (rc != 0 || cipherLen != sizeof(ciphertext)) {
        error = "Wallet wrap failed";
        return false;
    }

    g_prefs.putBytes("key_nonce", nonce, sizeof(nonce));
    g_prefs.putBytes("key_ct", ciphertext, sizeof(ciphertext));
    g_identity.solanaPublicKey = base58Encode(pubkey, SOLANA_PUBKEY_LEN);
    g_prefs.putString("sol_pub", g_identity.solanaPublicKey);
    return true;
}

bool loadOrCreateSolanaKey(bool rotate, String& error) {
    uint8_t seed[SOLANA_SEED_LEN];
    uint8_t pubkey[SOLANA_PUBKEY_LEN];
    uint8_t secret[SOLANA_SECRET_KEY_LEN];
    bool hasWrappedKey = g_prefs.isKey("key_nonce") || g_prefs.isKey("key_ct");

    if (!rotate && hasWrappedKey) {
        if (!decryptSolanaSeed(seed, error)) return false;
        crypto_sign_seed_keypair(pubkey, secret, seed);
        g_identity.solanaPublicKey = base58Encode(pubkey, SOLANA_PUBKEY_LEN);
        g_prefs.putString("sol_pub", g_identity.solanaPublicKey);
        sodium_memzero(seed, sizeof(seed));
        sodium_memzero(secret, sizeof(secret));
        return true;
    }

    randombytes_buf(seed, sizeof(seed));
    crypto_sign_seed_keypair(pubkey, secret, seed);
    bool ok = storeSolanaSeed(seed, pubkey, error);
    sodium_memzero(seed, sizeof(seed));
    sodium_memzero(secret, sizeof(secret));
    return ok;
}

void clearSolanaKey() {
    g_prefs.remove("key_nonce");
    g_prefs.remove("key_ct");
    g_prefs.remove("sol_pub");
    g_identity.solanaPublicKey = "";
}
