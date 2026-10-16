// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <stdbool.h>
#include "esp_websocket_client.h"

/**
 * @brief Reset the WebSocket send stub state and prepare it to capture the next send.
 *
 * Call this at the start of a test that exercises esp_websocket_client_send_text.
 * The linker --wrap flag redirects all calls to esp_websocket_client_send_text to
 * __wrap_esp_websocket_client_send_text, which records the transmitted text.
 */
void websocket_client_send_stub_init(void);

/**
 * @brief Return the last text body passed to esp_websocket_client_send_text.
 *
 * @return Null-terminated string of the last send, or an empty string if
 *         websocket_client_send_stub_init() has not been called or no send occurred.
 */
const char *ws_last_sent_text(void);

/**
 * @brief Resets WebSocket destroy stub state
 *
 * Call this at the start of a test that exercises esp_websocket_client_destroy. The linker
 * --wrap flag redirects all calls to esp_websocket_client_destroy to
 * __wrap_esp_websocket_client_destroy, which records calls
 */
void websocket_client_destroy_stub_init(void);

/**
 * @brief Checks if the WS client has been destroyed and resources are freed
 *
 * @return True/false based on if the client is destroyed
 */
bool websocket_client_destroyed(void);

/**
 * @brief Reset all WebSocket stub state (sent text, destroyed flag, init/start counts).
 *
 * Call at the start of each test that uses websocket stubs.
 */
void websocket_client_stubs_reset(void);

/**
 * @brief Returns the handle passed to the last esp_websocket_client_init call.
 */
esp_websocket_client_handle_t ws_stub_last_init_handle(void);

/**
 * @brief Returns the number of times esp_websocket_client_start was called.
 */
int ws_stub_start_call_count(void);