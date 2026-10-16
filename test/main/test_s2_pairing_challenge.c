// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

#include "unity.h"
#include "s2_pairing_challenge.h"
#include <string.h>

TEST_CASE("compute response with cert fingerprint", "[pairing challenge]") {
    uint8_t fp[32], chal[32];
    memset(fp, 0xAA, 32);
    memset(chal, 0xBB, 32);
    char out[45];
    s2_pairing_compute_response("token", fp, NULL, chal, 32, out, sizeof(out));
    TEST_ASSERT_NOT_EQUAL('\0', out[0]);
}

TEST_CASE("compute response with domain", "[pairing challenge]") {
    uint8_t chal[32];
    memset(chal, 0xBB, 32);
    char out[45];
    s2_pairing_compute_response("token", NULL, "example.com", chal, 32, out, sizeof(out));
    TEST_ASSERT_NOT_EQUAL('\0', out[0]);
}

TEST_CASE("compute response with null token and empty extras", "[pairing challenge]") {
    uint8_t chal[32] = {0};
    char out[45];
    s2_pairing_compute_response(NULL, NULL, NULL, chal, 0, out, sizeof(out));
}

TEST_CASE("verify matching response", "[pairing challenge]") {
    uint8_t fp[32], chal[32];
    memset(fp, 0xAA, 32);
    memset(chal, 0xBB, 32);
    char resp[45];
    s2_pairing_compute_response("tok", fp, NULL, chal, 32, resp, sizeof(resp));
    TEST_ASSERT_TRUE(s2_pairing_verify_response("tok", fp, NULL, chal, 32, resp));
}

TEST_CASE("verify mismatched response", "[pairing challenge]") {
    uint8_t fp[32], chal[32];
    memset(fp, 0xAA, 32);
    memset(chal, 0xBB, 32);
    TEST_ASSERT_FALSE(s2_pairing_verify_response("tok", fp, NULL, chal, 32, "wrong"));
}
