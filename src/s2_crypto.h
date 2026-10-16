// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <stddef.h>
#include <stdint.h>

/**
 * @brief Compute SHA256-HMAC for challenge-response.
 *
 * @param key        HMAC key bytes (token + fingerprint or token + domain)
 * @param key_len    Length of key in bytes
 * @param data       Challenge bytes
 * @param data_len   Length of challenge in bytes
 * @param output     32-byte buffer for the HMAC digest
 */
void s2_sha256_hmac(const uint8_t *key, size_t key_len,
                    const uint8_t *data, size_t data_len,
                    uint8_t output[32]);

/**
 * @brief Compute SHA256 fingerprint of a DER-encoded TLS certificate.
 *
 * @param der_cert   Pointer to DER certificate bytes
 * @param cert_len   Length of certificate in bytes
 * @param output     32-byte buffer for the SHA256 digest
 */
void s2_certificate_fingerprint_sha256(const uint8_t *der_cert, size_t cert_len,
                                uint8_t output[32]);

/**
 * @brief SHA256-HMAC with base64-encoded output.
 *
 * Convenience for the challenge-response JSON response field.
 * Output buffer needs 45 bytes (44 + null).
 *
 * @param key         HMAC key bytes (token || fingerprint)
 * @param key_len     Length of key
 * @param data        Challenge bytes
 * @param data_len    Length of challenge
 * @param output_b64  Buffer for base64 output
 * @param output_len  Size of output_b64 buffer
 */
void s2_sha256_hmac_base64(const uint8_t *key, size_t key_len,
                        const uint8_t *data, size_t data_len,
                        char *output_b64, size_t output_len);

/**
 * @brief Base64-encode 32 bytes.
 *
 * @param src 32 bytes to encode.
 * @param dst Output buffer, needs 45 bytes (44 + null).
 */
void s2_base64_encode_32(const uint8_t src[32], char dst[45]);

/**
 * @brief Base64-decode up to 44 chars into 32 bytes.
 *
 * @param src Base64 string.
 * @param dst 32-byte output buffer.
 *
 * @return Number of decoded bytes.
 */
int s2_base64_decode_32(const char *src, uint8_t dst[32]);

