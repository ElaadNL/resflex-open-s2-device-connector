// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

#include "unity.h"
#include "s2_long_poll.h"
#include "s2_client.h"
#include "fixtures.h"
#include "json_stubs.h"
#include "freertos_stubs.h"
#include "Mockesp_http_client_mock.h"
#include "MockcJSON_mock.h"

#define SERVER_URL "https://homeassistant.local:8123/api/s2_pebc"

static void dummy_on_session_ready(void) {}
static void dummy_on_request_measurement(s2_commodity_quantity_t q) {}
static void dummy_on_instruction(s2_commodity_quantity_t q, float u, float l) {}
static void dummy_on_request_param(s2_pebc_param_t p, s2_commodity_quantity_t q, bool a) {}

static s2_client_handle_t get_handle(void) {
    mock_queue_init(CREATE_AND_IGNORE);
    mock_task_init(CREATE_AND_IGNORE);
    MockHttpClient hc; mock_http_client_init(&hc, CREATE_AND_IGNORE);
    s2_client_config_t cfg = {
        .node = { .id = "abff635e-cd69-4350-b10f-a990b7920ba0", .brand = "b", .model = "m", .type = "t",
                  .role = S2_ROLE_RM, .deployment = S2_DEPLOYMENT_LAN,
                  .supported_versions = {"0.0.1"} },
        .server_config = { .base_url = SERVER_URL },
        .rm_config = {
            .control_type = S2_CONTROL_TYPE_PEBC,
            .roles = {{.commodity = S2_COMMODITY_ELECTRICITY, .role_type = S2_ROLE_TYPE_ENERGY_CONSUMER}},
            .roles_count = 1, .commodity_quantities = {S2_COMMODITY_QUANTITY_ELECTRIC_POWER_L1},
            .commodity_quantities_count = 1,
            .on_request_measurement = dummy_on_request_measurement,
            .on_session_ready = dummy_on_session_ready,
            .pebc = { .default_limits = {{.commodity_quantity = S2_COMMODITY_QUANTITY_ELECTRIC_POWER_L1}},
                      .default_limits_count = 1,
                      .on_instruction = dummy_on_instruction,
                      .on_request_param = dummy_on_request_param },
        }
    };
    return s2_client_init(&cfg);
}

TEST_CASE("invalid args", "[Long poll]") {
    s2_long_poll_result_t r;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, s2_long_poll_wait(NULL, "n", &r));
    s2_client_handle_t h = get_handle();
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, s2_long_poll_wait(h, NULL, &r));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, s2_long_poll_wait(h, "n", NULL));
}

TEST_CASE("body create fails", "[Long poll]") {
    s2_client_handle_t h = get_handle();
    s2_long_poll_result_t r;
    mock_json_init();
    cJSON_CreateObject_StopIgnore();
    cJSON_CreateObject_ExpectAndReturn(NULL);
    TEST_ASSERT_EQUAL(ESP_FAIL, s2_long_poll_wait(h, "n", &r));
}

TEST_CASE("http failure", "[Long poll]") {
    s2_client_handle_t h = get_handle();
    s2_long_poll_result_t r;
    mock_json_init();
    xQueueGenericSend_ExpectAnyArgsAndReturn(pdFALSE);
    vQueueDelete_ExpectAnyArgs();
    cJSON_Delete_StopIgnore(); cJSON_Delete_ExpectAnyArgs();
    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT, s2_long_poll_wait(h, "n", &r));
}

TEST_CASE("http success", "[Long poll]") {
    s2_client_handle_t h = get_handle();
    s2_long_poll_result_t r;
    mock_json_init();
    cJSON_Delete_StopIgnore(); cJSON_Delete_ExpectAnyArgs();
    freertos_stubs_queue_send_set_request_result(ESP_OK);
    xQueueSemaphoreTake_ExpectAnyArgsAndReturn(pdTRUE);
    TEST_ASSERT_EQUAL(ESP_OK, s2_long_poll_wait(h, "n", &r));
}

TEST_CASE("wait_extended invalid args", "[Long poll]") {
    s2_long_poll_result_t r;
    s2_long_poll_request_options_t opts = {0};
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, s2_long_poll_wait_extended(NULL, "n", &opts, &r));
    s2_client_handle_t h = get_handle();
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, s2_long_poll_wait_extended(h, NULL, &opts, &r));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, s2_long_poll_wait_extended(h, "n", &opts, NULL));
}

TEST_CASE("wait_extended with descriptions and error succeeds", "[Long poll]") {
    s2_client_handle_t h = get_handle();
    s2_long_poll_result_t r;
    s2_long_poll_request_options_t opts = {
        .include_descriptions = true,
        .no_valid_token = true,
    };
    mock_json_init();
    cJSON_Delete_StopIgnore(); cJSON_Delete_ExpectAnyArgs();
    freertos_stubs_queue_send_set_request_result(ESP_OK);
    xQueueSemaphoreTake_ExpectAnyArgsAndReturn(pdTRUE);
    TEST_ASSERT_EQUAL(ESP_OK, s2_long_poll_wait_extended(h, "n", &opts, &r));
}

// ---- parse_action ----

TEST_CASE("parse_action NULL returns ERROR", "[long_poll]") {
    TEST_ASSERT_EQUAL(S2_LP_ERROR, parse_action(NULL));
}

TEST_CASE("parse_action unknown string returns ERROR", "[long_poll]") {
    TEST_ASSERT_EQUAL(S2_LP_ERROR, parse_action("unknown"));
}

TEST_CASE("parse_action sendNodeDescription", "[long_poll]") {
    TEST_ASSERT_EQUAL(S2_LP_SEND_NODE_DESC, parse_action("sendNodeDescription"));
}

TEST_CASE("parse_action preparePairing", "[long_poll]") {
    TEST_ASSERT_EQUAL(S2_LP_PREPARE_PAIRING, parse_action("preparePairing"));
}

TEST_CASE("parse_action cancelPreparePairing", "[long_poll]") {
    TEST_ASSERT_EQUAL(S2_LP_CANCEL_PREPARE, parse_action("cancelPreparePairing"));
}

TEST_CASE("parse_action requestPairing", "[long_poll]") {
    TEST_ASSERT_EQUAL(S2_LP_REQUEST_PAIRING, parse_action("requestPairing"));
}

// ---- parse_long_poll_response ----

TEST_CASE("parse response empty body returns NO_ACTION", "[long_poll]") {
    s2_client_http_response_ctx_t raw = {0};
    s2_long_poll_result_t r;
    TEST_ASSERT_EQUAL(ESP_OK, parse_long_poll_response(&raw, &r));
    TEST_ASSERT_EQUAL(S2_LP_NO_ACTION, r.action);
}

TEST_CASE("parse response invalid JSON returns ERROR", "[long_poll]") {
    s2_client_http_response_ctx_t raw = {.buffer = "not-json", .len = 8};
    s2_long_poll_result_t r;
    cJSON_ParseWithLength_IgnoreAndReturn(NULL);
    cJSON_Delete_Ignore();
    TEST_ASSERT_EQUAL(ESP_FAIL, parse_long_poll_response(&raw, &r));
    TEST_ASSERT_EQUAL(S2_LP_ERROR, r.action);
}

TEST_CASE("parse response non-array returns ERROR", "[long_poll]") {
    s2_client_http_response_ctx_t raw = {.buffer = "{}", .len = 2};
    s2_long_poll_result_t r;
    cJSON_ParseWithLength_IgnoreAndReturn((cJSON *)1);
    cJSON_IsArray_IgnoreAndReturn(0);
    cJSON_Delete_Ignore();
    TEST_ASSERT_EQUAL(ESP_FAIL, parse_long_poll_response(&raw, &r));
    TEST_ASSERT_EQUAL(S2_LP_ERROR, r.action);
}

TEST_CASE("parse response extracts action and node id", "[long_poll]") {
    s2_client_http_response_ctx_t raw = {.buffer = "[]", .len = 2};
    s2_long_poll_result_t r;
    cJSON *arr = (cJSON *)1;
    cJSON_ParseWithLength_IgnoreAndReturn(arr);
    cJSON_IsArray_IgnoreAndReturn(1);
    cJSON *item = (cJSON *)2;
    cJSON_GetArrayItem_ExpectAndReturn(arr, 0, item);
    cJSON_IsObject_ExpectAndReturn(item, 1);
    cJSON id_node; mock_cjson_string(&id_node, "node-123");
    cJSON act_node; mock_cjson_string(&act_node, "preparePairing");
    cJSON_GetObjectItem_ExpectAndReturn(item, "clientNodeId", &id_node);
    cJSON_GetObjectItem_ExpectAndReturn(item, "action", &act_node);
    cJSON_IsString_IgnoreAndReturn(1);
    cJSON_Delete_Ignore();
    TEST_ASSERT_EQUAL(ESP_OK, parse_long_poll_response(&raw, &r));
    TEST_ASSERT_EQUAL(S2_LP_PREPARE_PAIRING, r.action);
    TEST_ASSERT_EQUAL_STRING("node-123", r.client_node_id);
}
