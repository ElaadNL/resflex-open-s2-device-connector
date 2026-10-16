// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

#include "s2_crypto.h"

#include <psa/crypto.h>
#include <string.h>

void s2_sha256_hmac(const uint8_t *key, size_t key_len,
                    const uint8_t *data, size_t data_len,
                    uint8_t output[32]) {
    psa_key_attributes_t attr = PSA_KEY_ATTRIBUTES_INIT;
    psa_set_key_type(&attr, PSA_KEY_TYPE_HMAC);
    psa_set_key_usage_flags(&attr, PSA_KEY_USAGE_SIGN_MESSAGE);
    psa_set_key_algorithm(&attr, PSA_ALG_HMAC(PSA_ALG_SHA_256));

    psa_key_id_t kid;
    psa_import_key(&attr, key, key_len, &kid);

    size_t mac_len;
    psa_mac_compute(kid, PSA_ALG_HMAC(PSA_ALG_SHA_256),
                    data, data_len, output, 32, &mac_len);

    psa_destroy_key(kid);
}

void s2_certificate_fingerprint_sha256(const uint8_t *der_cert, size_t cert_len,
                                uint8_t output[32]) {
    size_t hash_len;
    psa_hash_compute(PSA_ALG_SHA_256, der_cert, cert_len,
                     output, 32, &hash_len);
}

void s2_base64_encode_32(const uint8_t src[32], char dst[45]) {
    static const char t[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    int o = 0;
    for (int i = 0; i < 32; i += 3) {
        uint32_t v = (uint32_t)src[i] << 16;
        if (i + 1 < 32) v |= (uint32_t)src[i + 1] << 8;
        if (i + 2 < 32) v |= (uint32_t)src[i + 2];
        dst[o++] = t[(v >> 18) & 0x3F];
        dst[o++] = t[(v >> 12) & 0x3F];
        if (i + 1 < 32) dst[o++] = t[(v >> 6) & 0x3F];
        if (i + 2 < 32) dst[o++] = t[v & 0x3F];
    }
    dst[o++] = '=';
    dst[o] = '\0';
}

int s2_base64_decode_32(const char *src, uint8_t dst[32]) {
    static const signed char dec[128] = {
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,
        52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-1,-1,-1,
        -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,
        15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
        -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,
        41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1,
    };
    if (!src) return 0;
    int len = strlen(src);
    if (len < 43 || len > 44) return 0;

    int out = 0;
    for (int i = 0; i < len; i += 4) {
        int a = dec[(unsigned char)src[i]];
        int b = dec[(unsigned char)src[i + 1]];
        int c = (i + 2 < len && src[i + 2] != '=') ? dec[(unsigned char)src[i + 2]] : 0;
        int d = (i + 3 < len && src[i + 3] != '=') ? dec[(unsigned char)src[i + 3]] : 0;
        uint32_t v = ((uint32_t)a << 18) | ((uint32_t)b << 12) | ((uint32_t)c << 6) | (uint32_t)d;
        dst[out++] = (v >> 16) & 0xFF;
        if (out < 32) dst[out++] = (v >> 8) & 0xFF;
        if (out < 32) dst[out++] = v & 0xFF;
    }
    return out;
}

void s2_sha256_hmac_base64(const uint8_t *key, size_t key_len,
                        const uint8_t *data, size_t data_len,
                        char *output_b64, size_t output_len) {
    uint8_t hmac[32];
    s2_sha256_hmac(key, key_len, data, data_len, hmac);
    if (output_len >= 45) s2_base64_encode_32(hmac, output_b64);
}
