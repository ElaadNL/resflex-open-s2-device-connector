// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

#include "s2_pairing_challenge.h"
#include "s2_crypto.h"

#include <psa/crypto.h>
#include <string.h>

void s2_pairing_generate_challenge(uint8_t challenge[32]) {
    psa_generate_random(challenge, 32);
}

void s2_pairing_compute_response(const char *pairing_token,
                                 const uint8_t *cert_fingerprint,
                                 const char *domain,
                                 const uint8_t *challenge, size_t challenge_len,
                                 char *output_b64, size_t output_len) {
    size_t token_len = 0;
    if (pairing_token) {
        token_len = strlen(pairing_token);
    }
    size_t extra_len;
    if (cert_fingerprint) {
        extra_len = 32;
    } else if (domain) {
        extra_len = strlen(domain);
    } else {
        extra_len = 0;
    }
    size_t msg_len = token_len + extra_len;

    uint8_t msg[256];
    if (token_len > 0)
        memcpy(msg, pairing_token, token_len);
    if (cert_fingerprint) {
        memcpy(msg + token_len, cert_fingerprint, 32);
    } else if (domain && extra_len > 0) {
        memcpy(msg + token_len, domain, extra_len);
    }

    // only SHA256 is supported: S2 Connect v1.0 defines no other hash
    s2_sha256_hmac_base64(challenge, challenge_len, msg, msg_len, output_b64, output_len);
}

bool s2_pairing_verify_response(const char *pairing_token,
                                const uint8_t *cert_fingerprint,
                                const char *domain,
                                const uint8_t *challenge, size_t challenge_len,
                                const char *received_response_b64) {
    char expected[45];
    s2_pairing_compute_response(pairing_token, cert_fingerprint, domain,
                                challenge, challenge_len, expected, sizeof(expected));

    return strcmp(expected, received_response_b64) == 0;
}
