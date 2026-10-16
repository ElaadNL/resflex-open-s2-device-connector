// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

#include "unity.h"
#include "s2_client.h"
#include "s2_client_pairing.h"
#include "s2_client_http.h"

#include "fixtures.h"
#include "json_stubs.h"
#include "freertos_stubs.h"
#include "Mockesp_http_client_mock.h"
#include "MockcJSON_mock.h"

#include "esp_log.h"

#define SERVER_URL "https://homeassistant.local:8123/api/s2_pebc"

#define MOCK_HTTP_CLIENT_HANDLE ((esp_http_client_handle_t)0xdeadbeef)
#define MOCK_CJSON ((cJSON *)0xdeadbeef)

// Helper macros to reduce duplicate code

/** Test if function returns expected error on HTTP failure */
#define TEST_HTTP_FAILURE(func_call) do { \
    mock_json_init(); \
    xQueueGenericSend_ExpectAnyArgsAndReturn(pdFALSE); \
    vQueueDelete_ExpectAnyArgs(); \
    cJSON_Delete_StopIgnore(); \
    cJSON_Delete_ExpectAnyArgs(); \
    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT, (func_call)); \
} while(0)

/** Test if function returns expected result on HTTP success */
#define TEST_HTTP_SUCCESS(func_call) do { \
    mock_json_init(); \
    cJSON_Delete_StopIgnore(); \
    cJSON_Delete_ExpectAnyArgs(); \
    freertos_stubs_queue_send_set_request_result(ESP_OK); \
    xQueueSemaphoreTake_ExpectAnyArgsAndReturn(pdTRUE); \
    TEST_ASSERT_EQUAL(ESP_OK, (func_call)); \
} while(0)

/** Test if function returns expected result if JSON object creation fails */
#define TEST_JSON_OBJECT_FAILURE(func_call, expected_result) do { \
    mock_json_init(); \
    cJSON_CreateObject_StopIgnore(); \
    cJSON_CreateObject_ExpectAndReturn(NULL); \
    TEST_ASSERT_EQUAL(expected_result, (func_call)); \
} while(0)

/** Test if function returns expected result if adding string of specific key to JSON */
#define TEST_JSON_STRING_KEY_FAILURE(func_call, key) do { \
    mock_json_init(); \
    cJSON_AddStringToObject_fail_on_key(key); \
    TEST_ASSERT_NULL((func_call)); \
    TEST_ASSERT_TRUE(cJSON_AddStringToObject_failed_on_key(key)); \
} while(0)

/** Test if function returns expected result if adding object of specific key to JSON */
#define TEST_JSON_OBJECT_KEY_FAILURE(func_call, key) do { \
    mock_json_init(); \
    cJSON_AddItemToObject_fail_on_key(key); \
    TEST_ASSERT_NULL((func_call)); \
    TEST_ASSERT_TRUE(cJSON_AddItemToObject_failed_on_key(key)); \
} while(0)

/** Helper function to obtain a client handle */
/** Minimal dummy callbacks required by s2_client_init validation */
static void dummy_on_session_ready(void) {}
static void dummy_on_request_measurement(s2_commodity_quantity_t q) {}
static void dummy_on_instruction(s2_commodity_quantity_t q, float upper, float lower) {}
static void dummy_on_request_param(s2_pebc_param_t p, s2_commodity_quantity_t q, bool abnormal) {}

static s2_client_handle_t get_client_handle() {
    // Ignore client handle mocks as this function is tested in other test file
    mock_queue_init(CREATE_AND_IGNORE);
    mock_task_init(CREATE_AND_IGNORE);
    MockHttpClient http_client; mock_http_client_init(&http_client, CREATE_AND_IGNORE);

    // Create example configuration for the S2 client
    s2_client_config_t cfg = {
        .node = {
            .id = "abff635e-cd69-4350-b10f-a990b7920ba0",
            .brand = "test",
            .deployment = S2_DEPLOYMENT_LAN,
            .logo_uri = "test",
            .model = "test",
            .role = S2_ROLE_RM,
            .supported_versions = {"0.0.1-beta"},
            .type = "test",
            .user_defined_name = "test"
        },
        .server_config = {
            .base_url = SERVER_URL,
            .interface = "eth0"
        },
        .rm_config = {
            .control_type = S2_CONTROL_TYPE_PEBC,
            .roles = {{.commodity = S2_COMMODITY_ELECTRICITY, .role_type = S2_ROLE_TYPE_ENERGY_CONSUMER}},
            .roles_count = 1,
            .commodity_quantities = {S2_COMMODITY_QUANTITY_ELECTRIC_POWER_L1},
            .commodity_quantities_count = 1,
            .on_request_measurement = dummy_on_request_measurement,
            .on_session_ready = dummy_on_session_ready,
            .pebc = {
                .default_limits = {{.commodity_quantity = S2_COMMODITY_QUANTITY_ELECTRIC_POWER_L1}},
                .default_limits_count = 1,
                .on_instruction = dummy_on_instruction,
                .on_request_param = dummy_on_request_param,
            },
        }
    };

    return s2_client_init(&cfg);
}

/**
 * @brief Helper to build a mock HTTP response context from a payload string
 *
 * Copies @p payload into the response context's buffer and sets the length
 * accordingly. Use this to inject different payloads into the parser under
 * test
 */
static void mock_http_response_ctx(s2_client_http_response_ctx_t *ctx, const char *payload) {
    size_t len = strlen(payload);
    TEST_ASSERT_LESS_THAN(sizeof(ctx->buffer), len);

    memcpy(ctx->buffer, payload, len);
    ctx->buffer[len] = '\0';
    ctx->len = (int)len;
    ctx->is_complete = true;
}

// ___________________________ s2_client_prepare_pairing ___________________________

TEST_CASE("Correctly handles missing client handle", "[S2 client pairing - Prepare pairing]") {
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, s2_client_prepare_pairing(NULL, NULL));
}

TEST_CASE("Handles body construction failure", "[S2 client pairing - Prepare pairing]") {
    s2_client_handle_t handle = get_client_handle();
    TEST_JSON_OBJECT_FAILURE(s2_client_prepare_pairing(handle, NULL), ESP_FAIL);
}

TEST_CASE("Handles HTTP request failure", "[S2 client pairing - Prepare pairing]") {
    s2_client_handle_t handle = get_client_handle();
    TEST_HTTP_FAILURE(s2_client_prepare_pairing(handle, NULL));
}

TEST_CASE("Returns OK if request was successful", "[S2 client pairing - Prepare pairing]") {
    s2_client_handle_t handle = get_client_handle();
    TEST_HTTP_SUCCESS(s2_client_prepare_pairing(handle, NULL));
}

// ___________________________ s2_client_build_prepare_pairing_body ___________________________

TEST_CASE("Fails if JSON object could not be created", "[S2 client pairing - Prepare pairing body]") {
    s2_client_handle_t handle = get_client_handle();
    TEST_JSON_OBJECT_FAILURE(s2_client_build_prepare_pairing_body(handle, NULL), NULL);
}

TEST_CASE("Fails if S2 node description object could not be created", "[S2 client pairing - Prepare pairing body]") {
    // Initialize JSON mocks
    mock_json_init();

    // Root JSON object creation should succeed, but S2 node object creation should fail
    cJSON_CreateObject_StopIgnore();
    cJSON_CreateObject_IgnoreAndReturn(MOCK_CJSON); // Root object
    cJSON_CreateObject_ExpectAndReturn(NULL); // S2 node object

    // Call the function that is tested
    s2_client_handle_t handle = get_client_handle();
    TEST_ASSERT_NULL(s2_client_build_prepare_pairing_body(handle, NULL));
}

TEST_CASE("Fails if clientNodeDescription could not be added to JSON", "[S2 client pairing - Prepare pairing body]") {
    s2_client_handle_t handle = get_client_handle();
    cJSON_Delete_StopIgnore();
    cJSON_Delete_ExpectAnyArgs();
    TEST_JSON_OBJECT_KEY_FAILURE(s2_client_build_prepare_pairing_body(handle, NULL), "clientNodeDescription");
}

TEST_CASE("JSON has correct format if successful", "[S2 client pairing - Prepare pairing body]") {
    // Initialise JSON mocks
    mock_json_init();

    // Call the function that is tested
    s2_client_handle_t handle = get_client_handle();
    TEST_ASSERT_NOT_NULL(s2_client_build_prepare_pairing_body(handle, NULL));

    // Verify body contains the expected top-level entries
    TEST_ASSERT_TRUE_MESSAGE(
        cJSON_was_item_added("clientNodeDescription"),
        "Expected clientNodeDescription to be added as a child object"
    );

    // Verify body contains the expected node description fields (added by
    // s2_node_to_json) using the configuration from get_client_handle()
    TEST_ASSERT_TRUE(cJSON_was_string_added("brand", handle->node.brand));
    TEST_ASSERT_TRUE(cJSON_was_string_added("type", handle->node.type));
    TEST_ASSERT_TRUE(cJSON_was_string_added("modelName", handle->node.model));
    TEST_ASSERT_TRUE(cJSON_was_string_added("role", "RM"));
    TEST_ASSERT_TRUE(cJSON_was_string_added("deployment", "LAN"));
    TEST_ASSERT_TRUE(cJSON_was_item_added("supportedS2MessageVersions"));
}

// ___________________________ s2_client_cancel_prepare_pairing ___________________________

TEST_CASE("Correctly handles missing client handle", "[S2 client pairing - Cancel prepare pairing]") {
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, s2_client_cancel_prepare_pairing(NULL, NULL));
}

TEST_CASE("Handles body construction failure", "[S2 client pairing - Cancel prepare pairing]") {
    cJSON_CreateObject_ExpectAndReturn(NULL);

    s2_client_handle_t handle = get_client_handle();
    TEST_ASSERT_EQUAL(ESP_FAIL, s2_client_cancel_prepare_pairing(handle, NULL));
}

TEST_CASE("HTTP request failure causes function to fail", "[S2 client pairing - Cancel prepare pairing]") {
    s2_client_handle_t handle = get_client_handle();
    TEST_HTTP_FAILURE(s2_client_cancel_prepare_pairing(handle, NULL));
}

TEST_CASE("Returns OK if request was successful", "[S2 client pairing - Cancel prepare pairing]") {
    s2_client_handle_t handle = get_client_handle();
    TEST_HTTP_SUCCESS(s2_client_cancel_prepare_pairing(handle, NULL));
}

// ___________________________ s2_client_parse_request_pairing_response ___________________________

void test_invalid_key_for_request_pairing_parser(const char *key) {
    // Initialise JSON mocks
    mock_json_init();

    // Mock "pairingAttemptId" as valid string (required to pass first check)
    cJSON attempt_id_node;
    mock_cjson_string(&attempt_id_node, "attempt-123");
    cJSON_GetObjectItem_return_for_key("pairingAttemptId", &attempt_id_node);

    // Mock "serverNodeDescription" as valid object (required to pass object check)
    cJSON server_node_obj = {0};
    server_node_obj.type = cJSON_Object;
    cJSON_GetObjectItem_return_for_key("serverNodeDescription", &server_node_obj);

    // Sub-field nodes (must outlive the function call, hence static)
    static cJSON id_node, brand_node, type_node, model_node, name_node, role_node;
    mock_cjson_string(&id_node, "server_id");
    mock_cjson_string(&brand_node, "server_brand");
    mock_cjson_string(&type_node, "server_type");
    mock_cjson_string(&model_node, "server_model");
    mock_cjson_string(&name_node, "server_name");
    mock_cjson_string(&role_node, "RM");

    // Register all sub-fields as valid strings
    cJSON_GetObjectItem_return_for_key("id", &id_node);
    cJSON_GetObjectItem_return_for_key("brand", &brand_node);
    cJSON_GetObjectItem_return_for_key("type", &type_node);
    cJSON_GetObjectItem_return_for_key("modelName", &model_node);
    cJSON_GetObjectItem_return_for_key("userDefinedName", &name_node);
    cJSON_GetObjectItem_return_for_key("role", &role_node);

    // If the tested key is a REQUIRED sub-field, make cJSON_IsString fail on it.
    // Required sub-fields: id, brand, type, modelName, role (checked together).
    // userDefinedName is OPTIONAL; making it fail won't cause ESP_FAIL.
    if (strcmp(key, "id") == 0) {
        cJSON_IsString_fail_for_node(&id_node);
    } else if (strcmp(key, "brand") == 0) {
        cJSON_IsString_fail_for_node(&brand_node);
    } else if (strcmp(key, "type") == 0) {
        cJSON_IsString_fail_for_node(&type_node);
    } else if (strcmp(key, "modelName") == 0) {
        cJSON_IsString_fail_for_node(&model_node);
    } else if (strcmp(key, "role") == 0) {
        cJSON_IsString_fail_for_node(&role_node);
    }
    // For all other keys (optional sub-fields or keys not in the function),
    // make the required "pairingAttemptId" fail instead
    else {
        cJSON_IsString_fail_for_node(&attempt_id_node);
    }

    // Mock optional HMAC fields as NULL so they are safely skipped
    cJSON_GetObjectItem_return_for_key("clientHmacChallengeResponse", NULL);
    cJSON_GetObjectItem_return_for_key("serverHmacChallenge", NULL);

    // Make sure JSON object is freed from memory
    cJSON_Delete_StopIgnore();
    cJSON_Delete_ExpectAnyArgs();

    // Verify expected error is returned
    s2_client_http_response_ctx_t ctx = {0};
    mock_http_response_ctx(&ctx, "{}");
    s2_client_request_pairing_response_t out = {0};
    TEST_ASSERT_EQUAL(ESP_FAIL, s2_client_parse_request_pairing_response(&ctx, &out));
}

TEST_CASE("Fails when JSON response could not be parsed", "[S2 client pairing - Parse request pairing response]") {
    // Build a response context with an invalid payload
    s2_client_http_response_ctx_t ctx = {0};
    mock_http_response_ctx(&ctx, "not-valid-json");

    // Make cJSON_ParseWithLength return NULL to mock failure
    cJSON_ParseWithLength_ExpectAndReturn(ctx.buffer, ctx.len, NULL);

    // Call the function under test and verify ESP_FAIL is returned
    s2_client_request_pairing_response_t out = {0};
    TEST_ASSERT_EQUAL(ESP_FAIL, s2_client_parse_request_pairing_response(&ctx, &out));
}

TEST_CASE("Fails when access token is invalid", "[S2 client pairing - Parse request pairing response]") {
    test_invalid_key_for_request_pairing_parser("accessToken");
}

TEST_CASE("Fails when S2 server node description is invalid", "[S2 client pairing - Parse request pairing response]") {
    // Initialise JSON mocks
    mock_json_init();

    // Mock "pairingAttemptId" as valid string (required to pass first check
    // so we can reach the cJSON_IsObject check for serverNodeDescription)
    cJSON attempt_id_node;
    mock_cjson_string(&attempt_id_node, "attempt-123");
    cJSON_GetObjectItem_return_for_key("pairingAttemptId", &attempt_id_node);

    // Simulate cJSON_IsObject check to fail for S2 server node description
    cJSON_IsObject_StopIgnore();
    cJSON_IsObject_ExpectAnyArgsAndReturn(0);

    // Parsed JSON object should be freed on failure
    cJSON_Delete_StopIgnore();
    cJSON_Delete_ExpectAnyArgs();

    // Verify expected error code is returned
    s2_client_http_response_ctx_t ctx = {0};
    mock_http_response_ctx(&ctx, "{}");
    s2_client_request_pairing_response_t out = {0};
    TEST_ASSERT_EQUAL(ESP_FAIL, s2_client_parse_request_pairing_response(&ctx, &out));
}

TEST_CASE("Fails when S2 server id is invalid", "[S2 client pairing - Parse request pairing response]") {
    test_invalid_key_for_request_pairing_parser("s2ServerNodeId");
}

TEST_CASE("Fails when S2 server brand is invalid", "[S2 client pairing - Parse request pairing response]") {
    test_invalid_key_for_request_pairing_parser("brand");
}

TEST_CASE("Fails when S2 server logo URI is invalid", "[S2 client pairing - Parse request pairing response]") {
    test_invalid_key_for_request_pairing_parser("logoUri");
}

TEST_CASE("Fails when S2 server type is invalid", "[S2 client pairing - Parse request pairing response]") {
    test_invalid_key_for_request_pairing_parser("type");
}

TEST_CASE("Fails when S2 server model is invalid", "[S2 client pairing - Parse request pairing response]") {
    test_invalid_key_for_request_pairing_parser("modelName");
}

TEST_CASE("Fails when S2 server user defined name is invalid", "[S2 client pairing - Parse request pairing response]") {
    test_invalid_key_for_request_pairing_parser("userDefinedName");
}

TEST_CASE("Fails when S2 server role is invalid", "[S2 client pairing - Parse request pairing response]") {
    test_invalid_key_for_request_pairing_parser("role");
}

TEST_CASE("Fails when S2 server deployment is invalid", "[S2 client pairing - Parse request pairing response]") {
    test_invalid_key_for_request_pairing_parser("deployment");
}

TEST_CASE("Fails when S2 server versions is invalid", "[S2 client pairing - Parse request pairing response]") {
    // Initialise JSON mocks
    mock_json_init();

    // Mock "pairingAttemptId" as valid string (required to pass first check)
    cJSON attempt_id_node;
    mock_cjson_string(&attempt_id_node, "attempt-123");
    cJSON_GetObjectItem_return_for_key("pairingAttemptId", &attempt_id_node);

    // Mock "serverNodeDescription" as valid object (required to pass object check)
    cJSON server_node_obj = {0};
    server_node_obj.type = cJSON_Object;
    cJSON_GetObjectItem_return_for_key("serverNodeDescription", &server_node_obj);

    // Sub-field nodes (must outlive the function call, hence static)
    static cJSON id_node, brand_node, type_node, model_node, name_node, role_node;
    mock_cjson_string(&id_node, "server_id");
    mock_cjson_string(&brand_node, "server_brand");
    mock_cjson_string(&type_node, "server_type");
    mock_cjson_string(&model_node, "server_model");
    mock_cjson_string(&name_node, "server_name");
    mock_cjson_string(&role_node, "RM");

    // Register all sub-fields as valid strings
    cJSON_GetObjectItem_return_for_key("id", &id_node);
    cJSON_GetObjectItem_return_for_key("brand", &brand_node);
    cJSON_GetObjectItem_return_for_key("type", &type_node);
    cJSON_GetObjectItem_return_for_key("modelName", &model_node);
    cJSON_GetObjectItem_return_for_key("userDefinedName", &name_node);
    cJSON_GetObjectItem_return_for_key("role", &role_node);

    // The function does not call cJSON_IsArray, so make it fail on pairingAttemptId instead
    cJSON_IsString_fail_for_node(&attempt_id_node);

    // Parsed JSON object should be freed on failure
    cJSON_Delete_StopIgnore();
    cJSON_Delete_ExpectAnyArgs();

    // Verify expected error code is returned
    s2_client_http_response_ctx_t ctx = {0};
    mock_http_response_ctx(&ctx, "{}");
    s2_client_request_pairing_response_t out = {0};
    TEST_ASSERT_EQUAL(ESP_FAIL, s2_client_parse_request_pairing_response(&ctx, &out));
}

TEST_CASE("Fails when S2 server role is unknown", "[S2 client pairing - Parse request pairing response]") {
    // Initialise JSON mocks
    mock_json_init();

    // pairingAttemptId (required top-level field)
    cJSON attempt_id_node;
    mock_cjson_string(&attempt_id_node, "attempt-123");
    cJSON_GetObjectItem_return_for_key("pairingAttemptId", &attempt_id_node);

    // serverNodeDescription: use MOCK_CJSON as a dummy object that passes cJSON_IsObject
    cJSON server_node_obj = {0};
    server_node_obj.type = cJSON_Object;
    cJSON_GetObjectItem_return_for_key("serverNodeDescription", &server_node_obj);

    // server node sub-fields (must all be valid strings to reach role check)
    cJSON id_node;
    mock_cjson_string(&id_node, "server_id");
    cJSON_GetObjectItem_return_for_key("id", &id_node);

    cJSON brand_node;
    mock_cjson_string(&brand_node, "server_brand");
    cJSON_GetObjectItem_return_for_key("brand", &brand_node);

    cJSON type_node;
    mock_cjson_string(&type_node, "server_type");
    cJSON_GetObjectItem_return_for_key("type", &type_node);

    cJSON model_node;
    mock_cjson_string(&model_node, "server_model");
    cJSON_GetObjectItem_return_for_key("modelName", &model_node);

    cJSON name_node;
    mock_cjson_string(&name_node, "server_name");
    cJSON_GetObjectItem_return_for_key("userDefinedName", &name_node);

    // Role that is not defined in roles enum should cause s2_role_to_enum to fail
    cJSON role_node;
    mock_cjson_string(&role_node, "unknown-role");
    cJSON_GetObjectItem_return_for_key("role", &role_node);

    // Mock optional fields as NULL so cJSON_IsString returns false and they are skipped
    cJSON_GetObjectItem_return_for_key("clientHmacChallengeResponse", NULL);
    cJSON_GetObjectItem_return_for_key("serverHmacChallenge", NULL);

    // Parsed JSON object should be freed on failure
    cJSON_Delete_StopIgnore();
    cJSON_Delete_ExpectAnyArgs();

    // Verify expected error code is returned
    s2_client_http_response_ctx_t ctx = {0};
    mock_http_response_ctx(&ctx, "{}");
    s2_client_request_pairing_response_t out = {0};
    TEST_ASSERT_EQUAL(ESP_FAIL, s2_client_parse_request_pairing_response(&ctx, &out));
}

TEST_CASE("Fails when S2 server deployment is unknown", "[S2 client pairing - Parse request pairing response]") {
    // Initialise JSON mocks
    mock_json_init();

    // pairingAttemptId (required top-level field)
    cJSON attempt_id_node;
    mock_cjson_string(&attempt_id_node, "attempt-123");
    cJSON_GetObjectItem_return_for_key("pairingAttemptId", &attempt_id_node);

    // serverNodeDescription: use MOCK_CJSON as a dummy object that passes cJSON_IsObject
    cJSON server_node_obj = {0};
    server_node_obj.type = cJSON_Object;
    cJSON_GetObjectItem_return_for_key("serverNodeDescription", &server_node_obj);

    // server node sub-fields (all valid to pass string checks)
    cJSON id_node;
    mock_cjson_string(&id_node, "server_id");
    cJSON_GetObjectItem_return_for_key("id", &id_node);

    cJSON brand_node;
    mock_cjson_string(&brand_node, "server_brand");
    cJSON_GetObjectItem_return_for_key("brand", &brand_node);

    cJSON type_node;
    mock_cjson_string(&type_node, "server_type");
    cJSON_GetObjectItem_return_for_key("type", &type_node);

    cJSON model_node;
    mock_cjson_string(&model_node, "server_model");
    cJSON_GetObjectItem_return_for_key("modelName", &model_node);

    cJSON name_node;
    mock_cjson_string(&name_node, "server_name");
    cJSON_GetObjectItem_return_for_key("userDefinedName", &name_node);

    // Role that is not defined in roles enum should cause s2_role_to_enum to fail
    cJSON role_node;
    mock_cjson_string(&role_node, "unknown-role");
    cJSON_GetObjectItem_return_for_key("role", &role_node);

    // Mock optional fields as NULL so cJSON_IsString returns false and they are skipped
    cJSON_GetObjectItem_return_for_key("clientHmacChallengeResponse", NULL);
    cJSON_GetObjectItem_return_for_key("serverHmacChallenge", NULL);

    // Parsed JSON object should be freed on failure
    cJSON_Delete_StopIgnore();
    cJSON_Delete_ExpectAnyArgs();

    // Verify expected error code is returned
    s2_client_http_response_ctx_t ctx = {0};
    mock_http_response_ctx(&ctx, "{}");
    s2_client_request_pairing_response_t out = {0};
    TEST_ASSERT_EQUAL(ESP_FAIL, s2_client_parse_request_pairing_response(&ctx, &out));
}

TEST_CASE("Correct output should be returned when parsing was successful", "[S2 client pairing - Parse request pairing response]") {
    mock_json_init();

    // pairingAttemptId
    cJSON attempt_id_node;
    mock_cjson_string(&attempt_id_node, "attempt-123");
    cJSON_GetObjectItem_return_for_key("pairingAttemptId", &attempt_id_node);

    // serverNodeDescription: use MOCK_CJSON as a dummy object that passes cJSON_IsObject
    cJSON server_node_obj = {0};
    server_node_obj.type = cJSON_Object;  // make it pass cJSON_IsObject check
    cJSON_GetObjectItem_return_for_key("serverNodeDescription", &server_node_obj);

    // server node sub-fields (parsed from serverNodeDescription)
    cJSON server_id_node;
    mock_cjson_string(&server_id_node, "server_id");
    cJSON_GetObjectItem_return_for_key("id", &server_id_node);

    cJSON server_brand_node;
    mock_cjson_string(&server_brand_node, "server_brand");
    cJSON_GetObjectItem_return_for_key("brand", &server_brand_node);

    cJSON server_type_node;
    mock_cjson_string(&server_type_node, "server_type");
    cJSON_GetObjectItem_return_for_key("type", &server_type_node);

    cJSON server_model_node;
    mock_cjson_string(&server_model_node, "server_model");
    cJSON_GetObjectItem_return_for_key("modelName", &server_model_node);

    cJSON server_user_defined_name_node;
    mock_cjson_string(&server_user_defined_name_node, "server_name");
    cJSON_GetObjectItem_return_for_key("userDefinedName", &server_user_defined_name_node);

    cJSON server_role_node;
    mock_cjson_string(&server_role_node, "RM");
    cJSON_GetObjectItem_return_for_key("role", &server_role_node);

    // clientHmacChallengeResponse
    cJSON client_resp_node;
    mock_cjson_string(&client_resp_node, "resp");
    cJSON_GetObjectItem_return_for_key("clientHmacChallengeResponse", &client_resp_node);

    // serverHmacChallenge
    cJSON server_chal_node;
    mock_cjson_string(&server_chal_node, "chal");
    cJSON_GetObjectItem_return_for_key("serverHmacChallenge", &server_chal_node);

    cJSON_Delete_StopIgnore();
    cJSON_Delete_ExpectAnyArgs();

    s2_client_http_response_ctx_t ctx = {0};
    mock_http_response_ctx(&ctx, "{}");
    s2_client_request_pairing_response_t out = {0};
    TEST_ASSERT_EQUAL(ESP_OK, s2_client_parse_request_pairing_response(&ctx, &out));

    TEST_ASSERT_EQUAL_STRING(server_id_node.valuestring, out.server_node.id);
    TEST_ASSERT_EQUAL_STRING(server_brand_node.valuestring, out.server_node.brand);
    TEST_ASSERT_EQUAL_STRING(server_type_node.valuestring, out.server_node.type);
    TEST_ASSERT_EQUAL_STRING(server_model_node.valuestring, out.server_node.model);
    TEST_ASSERT_EQUAL_STRING(server_user_defined_name_node.valuestring, out.server_node.user_defined_name);
    TEST_ASSERT_EQUAL_INT(S2_ROLE_RM, out.server_node.role);
    TEST_ASSERT_EQUAL_STRING(server_model_node.valuestring, out.server_node.model);
    TEST_ASSERT_EQUAL_STRING(server_user_defined_name_node.valuestring, out.server_node.user_defined_name);
    TEST_ASSERT_EQUAL_INT(S2_ROLE_RM, out.server_node.role);
}

// ___________________________ s2_client_build_request_pairing_body ___________________________

TEST_CASE("Fails if JSON object could not be created", "[S2 client pairing - Request pairing body]") {
    s2_client_handle_t handle = get_client_handle();
    TEST_JSON_OBJECT_FAILURE(s2_client_build_request_pairing_body(handle, "challenge", true, false), NULL);
}

TEST_CASE("Fails if S2 node description object could not be created", "[S2 client pairing - Request pairing body]") {
    // Initialize JSON mocks
    mock_json_init();

    // Root JSON object creation should succeed, but S2 node object creation should fail
    cJSON_CreateObject_StopIgnore();
    cJSON_CreateObject_IgnoreAndReturn(MOCK_CJSON); // Root object
    cJSON_CreateObject_ExpectAndReturn(NULL); // S2 node object (inside s2_node_to_json)

    // Call the function under test to check if it returns expected error
    s2_client_handle_t handle = get_client_handle();
    TEST_ASSERT_NULL(s2_client_build_request_pairing_body(handle, "challenge", true, false));
}

TEST_CASE("Returns valid JSON if successful", "[S2 client pairing - Request pairing body]") {
    // Initialise JSON mocks
    mock_json_init();

    // Call the function under test
    s2_client_handle_t handle = get_client_handle();
    TEST_ASSERT_NOT_NULL(s2_client_build_request_pairing_body(handle, "challenge", true, false));

    // Verify body contains expected top-level entries
    TEST_ASSERT_TRUE_MESSAGE(
        cJSON_was_string_added("clientHmacChallenge", "challenge"),
        "Expected clientHmacChallenge to be added"
    );
    TEST_ASSERT_TRUE_MESSAGE(
        cJSON_was_item_added("clientNodeDescription"),
        "Expected clientNodeDescription to be added as a child object"
    );
    TEST_ASSERT_TRUE_MESSAGE(
        cJSON_was_item_added("clientEndpointDescription"),
        "Expected clientEndpointDescription to be added as a child object"
    );

    // Verify body contains the expected node description fields (added by
    // s2_node_to_json) using the configuration from get_client_handle()
    TEST_ASSERT_TRUE(cJSON_was_string_added("brand", handle->node.brand));
    TEST_ASSERT_TRUE(cJSON_was_string_added("type", handle->node.type));
    TEST_ASSERT_TRUE(cJSON_was_string_added("modelName", handle->node.model));
    TEST_ASSERT_TRUE(cJSON_was_string_added("role", "RM"));
    TEST_ASSERT_TRUE(cJSON_was_string_added("deployment", "LAN"));
    TEST_ASSERT_TRUE(cJSON_was_item_added("supportedS2MessageVersions"));
}

// ___________________________ s2_client_request_pairing ___________________________

TEST_CASE("Fails if client handle is missing", "[S2 client pairing - Request pairing]") {
    s2_client_request_pairing_response_t res;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,
        s2_client_request_pairing(NULL, "challenge", false, false, &res));
}

TEST_CASE("Fails if challenge is missing", "[S2 client pairing - Request pairing]") {
    s2_client_handle_t handle = get_client_handle();
    s2_client_request_pairing_response_t res;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,
        s2_client_request_pairing(handle, NULL, false, false, &res));
}

TEST_CASE("Fails if response output buffer is missing", "[S2 client pairing - Request pairing]") {
    s2_client_handle_t handle = get_client_handle();
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,
        s2_client_request_pairing(handle, "challenge", false, false, NULL));
}

TEST_CASE("Fails if request body could not be built", "[S2 client pairing - Request pairing]") {
    s2_client_handle_t handle = get_client_handle();
    s2_client_request_pairing_response_t res;
    TEST_JSON_OBJECT_FAILURE(
        s2_client_request_pairing(handle, "challenge", false, false, &res), ESP_FAIL);
}

TEST_CASE("Fails if HTTP request failed", "[S2 client pairing - Request pairing]") {
    s2_client_handle_t handle = get_client_handle();
    s2_client_request_pairing_response_t res;
    TEST_HTTP_FAILURE(
        s2_client_request_pairing(handle, "challenge", false, false, &res));
}

TEST_CASE("Returns OK if request was successful", "[S2 client pairing - Request pairing]") {
    s2_client_handle_t handle = get_client_handle();
    s2_client_request_pairing_response_t res;
    TEST_HTTP_SUCCESS(
        s2_client_request_pairing(handle, "challenge", false, false, &res));
}

// ___________________________ s2_client_request_connection_details ___________________________

TEST_CASE("Fails if client handle is missing", "[S2 client pairing - Connection details]") {
    s2_client_connection_details_response_t res;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,
        s2_client_request_connection_details(NULL, "attempt-id", "response", &res));
}

TEST_CASE("Fails if attempt ID is missing", "[S2 client pairing - Connection details]") {
    s2_client_handle_t handle = get_client_handle();
    s2_client_connection_details_response_t res;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,
        s2_client_request_connection_details(handle, NULL, "response", &res));
}

TEST_CASE("Fails if challenge response is missing", "[S2 client pairing - Connection details]") {
    s2_client_handle_t handle = get_client_handle();
    s2_client_connection_details_response_t res;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,
        s2_client_request_connection_details(handle, "attempt-id", NULL, &res));
}

TEST_CASE("Fails if response output buffer is missing", "[S2 client pairing - Connection details]") {
    s2_client_handle_t handle = get_client_handle();
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,
        s2_client_request_connection_details(handle, "attempt-id", "response", NULL));
}

TEST_CASE("Fails if request body could not be built", "[S2 client pairing - Connection details]") {
    s2_client_handle_t handle = get_client_handle();
    s2_client_connection_details_response_t res;
    TEST_JSON_OBJECT_FAILURE(
        s2_client_request_connection_details(handle, "attempt-id", "response", &res), ESP_FAIL);
}

TEST_CASE("Returns OK if request was successful", "[S2 client pairing - Connection details]") {
    s2_client_handle_t handle = get_client_handle();
    s2_client_connection_details_response_t res;
    TEST_HTTP_SUCCESS(
        s2_client_request_connection_details(handle, "attempt-id", "response", &res));
}

// ___________________________ s2_client_finalize_pairing ___________________________

TEST_CASE("Fails if client handle is missing", "[S2 client pairing - Finalize pairing]") {
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,
        s2_client_finalize_pairing(NULL, "attempt-id", true));
}

TEST_CASE("Fails if pairing attempt id is missing", "[S2 client pairing - Finalize pairing]") {
    s2_client_handle_t handle = get_client_handle();
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,
        s2_client_finalize_pairing(handle, NULL, true));
}

TEST_CASE("Fails if request body could not be built", "[S2 client pairing - Finalize pairing]") {
    s2_client_handle_t handle = get_client_handle();
    TEST_JSON_OBJECT_FAILURE(
        s2_client_finalize_pairing(handle, "attempt-id", true), ESP_FAIL);
}

TEST_CASE("Returns OK if request was successful", "[S2 client pairing - Finalize pairing]") {
    s2_client_handle_t handle = get_client_handle();
    TEST_HTTP_SUCCESS(
        s2_client_finalize_pairing(handle, "attempt-id", true));
}

// ___________________________ s2_client_build_unpair_body ___________________________

TEST_CASE("Fails if JSON object could not be created", "[S2 client pairing - Unpair body]") {
    s2_client_handle_t handle = get_client_handle();
    TEST_JSON_OBJECT_FAILURE(s2_client_build_unpair_body(handle, "server-id"), NULL);
}

TEST_CASE("Fails if client id could not be added to JSON object", "[S2 client pairing - Unpair body]") {
    s2_client_handle_t handle = get_client_handle();
    TEST_JSON_STRING_KEY_FAILURE(s2_client_build_unpair_body(handle, "server-id"), "clientNodeId");
}

TEST_CASE("Fails if server node id is missing", "[S2 client pairing - Unpair body]") {
    s2_client_handle_t handle = get_client_handle();
    TEST_ASSERT_NULL(s2_client_build_unpair_body(handle, NULL));
    TEST_ASSERT_NULL(s2_client_build_unpair_body(handle, ""));
}

TEST_CASE("Returns valid JSON if successful", "[S2 client pairing - Unpair body]") {
    // Initialise JSON mocks
    mock_json_init();

    // Call function
    s2_client_handle_t handle = get_client_handle();
    TEST_ASSERT_NOT_NULL(s2_client_build_unpair_body(handle, "server-id"));

    // Verify expected keys have been added
    TEST_ASSERT_TRUE(cJSON_was_string_added("clientNodeId", handle->node.id));
    TEST_ASSERT_TRUE(cJSON_was_string_added("serverNodeId", "server-id"));
}

// ___________________________ s2_client_unpair ___________________________

TEST_CASE("Fails if client handle is missing", "[S2 client pairing - Unpair]") {
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, s2_client_unpair(NULL, "token", NULL));
}

TEST_CASE("Fails if access token is missing", "[S2 client pairing - Unpair]") {
    s2_client_handle_t handle = get_client_handle();
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, s2_client_unpair(handle, NULL, NULL));
}

TEST_CASE("Fails if request body could not be built", "[S2 client pairing - Unpair]") {
    s2_client_handle_t handle = get_client_handle();
    TEST_JSON_OBJECT_FAILURE(s2_client_unpair(handle, "token", "server-id"), ESP_FAIL);
}

TEST_CASE("Fails if server node id is missing", "[S2 client pairing - Unpair]") {
    s2_client_handle_t handle = get_client_handle();
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, s2_client_unpair(handle, "token", NULL));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, s2_client_unpair(handle, "token", ""));
}

TEST_CASE("Fails if HTTP request failed", "[S2 client pairing - Unpair]") {
    s2_client_handle_t handle = get_client_handle();
    TEST_HTTP_FAILURE(s2_client_unpair(handle, "token", "server-id"));
}

TEST_CASE("Returns OK if request was successful", "[S2 client pairing - Unpair]") {
    s2_client_handle_t handle = get_client_handle();
    TEST_HTTP_SUCCESS(s2_client_unpair(handle, "token", "server-id"));
}

// ---- s2_client_pin_server_certificate ----

TEST_CASE("Accepts valid cert and silently ignores null inputs", "[S2 client pairing - Pin server cert]") {
    s2_client_handle_t handle = get_client_handle();
    uint8_t cert[32] = {0};
    s2_client_pin_server_certificate(NULL, cert, 32);       /* null handle */
    s2_client_pin_server_certificate(handle, NULL, 32);     /* null cert */
    s2_client_pin_server_certificate(handle, cert, 0);      /* zero length */
    s2_client_pin_server_certificate(handle, cert, 32);     /* valid */
}

// ---- s2_client_build_connection_details_body ----

TEST_CASE("Fails if JSON object could not be created", "[S2 client pairing - Connection details body]") {
    s2_client_handle_t handle = get_client_handle();
    TEST_JSON_OBJECT_FAILURE(s2_client_build_connection_details_body(handle, "resp"), NULL);
}

TEST_CASE("Returns valid JSON on success", "[S2 client pairing - Connection details body]") {
    mock_json_init();
    s2_client_handle_t handle = get_client_handle();
    cJSON *body = s2_client_build_connection_details_body(handle, "resp");
    TEST_ASSERT_NOT_NULL(body);
    TEST_ASSERT_TRUE(cJSON_was_string_added("serverHmacChallengeResponse", "resp"));
}

// ---- s2_client_parse_connection_details_response ----

TEST_CASE("Fails on unparseable or missing fields, passes on valid", "[S2 client pairing - Parse connection details]") {
    s2_client_connection_details_response_t out = {0};
    s2_client_http_response_ctx_t ctx;

    /* unparseable JSON */
    mock_http_response_ctx(&ctx, "bad");
    cJSON_ParseWithLength_ExpectAndReturn(ctx.buffer, ctx.len, NULL);
    TEST_ASSERT_EQUAL(ESP_FAIL, s2_client_parse_connection_details_response(&ctx, &out));

    /* missing initiateSessionUrl */
    mock_json_init();
    cJSON_GetObjectItem_return_for_key("initiateSessionUrl", NULL);
    cJSON_GetObjectItem_return_for_key("accessToken", NULL);
    cJSON_Delete_StopIgnore(); cJSON_Delete_ExpectAnyArgs();
    mock_http_response_ctx(&ctx, "{}");
    TEST_ASSERT_EQUAL(ESP_FAIL, s2_client_parse_connection_details_response(&ctx, &out));

    /* valid */
    mock_json_init();
    cJSON u, t;
    mock_cjson_string(&u, "wss://x"); cJSON_GetObjectItem_return_for_key("initiateSessionUrl", &u);
    mock_cjson_string(&t, "tok");     cJSON_GetObjectItem_return_for_key("accessToken", &t);
    cJSON_Delete_StopIgnore(); cJSON_Delete_ExpectAnyArgs();
    mock_http_response_ctx(&ctx, "{}");
    TEST_ASSERT_EQUAL(ESP_OK, s2_client_parse_connection_details_response(&ctx, &out));
}

// ---- s2_client_confirm_access_token ----

TEST_CASE("Invalid args or HTTP failure", "[S2 client pairing - Confirm access token]") {
    s2_client_handle_t handle = get_client_handle();
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, s2_client_confirm_access_token(NULL, "tok"));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, s2_client_confirm_access_token(handle, NULL));
    TEST_JSON_OBJECT_FAILURE(s2_client_confirm_access_token(handle, "tok"), ESP_FAIL);
    TEST_HTTP_FAILURE(s2_client_confirm_access_token(handle, "tok"));
}

TEST_CASE("Returns OK on success", "[S2 client pairing - Confirm access token]") {
    s2_client_handle_t handle = get_client_handle();
    TEST_HTTP_SUCCESS(s2_client_confirm_access_token(handle, "tok"));
}

// ---- s2_client_initiate_session ----

TEST_CASE("Invalid args or HTTP failure", "[S2 client pairing - Initiate session]") {
    s2_client_handle_t handle = get_client_handle();
    s2_client_initiate_session_response_t r = {0};
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, s2_client_initiate_session(NULL, "n", "t", &r));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, s2_client_initiate_session(handle, NULL, "t", &r));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, s2_client_initiate_session(handle, "n", NULL, &r));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, s2_client_initiate_session(handle, "n", "t", NULL));
    TEST_JSON_OBJECT_FAILURE(s2_client_initiate_session(handle, "n", "t", &r), ESP_FAIL);
    TEST_HTTP_FAILURE(s2_client_initiate_session(handle, "n", "t", &r));
}

TEST_CASE("Returns OK on success", "[S2 client pairing - Initiate session]") {
    s2_client_handle_t handle = get_client_handle();
    s2_client_initiate_session_response_t r = {0};
    TEST_HTTP_SUCCESS(s2_client_initiate_session(handle, "n", "t", &r));
}

// ---- s2_client_parse_confirm_access_token_response ----

TEST_CASE("Fails on unparseable or missing fields, passes on valid", "[S2 client pairing - Parse confirm access token]") {
    s2_client_confirm_access_token_response_t out = {0};
    s2_client_http_response_ctx_t ctx;

    /* unparseable */
    mock_http_response_ctx(&ctx, "bad");
    cJSON_ParseWithLength_ExpectAndReturn(ctx.buffer, ctx.len, NULL);
    TEST_ASSERT_EQUAL(ESP_FAIL, s2_client_parse_confirm_access_token_response(&ctx, &out));

    /* unsupported protocol */
    mock_json_init();
    cJSON proto;
    mock_cjson_string(&proto, "MQTT");
    cJSON_GetObjectItem_return_for_key("communicationProtocol", &proto);
    cJSON_Delete_StopIgnore(); cJSON_Delete_ExpectAnyArgs();
    mock_http_response_ctx(&ctx, "{}");
    TEST_ASSERT_EQUAL(ESP_FAIL, s2_client_parse_confirm_access_token_response(&ctx, &out));

    /* missing fields */
    mock_json_init();
    mock_cjson_string(&proto, "WebSocket");
    cJSON_GetObjectItem_return_for_key("communicationProtocol", &proto);
    cJSON_GetObjectItem_return_for_key("websocketToken", NULL);
    cJSON_GetObjectItem_return_for_key("websocketUrl", NULL);
    cJSON_Delete_StopIgnore(); cJSON_Delete_ExpectAnyArgs();
    mock_http_response_ctx(&ctx, "{}");
    TEST_ASSERT_EQUAL(ESP_FAIL, s2_client_parse_confirm_access_token_response(&ctx, &out));

    /* valid */
    mock_json_init();
    cJSON tk, wu;
    mock_cjson_string(&proto, "WebSocket");
    cJSON_GetObjectItem_return_for_key("communicationProtocol", &proto);
    mock_cjson_string(&tk, "t"); cJSON_GetObjectItem_return_for_key("websocketToken", &tk);
    mock_cjson_string(&wu, "w"); cJSON_GetObjectItem_return_for_key("websocketUrl", &wu);
    cJSON_Delete_StopIgnore(); cJSON_Delete_ExpectAnyArgs();
    mock_http_response_ctx(&ctx, "{}");
    TEST_ASSERT_EQUAL(ESP_OK, s2_client_parse_confirm_access_token_response(&ctx, &out));
}

// ---- s2_client_confirm_access_token_extended ----

TEST_CASE("Invalid args or HTTP failure", "[S2 client pairing - Confirm access token extended]") {
    s2_client_handle_t handle = get_client_handle();
    s2_client_confirm_access_token_response_t r = {0};
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, s2_client_confirm_access_token_extended(NULL, "t", &r));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, s2_client_confirm_access_token_extended(handle, NULL, &r));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, s2_client_confirm_access_token_extended(handle, "t", NULL));
    TEST_JSON_OBJECT_FAILURE(s2_client_confirm_access_token_extended(handle, "t", &r), ESP_FAIL);
    TEST_HTTP_FAILURE(s2_client_confirm_access_token_extended(handle, "t", &r));
}

TEST_CASE("Returns OK on success", "[S2 client pairing - Confirm access token extended]") {
    s2_client_handle_t handle = get_client_handle();
    s2_client_confirm_access_token_response_t r = {0};
    TEST_HTTP_SUCCESS(s2_client_confirm_access_token_extended(handle, "t", &r));
}
