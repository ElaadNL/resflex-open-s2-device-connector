// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

#include "unity.h"
#include "s2_crypto.h"
#include <string.h>

// ---- s2_base64_encode_32 ----

TEST_CASE("encodes all zeros", "[crypto]") {
    uint8_t src[32] = {0};
    char dst[45];
    s2_base64_encode_32(src, dst);
    TEST_ASSERT_EQUAL_STRING("AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=", dst);
}

TEST_CASE("encodes all 0xFF", "[crypto]") {
    uint8_t src[32];
    memset(src, 0xFF, 32);
    char dst[45];
    s2_base64_encode_32(src, dst);
    TEST_ASSERT_EQUAL_STRING("//////////////////////////////////////////8=", dst);
}

TEST_CASE("roundtrip encode then decode", "[crypto]") {
    uint8_t orig[32], decoded[32];
    for (int i = 0; i < 32; i++) orig[i] = (uint8_t)(i * 7 + 13);
    char enc[45];
    s2_base64_encode_32(orig, enc);
    int len = s2_base64_decode_32(enc, decoded);
    TEST_ASSERT_EQUAL(32, len);
    TEST_ASSERT_EQUAL_MEMORY(orig, decoded, 32);
}

// ---- s2_base64_decode_32 ----

TEST_CASE("null input returns 0", "[crypto]") {
    uint8_t dst[32];
    TEST_ASSERT_EQUAL(0, s2_base64_decode_32(NULL, dst));
}

TEST_CASE("too short returns 0", "[crypto]") {
    uint8_t dst[32];
    TEST_ASSERT_EQUAL(0, s2_base64_decode_32("short", dst));
}

TEST_CASE("too long returns 0", "[crypto]") {
    uint8_t dst[32];
    TEST_ASSERT_EQUAL(0, s2_base64_decode_32("this-string-is-way-too-long-to-be-valid-base64-for-32-bytes", dst));
}
