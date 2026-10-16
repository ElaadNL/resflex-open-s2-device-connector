// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/**
 * @brief Generate a cryptographically random challenge for HMAC challenge-response.
 *
 * Fills output with 32 random bytes. Output buffer must be at least 32 bytes.
 */
void s2_pairing_generate_challenge(uint8_t challenge[32]);

/**
 * @brief Compute the HMAC response for a given challenge.
 *
 * For LAN:  key = pairing_token (ASCII) || cert_fingerprint (binary, 32 bytes)
 * For WAN:  key = pairing_token (ASCII) || domain (ASCII)
 *
 * @param pairing_token     The pairing token string
 * @param cert_fingerprint  SHA256 fingerprint of server TLS cert (NULL for WAN)
 * @param domain            Server domain name (NULL for LAN)
 * @param challenge         Challenge bytes
 * @param challenge_len     Length of challenge
 * @param output_b64        Buffer for base64-encoded response (45 bytes)
 * @param output_len        Size of output_b64 buffer
 *
 * Note: only SHA256 is currently supported (S2 Connect v1.0 defines no other
 * hashing algorithm).
 */
void s2_pairing_compute_response(const char *pairing_token,
                                 const uint8_t *cert_fingerprint,
                                 const char *domain,
                                 const uint8_t *challenge, size_t challenge_len,
                                 char *output_b64, size_t output_len);

/**
 * @brief Validate a received challenge response against our own computation.
 *
 * @return true if responses match, false otherwise.
 */
bool s2_pairing_verify_response(const char *pairing_token,
                                const uint8_t *cert_fingerprint,
                                const char *domain,
                                const uint8_t *challenge, size_t challenge_len,
                                const char *received_response_b64);
