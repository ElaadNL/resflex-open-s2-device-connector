// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

#include "unity.h"
#include "s2_common.h"
#include "MockcJSON_mock.h"
#include <string.h>

// ______________________ s2_generate_uuid ______________________

TEST_CASE("Generates a valid UUID v4 string", "[s2_common - generate_uuid]") {
    char buf[37];
    s2_generate_uuid(buf);
    TEST_ASSERT_EQUAL(36, strlen(buf));
    TEST_ASSERT_EQUAL('-', buf[8]);
    TEST_ASSERT_EQUAL('-', buf[13]);
    TEST_ASSERT_EQUAL('-', buf[18]);
    TEST_ASSERT_EQUAL('-', buf[23]);
    TEST_ASSERT_EQUAL('4', buf[14]); // version 4
    TEST_ASSERT_TRUE(buf[19] == '8' || buf[19] == '9' ||
                     buf[19] == 'a' || buf[19] == 'b'); // RFC 4122 variant
}

// ______________________ s2_format_iso8601 ______________________

TEST_CASE("Formats epoch 0 as 1970-01-01T00:00:00Z", "[s2_common - format_iso8601]") {
    char buf[32];
    TEST_ASSERT_TRUE(s2_format_iso8601(0, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("1970-01-01T00:00:00Z", buf);
}

// ______________________ s2_commodity_quantity_to_string ______________________

TEST_CASE("Returns correct string for known quantity", "[s2_common - commodity_quantity_to_string]") {
    TEST_ASSERT_EQUAL_STRING("ELECTRIC.POWER.L1", s2_commodity_quantity_to_string(S2_COMMODITY_QUANTITY_ELECTRIC_POWER_L1));
    TEST_ASSERT_EQUAL_STRING("ELECTRIC.POWER.3_PHASE_SYMMETRIC", s2_commodity_quantity_to_string(S2_COMMODITY_QUANTITY_ELECTRIC_POWER_3_PHASE_SYMMETRIC));
}

TEST_CASE("Returns NULL for unknown quantity", "[s2_common - commodity_quantity_to_string]") {
    TEST_ASSERT_NULL(s2_commodity_quantity_to_string((s2_commodity_quantity_t)999));
}

// ______________________ s2_commodity_to_string ______________________

TEST_CASE("Returns correct string for known commodity", "[s2_common - commodity_to_string]") {
    TEST_ASSERT_EQUAL_STRING("ELECTRICITY", s2_commodity_to_string(S2_COMMODITY_ELECTRICITY));
    TEST_ASSERT_NULL(s2_commodity_to_string((s2_commodity_t)999));
}

// ______________________ s2_role_type_to_string ______________________

TEST_CASE("Returns correct string for known role type", "[s2_common - role_type_to_string]") {
    TEST_ASSERT_EQUAL_STRING("ENERGY_CONSUMER", s2_role_type_to_string(S2_ROLE_TYPE_ENERGY_CONSUMER));
    TEST_ASSERT_NULL(s2_role_type_to_string((s2_role_type_t)999));
}

// ______________________ s2_role_to_string / s2_role_to_enum ______________________

TEST_CASE("Returns correct string for CEM and RM roles", "[s2_common - role_to_string]") {
    TEST_ASSERT_EQUAL_STRING("CEM", s2_role_to_string(S2_ROLE_CEM));
    TEST_ASSERT_EQUAL_STRING("RM",  s2_role_to_string(S2_ROLE_RM));
    TEST_ASSERT_NULL(s2_role_to_string((s2_role)999));
}

TEST_CASE("Converts valid role strings to enum", "[s2_common - role_to_enum]") {
    s2_role out;
    TEST_ASSERT_TRUE(s2_role_to_enum("CEM", &out));
    TEST_ASSERT_EQUAL(S2_ROLE_CEM, out);
    TEST_ASSERT_TRUE(s2_role_to_enum("RM", &out));
    TEST_ASSERT_EQUAL(S2_ROLE_RM, out);
    TEST_ASSERT_FALSE(s2_role_to_enum("UNKNOWN", &out));
}

// ______________________ s2_deployment_to_string / s2_deployment_to_enum ______________________

TEST_CASE("Returns correct string for deployment, NULL for unknown", "[s2_common - deployment_to_string]") {
    TEST_ASSERT_EQUAL_STRING("LAN", s2_deployment_to_string(S2_DEPLOYMENT_LAN));
    TEST_ASSERT_EQUAL_STRING("WAN", s2_deployment_to_string(S2_DEPLOYMENT_WAN));
    TEST_ASSERT_NULL(s2_deployment_to_string((s2_deployment)999));
}

TEST_CASE("Converts valid deployment strings to enum", "[s2_common - deployment_to_enum]") {
    s2_deployment out;
    TEST_ASSERT_TRUE(s2_deployment_to_enum("LAN", &out));
    TEST_ASSERT_EQUAL(S2_DEPLOYMENT_LAN, out);
    TEST_ASSERT_TRUE(s2_deployment_to_enum("WAN", &out));
    TEST_ASSERT_EQUAL(S2_DEPLOYMENT_WAN, out);
    TEST_ASSERT_FALSE(s2_deployment_to_enum("UNKNOWN", &out));
}

// ______________________ s2_is_valid_json ______________________

TEST_CASE("Returns false for NULL input", "[s2_common - is_valid_json]") {
    TEST_ASSERT_FALSE(s2_is_valid_json(NULL));
}

TEST_CASE("Returns true when cJSON_Parse succeeds", "[s2_common - is_valid_json]") {
    cJSON fake = {0};
    cJSON_Parse_ExpectAndReturn("{}", &fake);
    cJSON_Delete_Ignore();
    TEST_ASSERT_TRUE(s2_is_valid_json("{}"));
}

TEST_CASE("Returns false when cJSON_Parse fails", "[s2_common - is_valid_json]") {
    cJSON_Parse_ExpectAndReturn("bad", NULL);
    cJSON_GetErrorPtr_IgnoreAndReturn(NULL);
    TEST_ASSERT_FALSE(s2_is_valid_json("bad"));
}

// ______________________ s2_node_to_json ______________________

TEST_CASE("Returns NULL for invalid role", "[s2_common - node_to_json]") {
    cJSON fake = {0};
    cJSON_CreateObject_ExpectAndReturn(&fake);
    cJSON_Delete_Ignore();
    s2_node_t node = {.role = (s2_role)999, .deployment = S2_DEPLOYMENT_LAN};
    TEST_ASSERT_NULL(s2_node_to_json(node));
}

TEST_CASE("Returns NULL for unknown deployment", "[s2_common - node_to_json]") {
    cJSON fake_root = {0};
    cJSON_CreateObject_ExpectAndReturn(&fake_root);
    cJSON_AddStringToObject_IgnoreAndReturn((cJSON *)1); // role add succeeds
    cJSON_Delete_Ignore();
    s2_node_t node = {.role = S2_ROLE_RM, .deployment = (s2_deployment)999};
    TEST_ASSERT_NULL(s2_node_to_json(node));
}

// Happy path: valid role + deployment, all cJSON calls succeed -> returns non-NULL object.
TEST_CASE("Returns JSON object for valid node", "[s2_common - node_to_json]") {
    cJSON fake_root = {0};
    cJSON fake_array = {0};
    cJSON_CreateObject_ExpectAndReturn(&fake_root);
    cJSON_AddStringToObject_IgnoreAndReturn((cJSON *)1); // all string fields succeed
    cJSON_AddNullToObject_IgnoreAndReturn((cJSON *)1);   // logoUri + userDefinedName (empty -> null)
    cJSON_CreateArray_ExpectAndReturn(&fake_array);
    cJSON_AddItemToObject_IgnoreAndReturn(1);             // versions array added to root
    s2_node_t node = {.role = S2_ROLE_RM, .deployment = S2_DEPLOYMENT_LAN};
    TEST_ASSERT_NOT_NULL(s2_node_to_json(node));
}

// ______________________ s2_parse_iso8601 ______________________

TEST_CASE("Returns 0 for invalid string", "[s2_common - parse_iso8601]") {
    TEST_ASSERT_EQUAL(0, s2_parse_iso8601("not-a-date"));
}

TEST_CASE("Returns non-zero for valid ISO 8601 string", "[s2_common - parse_iso8601]") {
    TEST_ASSERT_NOT_EQUAL(0, s2_parse_iso8601("2024-01-15T10:30:00"));
}

// Out-of-range fields must be rejected: the month indexes mdays[] inside the parser.
TEST_CASE("Returns 0 for out of range date fields", "[s2_common - parse_iso8601]") {
    TEST_ASSERT_EQUAL(0, s2_parse_iso8601("2024-13-15T10:30:00"));     // month > 12
    TEST_ASSERT_EQUAL(0, s2_parse_iso8601("2024-00-15T10:30:00"));     // month < 1
    TEST_ASSERT_EQUAL(0, s2_parse_iso8601("2024-01-99T10:30:00"));     // day > 31
    TEST_ASSERT_EQUAL(0, s2_parse_iso8601("2024-01-15T99:30:00"));     // hour > 23
    TEST_ASSERT_EQUAL(0, s2_parse_iso8601("2024-01-15T10:99:00"));     // minute > 59
    TEST_ASSERT_EQUAL(0, s2_parse_iso8601("2024-01-15T10:30:99"));     // second > 60
    TEST_ASSERT_EQUAL(0, s2_parse_iso8601("1969-01-01T00:00:00"));     // year < 1970
    TEST_ASSERT_EQUAL(0, s2_parse_iso8601("99999999-01-01T00:00:00")); // implausible year from the report
}
