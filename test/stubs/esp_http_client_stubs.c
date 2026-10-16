// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

#include "esp_http_client_stubs.h"
#include "Mockesp_http_client_mock.h"
#include "unity.h"
#include <net/if.h>
#include <string.h>

#define MOCK_HTTP_CLIENT ((esp_http_client_handle_t)0xdeadbeef)

// _____________________________ Recording _____________________________

static const char *m_iface = NULL;
static const char *m_cert_pem = NULL;

static esp_http_client_handle_t http_client_init_stub(const esp_http_client_config_t *config, int num_calls) {
    if (config->if_name != NULL) m_iface = config->if_name->ifr_name;
    m_cert_pem = config->cert_pem;
    return MOCK_HTTP_CLIENT;
}

void http_client_record_init(void) {
    m_iface = NULL;
    m_cert_pem = NULL;
    esp_http_client_init_Stub(http_client_init_stub);
}

bool http_client_was_iface_set(const char *iface) {
    if (iface == NULL) return m_iface == NULL;
    if (m_iface == NULL) return false;
    return strcmp(m_iface, iface) == 0;
}

bool http_client_was_cert_set(const char *cert) {
    if (cert == NULL) return m_cert_pem == NULL;
    if (m_cert_pem == NULL) return false;
    return strcmp(m_cert_pem, cert) == 0;
}

// These two are not mocked, because no test asserts on them: they only have to
// resolve at link time so the sources that call them can be built for linux.

esp_err_t esp_http_client_close(esp_http_client_handle_t client) {
    (void)client;
    return ESP_OK;
}

esp_err_t esp_http_client_set_timeout_ms(esp_http_client_handle_t client, int timeout_ms) {
    (void)client;
    (void)timeout_ms;
    return ESP_OK;
}
