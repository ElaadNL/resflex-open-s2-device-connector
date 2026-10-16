// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

#include "s2_client_initiate_stubs.h"
#include "s2_client_internal.h"
#include <string.h>

static esp_err_t next_return = ESP_OK;

void s2_websocket_connect_stub_set_return(esp_err_t v) {
    next_return = v;
}

void s2_websocket_connect_stub_reset(void) {
    next_return = ESP_OK;
}

/* Stub: wraps s2_client_websocket_connect for unit tests */
esp_err_t __wrap_s2_client_websocket_connect(s2_client_handle_t client_handle,
                                const char *url,
                                const char *bearer_token,
                                const char *access_token) {
    /* Validate args like the real function */
    if (!client_handle || !url || !bearer_token) return ESP_ERR_INVALID_ARG;
    (void)access_token;
    return next_return;
}

/* Stub: s2_client_fetch_server_certificate is excluded from tests (uses real TLS) */
esp_err_t s2_client_fetch_server_certificate(s2_client_handle_t client_handle) {
    (void)client_handle;
    return ESP_OK;
}

/* Wrapped session calls: the reconnect logic needs to see a refusal, a transient
 * failure and a success, which the real code only produces through the HTTP queue. */

/* The linker creates these alongside the wrapped functions. */
esp_err_t __real_s2_client_initiate_session(s2_client_handle_t client_handle,
                                            const char *server_node_id,
                                            const char *access_token,
                                            s2_client_initiate_session_response_t *response);
esp_err_t __real_s2_client_confirm_access_token_extended(s2_client_handle_t client_handle,
                                                         const char *access_token,
                                                         s2_client_confirm_access_token_response_t *response);

static bool m_initiate_stubbed = false;
static int m_initiate_fail_times = 0;
static esp_err_t m_initiate_then = ESP_OK;
static bool m_confirm_stubbed = false;
static esp_err_t m_confirm_return = ESP_OK;

void s2_initiate_session_stub_fail_times(int times, esp_err_t then) {
    m_initiate_stubbed = true;
    m_initiate_fail_times = times;
    m_initiate_then = then;
}

void s2_confirm_access_token_stub_set_return(esp_err_t err) {
    m_confirm_stubbed = true;
    m_confirm_return = err;
}

void s2_session_stubs_reset(void) {
    m_initiate_stubbed = false;
    m_initiate_fail_times = 0;
    m_initiate_then = ESP_OK;
    m_confirm_stubbed = false;
    m_confirm_return = ESP_OK;
}

esp_err_t __wrap_s2_client_initiate_session(s2_client_handle_t client_handle,
                                            const char *server_node_id,
                                            const char *access_token,
                                            s2_client_initiate_session_response_t *response) {
    if (!m_initiate_stubbed) {
        return __real_s2_client_initiate_session(client_handle, server_node_id, access_token, response);
    }
    if (m_initiate_fail_times > 0) {
        m_initiate_fail_times--;
        return ESP_FAIL;
    }
    if (m_initiate_then == ESP_OK && response != NULL) {
        strlcpy(response->access_token, "stub-pending-token", sizeof(response->access_token));
    }
    return m_initiate_then;
}

esp_err_t __wrap_s2_client_confirm_access_token_extended(s2_client_handle_t client_handle,
                                                         const char *access_token,
                                                         s2_client_confirm_access_token_response_t *response) {
    if (!m_confirm_stubbed) {
        return __real_s2_client_confirm_access_token_extended(client_handle, access_token, response);
    }
    if (m_confirm_return == ESP_OK && response != NULL) {
        strlcpy(response->websocket_url, "wss://cem/ws", sizeof(response->websocket_url));
        strlcpy(response->websocket_token, "stub-comm-token", sizeof(response->websocket_token));
    }
    return m_confirm_return;
}
