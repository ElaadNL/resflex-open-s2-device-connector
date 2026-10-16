// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

#include "esp_websocket_client_stubs.h"
#include "esp_websocket_client.h"
#include <string.h>

// Maximum body size we capture (matches S2_MAX_TEXT_MESSAGE_LEN in the library)
#define WS_STUB_BUF_SIZE 4096

static char m_last_sent[WS_STUB_BUF_SIZE] = {0};
static bool m_enabled = false;

/**
 * GNU ld --wrap=esp_websocket_client_send_text replaces every call to
 * esp_websocket_client_send_text with this function.  The original symbol is
 * still accessible as __real_esp_websocket_client_send_text if needed.
 */
int __wrap_esp_websocket_client_send_text(esp_websocket_client_handle_t client,
                                          const char *data, int len,
                                          TickType_t timeout)
{
    (void)client;
    (void)timeout;
    if (m_enabled && data != NULL && len > 0) {
        int copy_len = len < WS_STUB_BUF_SIZE - 1 ? len : WS_STUB_BUF_SIZE - 1;
        memcpy(m_last_sent, data, copy_len);
        m_last_sent[copy_len] = '\0';
    }
    return len; // simulate a successful send of len bytes
}

void websocket_client_send_stub_init(void)
{
    m_last_sent[0] = '\0';
    m_enabled = true;
}

const char *ws_last_sent_text(void)
{
    return m_last_sent;
}

static bool m_websocket_client_destroyed = false;

esp_err_t __wrap_esp_websocket_client_destroy(esp_websocket_client_handle_t client) {
    m_websocket_client_destroyed = true;
    return ESP_OK;
}

void websocket_client_destroy_stub_init(void) {
    m_websocket_client_destroyed = false;
}

bool websocket_client_destroyed(void) {
    return m_websocket_client_destroyed;
}

/* ── Additional --wrap stubs for reconnect tests ── */

static esp_websocket_client_handle_t m_last_init_handle = NULL;
static int m_start_call_count = 0;

esp_err_t __wrap_esp_websocket_client_stop(esp_websocket_client_handle_t client)
{
    (void)client;
    return ESP_OK;
}

esp_websocket_client_handle_t __wrap_esp_websocket_client_init(
        const esp_websocket_client_config_t *config)
{
    (void)config;
    /* Return a unique non-NULL handle each time */
    static int counter = 100;
    m_last_init_handle = (esp_websocket_client_handle_t)(intptr_t)(++counter);
    return m_last_init_handle;
}

esp_err_t __wrap_esp_websocket_register_events(
        esp_websocket_client_handle_t client,
        int event, esp_event_handler_t handler, void *arg)
{
    (void)client; (void)event; (void)handler; (void)arg;
    return ESP_OK;
}

esp_err_t __wrap_esp_websocket_client_start(esp_websocket_client_handle_t client)
{
    (void)client;
    m_start_call_count++;
    return ESP_OK;
}

void websocket_client_stubs_reset(void)
{
    m_last_sent[0] = '\0';
    m_enabled = false;
    m_websocket_client_destroyed = false;
    m_last_init_handle = NULL;
    m_start_call_count = 0;
}

esp_websocket_client_handle_t ws_stub_last_init_handle(void)
{
    return m_last_init_handle;
}

int ws_stub_start_call_count(void)
{
    return m_start_call_count;
}