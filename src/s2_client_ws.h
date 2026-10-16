// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "s2_client.h"

const char *test_err_to_reception_status(esp_err_t err);
void test_s2_forecast_timer_cb(void *arg);
// Test helper: invoke internal websocket event handler from unit tests
void s2_client_websocket_invoke_event_handler_for_test(void *handle, int32_t event_id, void *data);
// Test helper: invoke the static reconnect task from unit tests
void s2_client_websocket_reconnect_task_for_test(void *arg);