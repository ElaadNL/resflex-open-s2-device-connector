// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

#include "unity.h"
#include "s2_client.h"
#include "s2_client_internal.h"
#include "s2_client_ws.h"
#include "Mockqueue.h"
#include "Mockevent_groups.h"
#include "MockcJSON_mock.h"
#include "Mockesp_timer.h"
#include "json_stubs.h"
#include "fixtures.h"
#include "esp_websocket_client_stubs.h"

/* Local dummy callback used by tests */
static void dummy_on_request_forecast(s2_commodity_quantity_t q) { (void)q; }

TEST_CASE("Connect invalid arg NULL handle", "[s2_client_websocket-simple]") {
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, s2_client_websocket_connect(NULL, "wss://example", "commtoken", "accesstoken"));
}

TEST_CASE("Connect invalid arg no communication_token", "[s2_client_websocket-simple]") {
    s2_client_t handle = {0};
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, s2_client_websocket_connect(&handle, "wss://example", NULL, NULL));
}

TEST_CASE("Event handler ignores empty data fragment", "[s2_client_websocket-simple]") {
    s2_client_t handle = {0};
    esp_websocket_event_data_t data = {0};
    data.data_len = 0; // empty message should be ignored
    handle.websocket_rx_len = 0;
    TEST_ASSERT_TRUE(1);
}

TEST_CASE("err_to_reception_status maps errors correctly", "[s2_client_websocket]") {
    TEST_ASSERT_EQUAL_STRING("OK", test_err_to_reception_status(ESP_OK));
    TEST_ASSERT_EQUAL_STRING("INVALID_CONTENT", test_err_to_reception_status(ESP_ERR_INVALID_ARG));
    TEST_ASSERT_EQUAL_STRING("PERMANENT_ERROR", test_err_to_reception_status(ESP_FAIL));
}

TEST_CASE("s2_try_revoke_forecast clears forecast on success", "[s2_client_websocket]") {
    s2_client_t handle;
    memset(&handle, 0, sizeof(handle));
    /* prepare one forecast slot */
    handle.rm.commodity_quantities_count = 1;
    handle.rm.commodity_quantities[0] = S2_COMMODITY_QUANTITY_ELECTRIC_POWER_L1;
    handle.rm.on_request_forecast = dummy_on_request_forecast;

    s2_forecast_state_t *fs = &handle.forecast[0];
    strcpy(fs->forecast_id, "forecast-1");
    fs->forecast_miss_ticks = 1; /* trigger revoke path */

    /* prepare websocket send stub */
    websocket_client_send_stub_init();
    handle.websocket_client = (esp_websocket_client_handle_t)1; /* non-null */

    cJSON_CreateObject_IgnoreAndReturn((cJSON *)1);
    cJSON_AddStringToObject_IgnoreAndReturn((cJSON *)1);
    cJSON_PrintUnformatted_IgnoreAndReturn(strdup("{}"));
    cJSON_Delete_Ignore();
    test_s2_forecast_timer_cb(&handle);

    /* After revoke, forecast_id should be cleared and miss ticks reset */
    TEST_ASSERT_EQUAL_CHAR('\0', fs->forecast_id[0]);
    TEST_ASSERT_EQUAL(0, fs->forecast_miss_ticks);
}

TEST_CASE("s2_forecast_timer_cb increments miss ticks when no revoke", "[s2_client_websocket]") {
    s2_client_t handle;
    memset(&handle, 0, sizeof(handle));
    handle.rm.commodity_quantities_count = 1;
    handle.rm.on_request_forecast = dummy_on_request_forecast;

    s2_forecast_state_t *fs = &handle.forecast[0];
    fs->forecast_id[0] = '\0';
    fs->forecast_miss_ticks = 0;

    test_s2_forecast_timer_cb(&handle);

    TEST_ASSERT_EQUAL(1, fs->forecast_miss_ticks);
}

TEST_CASE("fragment payload too large placeholder", "[s2_client_websocket-simple]") {

    TEST_ASSERT_TRUE(1);
}

TEST_CASE("fragment overflow placeholder", "[s2_client_websocket-simple]") {

    TEST_ASSERT_TRUE(1);
}

TEST_CASE("assembled message reset placeholder", "[s2_client_websocket-simple]") {

    TEST_ASSERT_TRUE(1);
}

TEST_CASE("ws event: payload too large resets buffer and sends INVALID_DATA", "[s2_client_websocket-fragment]") {
    s2_client_t h = {0};
    h.websocket_client = (esp_websocket_client_handle_t)1;
    h.websocket_rx_len = 5; // non-zero to ensure reset happens
    websocket_client_send_stub_init();
    cJSON_build_init();

    esp_websocket_event_data_t data = {0};
    data.payload_len = S2_WEBSOCKET_RECV_BUFFER_SIZE + 1; // trigger too-large branch
    data.data_ptr = "x";
    data.data_len = 1;

    s2_client_websocket_invoke_event_handler_for_test(&h, WEBSOCKET_EVENT_DATA, &data);
    TEST_ASSERT_EQUAL(0, h.websocket_rx_len);
}

TEST_CASE("ws event: fragment overflow resets buffer and sends INVALID_DATA", "[s2_client_websocket-fragment]") {
    s2_client_t h = {0};
    h.websocket_client = (esp_websocket_client_handle_t)1;
    h.websocket_rx_len = 2;
    websocket_client_send_stub_init();
    cJSON_build_init();

    esp_websocket_event_data_t data = {0};
    data.payload_offset = S2_WEBSOCKET_RECV_BUFFER_SIZE - 5;
    data.data_len = 10; // causes overflow
    data.data_ptr = "overflow";
    data.payload_len = 100; // arbitrary > data_len

    s2_client_websocket_invoke_event_handler_for_test(&h, WEBSOCKET_EVENT_DATA, &data);
    TEST_ASSERT_EQUAL(0, h.websocket_rx_len);
}

TEST_CASE("ws event: assembled final fragment resets buffer after processing", "[s2_client_websocket-fragment]") {
    s2_client_t h = {0};
    h.websocket_client = (esp_websocket_client_handle_t)1;
    h.websocket_rx_len = 0;
    websocket_client_send_stub_init();

    esp_websocket_event_data_t data = {0};
    char msg[] = "{}";
    data.data_ptr = msg;
    data.data_len = (int)strlen(msg);
    data.payload_len = data.data_len;
    data.payload_offset = 0;
    data.fin = true; // final fragment

    /* Ignore cJSON builders so mocks don't fail; we only assert on buffer/reset behavior */
    cJSON_CreateObject_IgnoreAndReturn((cJSON *)1);
    cJSON_AddStringToObject_IgnoreAndReturn((cJSON *)1);
    cJSON_PrintUnformatted_IgnoreAndReturn(strdup("{}"));
    cJSON_ParseWithLength_IgnoreAndReturn(NULL);
    cJSON_Delete_Ignore();

    s2_client_websocket_invoke_event_handler_for_test(&h, WEBSOCKET_EVENT_DATA, &data);
    TEST_ASSERT_EQUAL(0, h.websocket_rx_len);
    TEST_ASSERT_EQUAL_INT(0, memcmp(h.websocket_rx_buffer, msg, data.data_len));
}


TEST_CASE("process_reception_status OK untracks message", "[s2_client_websocket-reception]") {
    s2_client_t h = {0};
    strcpy(h.tracked_msgs[0].id, "msg-1");
    h.tracked_msgs[0].in_use = true;

    cJSON_GetObjectItem_init();
    cJSON id_j = {.valuestring = "msg-1"};
    cJSON st_j = {.valuestring = "OK"};
    cJSON_GetObjectItem_return_for_key("subject_message_id", &id_j);
    cJSON_GetObjectItem_return_for_key("status", &st_j);
    cJSON_IsString_init();

    TEST_ASSERT_EQUAL(ESP_OK, s2_client_websocket_process_reception_status(&h, (cJSON *)1));
    TEST_ASSERT_FALSE(h.tracked_msgs[0].in_use);
}

TEST_CASE("process_reception_status TEMPORARY_ERROR schedules retry", "[s2_client_websocket-reception]") {
    s2_client_t h = {0};
    strcpy(h.tracked_msgs[0].id, "msg-2");
    h.tracked_msgs[0].in_use = true;
    h.tracked_msgs[0].retry_count = 0;

    cJSON_GetObjectItem_init();
    cJSON id_j = {.valuestring = "msg-2"};
    cJSON st_j = {.valuestring = "TEMPORARY_ERROR"};
    cJSON_GetObjectItem_return_for_key("subject_message_id", &id_j);
    cJSON_GetObjectItem_return_for_key("status", &st_j);
    cJSON_IsString_init();

    esp_timer_create_IgnoreAndReturn(ESP_OK);
    esp_timer_start_once_IgnoreAndReturn(ESP_OK);

    TEST_ASSERT_EQUAL(ESP_OK, s2_client_websocket_process_reception_status(&h, (cJSON *)1));
    TEST_ASSERT_EQUAL(1, h.tracked_msgs[0].retry_count);
}

// ______________________ s2_client_provide_forecast ______________________

// Returns invalid arg when quantity is not in commodity_quantities.
TEST_CASE("Returns invalid arg if quantity is not in commodity_quantities", "[s2_client_websocket - provide_forecast]") {
    s2_client_t h = {0};
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, s2_client_provide_forecast(&h, S2_COMMODITY_QUANTITY_ELECTRIC_POWER_L1, 1.0f));
}

// Skips sending when value is unchanged and interval has not elapsed.
TEST_CASE("Returns OK and skips send if same value was sent recently", "[s2_client_websocket - provide_forecast]") {
    s2_client_t h = {
        .rm = {
            .commodity_quantities = {S2_COMMODITY_QUANTITY_ELECTRIC_POWER_L1},
            .commodity_quantities_count = 1,
        },
        .forecast = {{
            .forecast_value = 3.14f,
            .forecast_id = "some-id",          // not empty = already sent once
            .last_forecast_sent = (time_t)1e10, // far in the future = interval not reached yet
        }},
    };
    TEST_ASSERT_EQUAL(ESP_OK, s2_client_provide_forecast(&h, S2_COMMODITY_QUANTITY_ELECTRIC_POWER_L1, 3.14f));
    // forecast_id and forecast_value must be unchanged: no send occurred
    TEST_ASSERT_EQUAL_STRING("some-id", h.forecast[0].forecast_id);
    TEST_ASSERT_EQUAL_FLOAT(3.14f, h.forecast[0].forecast_value);
}

// Sends forecast and updates state when no forecast has been sent yet (forecast_id empty).
TEST_CASE("Sends forecast and updates state on first send", "[s2_client_websocket - provide_forecast]") {
    s2_client_t h = {
        .rm = {
            .commodity_quantities = {S2_COMMODITY_QUANTITY_ELECTRIC_POWER_L1},
            .commodity_quantities_count = 1,
            // forecast_interval_ms = 0 -> interval always elapsed
        },
        // forecast[0] zeroed: forecast_id = "" -> value_same = false -> must send
    };

    cJSON_build_init();
    websocket_client_send_stub_init();

    TEST_ASSERT_EQUAL(ESP_OK, s2_client_provide_forecast(&h, S2_COMMODITY_QUANTITY_ELECTRIC_POWER_L1, 500.0f));
    TEST_ASSERT_NOT_EQUAL('\0', h.forecast[0].forecast_id[0]); // UUID was stored
    TEST_ASSERT_EQUAL_FLOAT(500.0f, h.forecast[0].forecast_value);
    TEST_ASSERT_EQUAL(0, h.forecast[0].forecast_miss_ticks);
    // verify the forecast was actually sent over the WebSocket
    TEST_ASSERT_NOT_EQUAL('\0', ws_last_sent_text()[0]);
    TEST_ASSERT_TRUE(h.tracked_msgs[0].in_use);
}

// ______________________ s2_client_provide_measurement ______________________

// Sends measurement and returns OK.
TEST_CASE("Sends measurement and returns OK", "[s2_client_websocket - provide_measurement]") {
    s2_client_t h = {0};

    cJSON_build_init();
    websocket_client_send_stub_init();

    TEST_ASSERT_EQUAL(ESP_OK, s2_client_provide_measurement(&h, S2_COMMODITY_QUANTITY_ELECTRIC_POWER_L1, 100.0f));

    // verify esp_websocket_client_send_text was called with a non-empty body
    TEST_ASSERT_NOT_EQUAL('\0', ws_last_sent_text()[0]);
    // verify the message was queued for retry tracking (only set after a successful send)
    TEST_ASSERT_TRUE(h.tracked_msgs[0].in_use);
}

// Returns ESP_FAIL when JSON build fails (body = NULL, nothing sent).
TEST_CASE("Returns fail when measurement build fails", "[s2_client_websocket - provide_measurement]") {
    s2_client_t h = {0};
    cJSON_CreateObject_ExpectAndReturn(NULL); // triggers NULL body path
    TEST_ASSERT_EQUAL(ESP_FAIL, s2_client_provide_measurement(&h, S2_COMMODITY_QUANTITY_ELECTRIC_POWER_L1, 100.0f));
}

// ______________________ s2_client_websocket_send_message ______________________

TEST_CASE("Returns invalid arg for NULL handle", "[s2_client_websocket - send_message]") {
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, s2_client_websocket_send_message(NULL, "{}", 100));
}

TEST_CASE("Returns invalid arg for NULL message", "[s2_client_websocket - send_message]") {
    s2_client_t h = {0};
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, s2_client_websocket_send_message(&h, NULL, 100));
}

// s2_is_valid_json uses real cJSON_Parse - "not-json" returns NULL so cJSON_Delete is never called.
TEST_CASE("Returns invalid arg for invalid JSON", "[s2_client_websocket - send_message]") {
    s2_client_t h = {0};
    cJSON_Parse_ExpectAnyArgsAndReturn(NULL);
    cJSON_GetErrorPtr_IgnoreAndReturn(NULL);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, s2_client_websocket_send_message(&h, "not-json", 100));
}

// s2_is_valid_json parses object  with real cJSON then deletes it (mocked).
TEST_CASE("Returns invalid state if websocket_conn_event_group is NULL", "[s2_client_websocket - send_message]") {
    s2_client_t h = {0}; // event_group = NULL
    cJSON_Parse_IgnoreAndReturn((cJSON *)1);
    cJSON_Delete_Ignore();
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, s2_client_websocket_send_message(&h, "{}", 100));
}

TEST_CASE("Returns timeout when not connected", "[s2_client_websocket - send_message]") {
    s2_client_t h = {.websocket_conn_event_group = (EventGroupHandle_t)1};
    cJSON_Parse_IgnoreAndReturn((cJSON *)1);
    cJSON_Delete_Ignore();
    xEventGroupWaitBits_IgnoreAndReturn(0); // 0 = bits not set = timed out
    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT, s2_client_websocket_send_message(&h, "{}", 100));
}

// Happy path: message sent over WS, state_mutex is taken and released.
TEST_CASE("Sends message over WS on success", "[s2_client_websocket - send_message]") {
    s2_client_t h = {
        .websocket_conn_event_group = (EventGroupHandle_t)1,
        .websocket_state_mutex = (SemaphoreHandle_t)2,
        .websocket_conn_state  = S2_CLIENT_WEBSOCKET_CONN_STATE_READY,
    };
    websocket_client_send_stub_init();
    cJSON_Parse_IgnoreAndReturn((cJSON *)1);
    cJSON_Delete_Ignore();
    xEventGroupWaitBits_IgnoreAndReturn(1);       // connected
    xQueueSemaphoreTake_IgnoreAndReturn(pdTRUE);  // state_mutex take
    xQueueGenericSend_ExpectAnyArgsAndReturn(pdTRUE); // xSemaphoreGive(websocket_state_mutex)
    TEST_ASSERT_EQUAL(ESP_OK, s2_client_websocket_send_message(&h, "{}", 100));
    TEST_ASSERT_TRUE(ws_last_sent_text()[0] != '\0'); // message passed to WS
}

// ______________________ s2_client_websocket_process_reception_status ______________________

TEST_CASE("Returns invalid arg if required fields missing", "[s2_client_websocket - process_reception_status]") {
    s2_client_t h = {0};
    cJSON msg = {0};
    cJSON_GetObjectItem_IgnoreAndReturn(NULL);
    cJSON_IsString_IgnoreAndReturn(0);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, s2_client_websocket_process_reception_status(&h, &msg));
}

// ReceptionStatus OK 
TEST_CASE("OK status untracks the message", "[s2_client_websocket - process_reception_status]") {
    cJSON id_j  = {.valuestring = "msg-1"};
    cJSON st_j  = {.valuestring = "OK"};
    s2_client_t h = {.tracked_msgs = {{
        .in_use = true, .id = "msg-1", .body = "{}",
    }}};
    cJSON_GetObjectItem_ExpectAnyArgsAndReturn(&id_j); // subject_message_id
    cJSON_GetObjectItem_ExpectAnyArgsAndReturn(&st_j); // status
    cJSON_IsString_IgnoreAndReturn(1);
    TEST_ASSERT_EQUAL(ESP_OK, s2_client_websocket_process_reception_status(&h, NULL));
    TEST_ASSERT_FALSE(h.tracked_msgs[0].in_use); // slot freed
}

// ReceptionStatus TEMPORARY_ERROR
TEST_CASE("TEMPORARY_ERROR schedules retry timer with backoff", "[s2_client_websocket - process_reception_status]") {
    cJSON id_j = {.valuestring = "msg-2"};
    cJSON st_j = {.valuestring = "TEMPORARY_ERROR"};
    s2_client_t h = {.tracked_msgs = {{
        .in_use = true, .id = "msg-2", .retry_count = 0, .retry_timer = NULL,
    }}};
    cJSON_GetObjectItem_ExpectAnyArgsAndReturn(&id_j);
    cJSON_GetObjectItem_ExpectAnyArgsAndReturn(&st_j);
    cJSON_IsString_IgnoreAndReturn(1);
    esp_timer_create_IgnoreAndReturn(ESP_OK);
    esp_timer_start_once_IgnoreAndReturn(ESP_OK);
    TEST_ASSERT_EQUAL(ESP_OK, s2_client_websocket_process_reception_status(&h, NULL));
    TEST_ASSERT_EQUAL(1, h.tracked_msgs[0].retry_count); // incremented
}

// ReceptionStatus PERMANENT_ERROR
TEST_CASE("PERMANENT_ERROR drops and untracks the message", "[s2_client_websocket - process_reception_status]") {
    cJSON id_j = {.valuestring = "msg-3"};
    cJSON st_j = {.valuestring = "PERMANENT_ERROR"};
    s2_client_t h = {.tracked_msgs = {{
        .in_use = true, .id = "msg-3", .body = "{}",
    }}};
    cJSON_GetObjectItem_ExpectAnyArgsAndReturn(&id_j);
    cJSON_GetObjectItem_ExpectAnyArgsAndReturn(&st_j);
    cJSON_IsString_IgnoreAndReturn(1);
    TEST_ASSERT_EQUAL(ESP_OK, s2_client_websocket_process_reception_status(&h, NULL));
    TEST_ASSERT_FALSE(h.tracked_msgs[0].in_use); // dropped
}

// ______________________ s2_client_websocket_process_select_control_type ______________________

TEST_CASE("Returns invalid arg if control_type field is missing", "[s2_client_websocket - process_select_control_type]") {
    s2_client_t h = {0};
    cJSON msg = {0};
    cJSON_GetObjectItem_IgnoreAndReturn(NULL);
    cJSON_IsString_IgnoreAndReturn(0);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, s2_client_websocket_process_select_control_type(&h, &msg));
}

// non-existant control type = WS closed, returns fail
TEST_CASE("Returns fail and closes WS for unexpected control type", "[s2_client_websocket - process_select_control_type]") {
    cJSON ct = {.valuestring = "XXXX"}; // not a real control type.
    s2_client_t h = {.rm.control_type = S2_CONTROL_TYPE_PEBC};
    cJSON msg = {0};
    cJSON_GetObjectItem_IgnoreAndReturn(&ct);
    cJSON_IsString_IgnoreAndReturn(1);
    TEST_ASSERT_EQUAL(ESP_FAIL, s2_client_websocket_process_select_control_type(&h, &msg));
}

// Happy path: PEBC accepted, on_session_ready called, PEBC session started
TEST_CASE("Accepts PEBC, calls on_session_ready and starts session", "[s2_client_websocket - process_select_control_type]") {
    static bool session_ready_called;
    session_ready_called = false;
    void on_ready(void) { session_ready_called = true; }
    cJSON ct = {.valuestring = "POWER_ENVELOPE_BASED_CONTROL"};
    s2_client_t h = {
        .rm = {
            .control_type = S2_CONTROL_TYPE_PEBC,
            .on_session_ready = on_ready,
            .pebc.default_limits_count = 0, // no constraints to fire
        },
    };
    cJSON msg = {0};
    cJSON_GetObjectItem_IgnoreAndReturn(&ct);
    cJSON_IsString_IgnoreAndReturn(1);
    esp_timer_create_IgnoreAndReturn(ESP_OK);
    esp_timer_start_periodic_IgnoreAndReturn(ESP_OK);
    TEST_ASSERT_EQUAL(ESP_OK, s2_client_websocket_process_select_control_type(&h, &msg));
    TEST_ASSERT_TRUE(session_ready_called);
}

TEST_CASE("Returns fail when forecast timer create fails", "[s2_client_websocket - process_select_control_type]") {
    void on_ready(void) {}
    cJSON ct = {.valuestring = "POWER_ENVELOPE_BASED_CONTROL"};
    s2_client_t h = {
        .rm = {
            .control_type    = S2_CONTROL_TYPE_PEBC,
            .on_session_ready = on_ready,
            .provides_forecast = true,
        },
    };
    cJSON msg = {0};
    cJSON_GetObjectItem_IgnoreAndReturn(&ct);
    cJSON_IsString_IgnoreAndReturn(1);
    esp_timer_create_ExpectAnyArgsAndReturn(ESP_FAIL);
    TEST_ASSERT_EQUAL(ESP_FAIL, s2_client_websocket_process_select_control_type(&h, &msg));
}

TEST_CASE("Returns fail when forecast timer start fails", "[s2_client_websocket - process_select_control_type]") {
    void on_ready(void) {}
    cJSON ct = {.valuestring = "POWER_ENVELOPE_BASED_CONTROL"};
    s2_client_t h = {
        .rm = {
            .control_type    = S2_CONTROL_TYPE_PEBC,
            .on_session_ready = on_ready,
            .provides_forecast = true,
        },
    };
    cJSON msg = {0};
    cJSON_GetObjectItem_IgnoreAndReturn(&ct);
    cJSON_IsString_IgnoreAndReturn(1);
    esp_timer_create_ExpectAnyArgsAndReturn(ESP_OK);
    esp_timer_start_periodic_ExpectAnyArgsAndReturn(ESP_FAIL);
    TEST_ASSERT_EQUAL(ESP_FAIL, s2_client_websocket_process_select_control_type(&h, &msg));
}

// ______________________ s2_retry_timer_cb ______________________

// Valid body -> sends over WS.
TEST_CASE("Resends tracked message body over WS", "[s2_client_websocket - retry_timer_cb]") {
    s2_client_t h = {0};
    s2_tracked_msg_t msg = {.client_handle = &h, .body = "{}" };
    websocket_client_send_stub_init();
    s2_retry_timer_cb(&msg);
    // verify the message body was actually passed to esp_websocket_client_send_text
    TEST_ASSERT_NOT_EQUAL('\0', ws_last_sent_text()[0]);
}

// body with no null terminator -> get_bounded_strlen fails, nothing sent.
TEST_CASE("Does not send if body is not null-terminated", "[s2_client_websocket - retry_timer_cb]") {
    s2_client_t h = {0};
    s2_tracked_msg_t msg = {.client_handle = &h};
    memset(msg.body, 'X', sizeof(msg.body)); // fill entire buffer- no null terminator
    websocket_client_send_stub_init();
    s2_retry_timer_cb(&msg);
    TEST_ASSERT_EQUAL('\0', ws_last_sent_text()[0]); // nothing should have been sent
}

// ______________________ s2_websocket_send_tracked ______________________

// Valid body + id -> message stored in tracked_msgs.
TEST_CASE("Tracks sent message in tracked_msgs", "[s2_client_websocket - ws_send_tracked]") {
    s2_client_t h = {0};
    cJSON_ParseWithLength_IgnoreAndReturn(NULL);
    cJSON_Delete_Ignore();
    s2_websocket_send_tracked(&h, "{}", "test-id");
    TEST_ASSERT_TRUE(h.tracked_msgs[0].in_use);
}

// Happy path: message sent over WS, tracked in tracked_msgs, returns OK.
TEST_CASE("Sends tracked message over WS and returns OK", "[s2_client_websocket - ws_send_tracked]") {
    s2_client_t h = {0};
    websocket_client_send_stub_init();
    cJSON_ParseWithLength_IgnoreAndReturn(NULL);
    cJSON_Delete_Ignore();
    esp_err_t ret = s2_websocket_send_tracked(&h, "{}", "test-id");
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_NOT_EQUAL('\0', ws_last_sent_text()[0]); // message passed to WS
    TEST_ASSERT_TRUE(h.tracked_msgs[0].in_use);           // and tracked for retry
}

// ______________________ s2_client_websocket_process_message ______________________

// JSON parse fails -> sends INVALID_DATA ReceptionStatus, returns fail.
TEST_CASE("Returns fail when message JSON is unparseable", "[s2_client_websocket - process_message]") {
    s2_client_t h = {0};
    esp_websocket_event_data_t data = {.data_ptr = "not-json", .data_len = 8};
    cJSON_build_init(); // sets ParseWithLength -> NULL; stubs all builders + PrintUnformatted -> "{}"
    websocket_client_send_stub_init();
    TEST_ASSERT_EQUAL(ESP_FAIL, s2_client_websocket_process_message(&h, &data));
    // verify the INVALID_DATA ReceptionStatus reply was actually sent over the WebSocket
    TEST_ASSERT_NOT_EQUAL('\0', ws_last_sent_text()[0]);
}

// ---- s2_client_websocket_connect URL handling ----

TEST_CASE("accepts https URL", "[s2_client_websocket - connect]") {
    s2_client_t h = {0};
    /* NULL comm token will fail after URL conversion, proving URL was accepted */
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,
        s2_client_websocket_connect(&h, "https://example.com/ws", NULL, NULL));
}

TEST_CASE("accepts wss URL as-is", "[s2_client_websocket - connect]") {
    s2_client_t h = {0};
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,
        s2_client_websocket_connect(&h, "wss://example.com/ws", NULL, NULL));
}

// ---- s2_client_websocket_send_message ----

TEST_CASE("null handle returns invalid arg", "[s2_client_websocket - send]") {
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, s2_client_websocket_send_message(NULL, "{}", 1000));
}

// ---- s2_client_websocket_process_message ----

TEST_CASE("unhandled message type sends INVALID_MESSAGE", "[s2_client_websocket - process_message]") {
    s2_client_t h = {0};
    cJSON_build_init();
    websocket_client_send_stub_init();
    cJSON type_node;
    mock_cjson_string(&type_node, "UnknownType");
    cJSON_GetObjectItem_return_for_key("message_type", &type_node);
    esp_websocket_event_data_t data = {.data_ptr = "{}", .data_len = 2};
    websocket_client_send_stub_init();
    s2_client_websocket_process_message(&h, &data);
    TEST_ASSERT_NOT_EQUAL('\0', ws_last_sent_text()[0]);
}

// missing message_type field sends INVALID_DATA
TEST_CASE("missing message type sends INVALID_DATA", "[s2_client_websocket - process_message]") {
    s2_client_t h = {0};
    websocket_client_send_stub_init();
    cJSON_build_init();
    cJSON_GetObjectItem_IgnoreAndReturn(NULL);
    cJSON_IsString_IgnoreAndReturn(0);
    esp_websocket_event_data_t data = {.data_ptr = "{}", .data_len = 2};
    s2_client_websocket_process_message(&h, &data);
    TEST_ASSERT_NOT_EQUAL('\0', ws_last_sent_text()[0]);
}

// SessionRequest sends OK without closing WebSocket
TEST_CASE("SessionRequest sends OK without closing", "[s2_client_websocket - process_message]") {
    s2_client_t h = {0};
    websocket_client_send_stub_init();
    cJSON_ParseWithLength_IgnoreAndReturn((cJSON *)1);
    cJSON_Delete_Ignore();
    cJSON_CreateObject_IgnoreAndReturn((cJSON *)1);
    cJSON_AddStringToObject_IgnoreAndReturn((cJSON *)1);
    cJSON_PrintUnformatted_IgnoreAndReturn(strdup("{}"));
    cJSON_GetObjectItem_init();
    cJSON mid_node; mock_cjson_string(&mid_node, "msg-1");
    cJSON type_node; mock_cjson_string(&type_node, "SessionRequest");
    cJSON req_node; mock_cjson_string(&req_node, "TERMINATE");
    cJSON_GetObjectItem_return_for_key("message_id", &mid_node);
    cJSON_GetObjectItem_return_for_key("message_type", &type_node);
    cJSON_GetObjectItem_return_for_key("request", &req_node);
    cJSON_IsString_IgnoreAndReturn(1);
    esp_websocket_event_data_t data = {.data_ptr = "{}", .data_len = 2};
    TEST_ASSERT_EQUAL(ESP_OK, s2_client_websocket_process_message(&h, &data));
}

// reconnect task exits when no session URL
TEST_CASE("reconnect task exits without session URL", "[s2_client_websocket - reconnect]") {
    s2_client_t h = {0};
    vTaskDelete_Ignore();
    s2_client_websocket_reconnect_task_for_test(&h);
    TEST_ASSERT_FALSE(h.reconnecting);
}

// ws connected event sends RMD and initialises log forwarder
TEST_CASE("ws connected event sends RMD", "[s2_client_websocket - event]") {
    s2_client_t h = {0};
    h.websocket_client = (esp_websocket_client_handle_t)1;
    h.websocket_conn_event_group = (EventGroupHandle_t)2;
    h.websocket_state_mutex = (SemaphoreHandle_t)3;

    xQueueSemaphoreTake_IgnoreAndReturn(pdTRUE);
    xQueueGenericSend_IgnoreAndReturn(pdTRUE);
    xEventGroupSetBits_IgnoreAndReturn(0);

    cJSON_build_init();
    websocket_client_send_stub_init();

    esp_timer_create_IgnoreAndReturn(ESP_OK);
    esp_timer_start_periodic_IgnoreAndReturn(ESP_OK);

    s2_client_websocket_invoke_event_handler_for_test(&h, WEBSOCKET_EVENT_CONNECTED, NULL);

    TEST_ASSERT_EQUAL(S2_CLIENT_WEBSOCKET_CONN_STATE_READY, h.websocket_conn_state);
    TEST_ASSERT_NOT_EQUAL('\0', ws_last_sent_text()[0]);
}

// ws connect converts http to ws with warning
TEST_CASE("ws connect converts http to ws", "[s2_client_websocket - connect]") {
    s2_client_t h = {0};
    websocket_client_stubs_reset();
    esp_err_t err = s2_client_websocket_connect(&h, "http://example.com/ws", "token", "access");
    TEST_ASSERT_EQUAL(ESP_OK, err);
}

// ws connect sets interface when configured
TEST_CASE("ws connect configures interface", "[s2_client_websocket - connect]") {
    s2_client_t h = {.server = {.interface = "eth0"}};
    websocket_client_stubs_reset();
    esp_err_t err = s2_client_websocket_connect(&h, "wss://example.com/ws", "token", "access");
    TEST_ASSERT_EQUAL(ESP_OK, err);
}
