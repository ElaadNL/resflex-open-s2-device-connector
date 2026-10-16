// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

#include "unity.h"
#include <string.h>
#include "Mockesp_http_client_mock.h"
#include "Mockidf_additions.h"
#include "Mocktask.h"
#include "Mockqueue.h"
#include "MockcJSON_mock.h"
#include "Mockesp_timer.h"
#include "Mockevent_groups.h"
#include "esp_websocket_client.h"
#include "esp_websocket_client_stubs.h"

#include "s2_client.h"
#include "s2_client_internal.h"
#include "s2_client_ws.h"
#include "s2_client_initiate_stubs.h"

/* setUp and tearDown are defined in test_main.c — do NOT redefine them here. */

/* ── s2_client_websocket_connect stub (replaces old initiate_connection stub) ── */

// Test that WEBSOCKET_EVENT_DISCONNECTED handles xTaskCreate failure gracefully
TEST_CASE("disconnected xTaskCreate fails", "[s2_client_websocket]")
{
    xEventGroupClearBits_IgnoreAndReturn(0);
    vTaskDelete_Ignore();
    s2_client_t h = {0};
    strcpy(h.last_access_token, "access-token");
    strcpy(h.session_url, "wss://cem/ws");
    h.websocket_client = (esp_websocket_client_handle_t)1;
    h.reconnecting = false;

    // Simulate xTaskCreate failure
    xTaskCreatePinnedToCore_ExpectAnyArgsAndReturn(pdFAIL);

    s2_client_websocket_invoke_event_handler_for_test(&h, WEBSOCKET_EVENT_DISCONNECTED, NULL);

    // reconnecting should be cleared after failed task creation
    TEST_ASSERT_FALSE(h.reconnecting);
}

// Test that WEBSOCKET_EVENT_DISCONNECTED schedules reconnect when access token present
TEST_CASE("disconnected schedules reconnect", "[s2_client_websocket]")
{
    xEventGroupClearBits_IgnoreAndReturn(0);
    vTaskDelete_Ignore();
    s2_client_t h = {0};
    strcpy(h.last_access_token, "access-token");
    strcpy(h.session_url, "wss://cem/ws");
    h.websocket_client = (esp_websocket_client_handle_t)1;
    h.reconnecting = false;

    // Allow xTaskCreate to succeed
    xTaskCreatePinnedToCore_IgnoreAndReturn(pdPASS);

    s2_client_websocket_invoke_event_handler_for_test(&h, WEBSOCKET_EVENT_DISCONNECTED, NULL);

    TEST_ASSERT_TRUE(h.reconnecting);
}

// Test that WEBSOCKET_EVENT_ERROR with handshake 401 schedules reconnect
TEST_CASE("error handshake 401 schedules reconnect", "[s2_client_websocket]")
{
    vTaskDelete_Ignore();
    s2_client_t h = {0};
    strcpy(h.last_access_token, "access-token");
    h.websocket_client = (esp_websocket_client_handle_t)2;
    h.reconnecting = false;

    esp_websocket_event_data_t data = {0};
    data.error_handle.esp_ws_handshake_status_code = 401;

    xTaskCreatePinnedToCore_IgnoreAndReturn(pdPASS);

    s2_client_websocket_invoke_event_handler_for_test(&h, WEBSOCKET_EVENT_ERROR, &data);

    TEST_ASSERT_TRUE(h.reconnecting);
}

// Test that WEBSOCKET_EVENT_ERROR 401 handles xTaskCreate failure gracefully
TEST_CASE("error handshake 401 xTaskCreate fails", "[s2_client_websocket]")
{
    vTaskDelete_Ignore();
    s2_client_t h = {0};
    strcpy(h.last_access_token, "access-token");
    h.websocket_client = (esp_websocket_client_handle_t)2;
    h.reconnecting = false;

    esp_websocket_event_data_t data = {0};
    data.error_handle.esp_ws_handshake_status_code = 401;

    // Simulate xTaskCreate failure
    xTaskCreatePinnedToCore_ExpectAnyArgsAndReturn(pdFAIL);

    s2_client_websocket_invoke_event_handler_for_test(&h, WEBSOCKET_EVENT_ERROR, &data);

    // reconnecting should be cleared after failed task creation
    TEST_ASSERT_FALSE(h.reconnecting);
}

// Test reconnect task: no access token -> should clear reconnecting and exit
TEST_CASE("reconnect task no token", "[s2_client_websocket]")
{
    vTaskDelete_Ignore();
    s2_client_t h = {0};
    h.reconnecting = false;
    h.last_access_token[0] = '\0';
    h.session_url[0] = '\0';

    s2_client_websocket_reconnect_task_for_test(&h);

    TEST_ASSERT_FALSE(h.reconnecting);
}

// Test reconnect task: no session URL -> should clear reconnecting and exit
TEST_CASE("reconnect task no url", "[s2_client_websocket]")
{
    vTaskDelete_Ignore();
    s2_client_t h = {0};
    strcpy(h.last_access_token, "token");
    h.reconnecting = false;
    h.session_url[0] = '\0';

    s2_client_websocket_reconnect_task_for_test(&h);

    TEST_ASSERT_FALSE(h.reconnecting);
}

// Test reconnect task: ws_connect fails -> reconnecting cleared
TEST_CASE("reconnect task connect fail", "[s2_client_websocket]")
{
    vTaskDelete_Ignore();
    vTaskDelay_Ignore();
    s2_client_t h = {0};
    strcpy(h.last_access_token, "token");
    strcpy(h.session_url, "wss://cem/ws");
    h.reconnecting = false;

    s2_websocket_connect_stub_set_return(ESP_FAIL);

    s2_client_websocket_reconnect_task_for_test(&h);

    TEST_ASSERT_FALSE(h.reconnecting);
}

// Test reconnect task: ws_connect succeeds -> reconnecting cleared
TEST_CASE("reconnect task success", "[s2_client_websocket]")
{
    vTaskDelete_Ignore();
    xQueueSemaphoreTake_IgnoreAndReturn(pdTRUE);
    xQueueCreateMutex_IgnoreAndReturn((QueueHandle_t)0xdeadbeef);
    xEventGroupCreate_IgnoreAndReturn((EventGroupHandle_t)0xdeadbeef);
    xQueueGenericSend_IgnoreAndReturn(pdTRUE);
    vTaskDelay_Ignore();
    s2_client_t h = {0};
    strcpy(h.last_access_token, "token");
    strcpy(h.session_url, "wss://cem/ws");
    h.reconnecting = false;

    s2_websocket_connect_stub_set_return(ESP_OK);

    s2_client_websocket_reconnect_task_for_test(&h);

    TEST_ASSERT_FALSE(h.reconnecting);
}

// 401 handshake error when reconnect already in progress -> skips duplicate spawn
TEST_CASE("error handshake 401 already reconnecting", "[s2_client_websocket]")
{
    vTaskDelete_Ignore();
    s2_client_t h = {0};
    strcpy(h.last_access_token, "access-token");
    h.websocket_client = (esp_websocket_client_handle_t)1;
    h.reconnecting = true;  // already reconnecting

    esp_websocket_event_data_t data = {0};
    data.error_handle.esp_ws_handshake_status_code = 401;

    // xTaskCreate must NOT be called (no expectation set -> CMock fails if called)

    s2_client_websocket_invoke_event_handler_for_test(&h, WEBSOCKET_EVENT_ERROR, &data);

    TEST_ASSERT_TRUE(h.reconnecting);
}

// s2_client_websocket_connect destroys an existing websocket_client before creating a new one
TEST_CASE("ws connect destroys existing client", "[s2_client_websocket]")
{
    xQueueSemaphoreTake_IgnoreAndReturn(pdTRUE);
    xQueueCreateMutex_IgnoreAndReturn((QueueHandle_t)0xdeadbeef);
    xEventGroupCreate_IgnoreAndReturn((EventGroupHandle_t)0xdeadbeef);
    xQueueGenericSend_IgnoreAndReturn(pdTRUE);
    s2_client_t h = {0};
    h.websocket_client = (esp_websocket_client_handle_t)1;

    // --wrap stub for esp_websocket_client_destroy records the call;
    // init/start/register stubs return success automatically

    esp_err_t result = s2_client_websocket_connect(&h, "ws://test.local", "comm-token", NULL);
    TEST_ASSERT_EQUAL(ESP_OK, result);
}

// Test reconnect task: transient failures back off, then the same token connects
TEST_CASE("reconnect retries then connects", "[s2_client_websocket]")
{
    vTaskDelete_Ignore();
    vTaskDelay_Ignore();
    // the real s2_client_websocket_connect runs here (it is called from its own file,
    // so --wrap does not intercept it) and needs the FreeRTOS primitives mocked
    xQueueSemaphoreTake_IgnoreAndReturn(pdTRUE);
    xQueueCreateMutex_IgnoreAndReturn((QueueHandle_t)0xdeadbeef);
    xEventGroupCreate_IgnoreAndReturn((EventGroupHandle_t)0xdeadbeef);
    xQueueGenericSend_IgnoreAndReturn(pdTRUE);
    xEventGroupClearBits_IgnoreAndReturn(0);
    // handshake reported done, so the re-initiating attempt counts as connected
    xEventGroupWaitBits_IgnoreAndReturn((EventBits_t)1);
    s2_client_t h = {0};
    h.access_token_count = 1;
    strcpy(h.access_tokens[0], "token");
    strcpy(h.session_url, "wss://cem/ws");
    strcpy(h.server_node_id, "server-node");
    h.reconnecting = true;

    // nine failed attempts is enough to reach the delay cap, then the session comes back
    s2_initiate_session_stub_fail_times(9, ESP_OK);
    s2_confirm_access_token_stub_set_return(ESP_OK);

    s2_client_websocket_reconnect_task_for_test(&h);

    TEST_ASSERT_FALSE(h.reconnecting);
}

// Test reconnect task: the CEM refuses the token, so the node has to pair again
TEST_CASE("reconnect token refused", "[s2_client_websocket]")
{
    vTaskDelete_Ignore();
    s2_client_t h = {0};
    h.access_token_count = 1;
    strcpy(h.access_tokens[0], "token");
    strcpy(h.session_url, "wss://cem/ws");
    strcpy(h.server_node_id, "server-node");
    h.reconnecting = true;

    s2_initiate_session_stub_fail_times(0, ESP_ERR_NOT_SUPPORTED);

    s2_client_websocket_reconnect_task_for_test(&h);

    TEST_ASSERT_FALSE(h.reconnecting);
}

// Test reconnect task: an empty token slot is skipped, so the node has to pair again
TEST_CASE("reconnect skips empty token slot", "[s2_client_websocket]")
{
    vTaskDelete_Ignore();
    s2_client_t h = {0};
    h.access_token_count = 1;
    strcpy(h.session_url, "wss://cem/ws");
    strcpy(h.server_node_id, "server-node");
    h.reconnecting = true;

    s2_client_websocket_reconnect_task_for_test(&h);

    TEST_ASSERT_FALSE(h.reconnecting);
}
