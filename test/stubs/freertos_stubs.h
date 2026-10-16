// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "esp_err.h"

/**
 * @brief Install an xQueueGenericSend stub that simulates a successful HTTP request
 *
 * The stub reads back the queued s2_request_t pointer and updates its `result`
 * field with the supplied value before returning pdTRUE. This allows tests
 * to exercise s2_client_http_add_request without a worker task actually
 * processing the queue.
 *
 * @param result Value that will be written into request->result
 */
void freertos_stubs_queue_send_set_request_result(esp_err_t result);
