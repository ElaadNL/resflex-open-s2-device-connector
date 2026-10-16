// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

#include "unity.h"
#include "s2_client_http.h"
#include "s2_client.h"
#include "Mockesp_http_client_mock.h"
#include "MockcJSON_mock.h"
#include "json_stubs.h"

static s2_client_http_response_ctx_t *g_test_http_ctx = NULL;

static esp_err_t get_user_data_stub(esp_http_client_handle_t client, void **data, int num_calls) {
    (void)client; (void)num_calls;
    if (g_test_http_ctx) *data = g_test_http_ctx;
    return ESP_OK;
}

TEST_CASE("s2_client_http_perform_request returns invalid arg on bad inputs", "[s2_client_http]") {
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, s2_client_http_perform_request(NULL, NULL));

    s2_client_t h = {0};
    s2_request_t req = {0};
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, s2_client_http_perform_request(&h, &req));
}

TEST_CASE("s2_client_http_perform_request success path", "[s2_client_http]") {
    s2_client_t h = {0};
    h.server.base_url[0] = '\0';
    strncpy(h.server.base_url, "https://example.com/api", sizeof(h.server.base_url)-1);
    strncpy(h.server.host, "example.com", sizeof(h.server.host)-1);
    h.http_client = (esp_http_client_handle_t)1;

    cJSON root = {0};
    s2_request_t req = { .endpoint = "test/path", .body = &root, .access_token = NULL };

    cJSON_PrintUnformatted_IgnoreAndReturn(strdup("{}"));

    esp_http_client_delete_all_headers_IgnoreAndReturn(ESP_OK);
    esp_http_client_set_user_data_IgnoreAndReturn(ESP_OK);
    esp_http_client_set_url_IgnoreAndReturn(ESP_OK);
    esp_http_client_set_method_IgnoreAndReturn(ESP_OK);
    esp_http_client_set_header_IgnoreAndReturn(ESP_OK);
    esp_http_client_set_post_field_IgnoreAndReturn(ESP_OK);
    esp_http_client_perform_IgnoreAndReturn(ESP_OK);
    esp_http_client_get_status_code_IgnoreAndReturn(200);

    TEST_ASSERT_EQUAL(ESP_OK, s2_client_http_perform_request(&h, &req));
}

TEST_CASE("s2_client_http_perform_request returns fail on HTTP error status", "[s2_client_http]") {
    s2_client_t h = {0};
    strncpy(h.server.base_url, "https://example.com/api", sizeof(h.server.base_url)-1);
    strncpy(h.server.host, "example.com", sizeof(h.server.host)-1);
    h.http_client = (esp_http_client_handle_t)1;

    cJSON root = {0};
    s2_request_t req = { .endpoint = "bad/path", .body = &root };

    cJSON_PrintUnformatted_IgnoreAndReturn(strdup("{}"));
    esp_http_client_delete_all_headers_IgnoreAndReturn(ESP_OK);
    esp_http_client_set_user_data_IgnoreAndReturn(ESP_OK);
    esp_http_client_set_url_IgnoreAndReturn(ESP_OK);
    esp_http_client_set_method_IgnoreAndReturn(ESP_OK);
    esp_http_client_set_header_IgnoreAndReturn(ESP_OK);
    esp_http_client_set_post_field_IgnoreAndReturn(ESP_OK);
    esp_http_client_perform_IgnoreAndReturn(ESP_OK);
    esp_http_client_get_status_code_IgnoreAndReturn(400);

    TEST_ASSERT_EQUAL(ESP_FAIL, s2_client_http_perform_request(&h, &req));
}

TEST_CASE("s2_client_http_event_handler copies data into context on HTTP_EVENT_ON_DATA", "[s2_client_http]") {
    s2_client_http_response_ctx_t ctx = {0};
    g_test_http_ctx = &ctx;

    esp_http_client_event_t ev = {0};
    ev.event_id = HTTP_EVENT_ON_DATA;
    const char *payload = "hello";
    ev.data = (char *)payload;
    ev.data_len = strlen(payload);
    ev.client = (esp_http_client_handle_t)1;

    esp_http_client_get_user_data_Stub(get_user_data_stub);

    TEST_ASSERT_EQUAL(ESP_OK, s2_client_http_event_handler(&ev));
    TEST_ASSERT_EQUAL(0, strcmp(ctx.buffer, "hello"));
    TEST_ASSERT_EQUAL((int)strlen(payload), ctx.len);

    g_test_http_ctx = NULL;
}
