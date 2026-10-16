// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "esp_http_client.h"
#include "esp_err.h"

// Direct mock of "esp_http_client" is not possible because of CMock not being
// able to parse complex logic in the original header file (e.g. macros etc.)

esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *config);
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t client);
esp_err_t esp_http_client_perform(esp_http_client_handle_t client);
esp_err_t esp_http_client_prepare(esp_http_client_handle_t client);
esp_err_t esp_http_client_request_send(esp_http_client_handle_t client, int write_len);
esp_err_t esp_http_client_cancel_request(esp_http_client_handle_t client);
esp_err_t esp_http_client_get_user_data(esp_http_client_handle_t client, void **data);
esp_err_t esp_http_client_delete_all_headers(esp_http_client_handle_t client);
esp_err_t esp_http_client_set_user_data(esp_http_client_handle_t client, void *data);
esp_err_t esp_http_client_set_url(esp_http_client_handle_t client, const char *url);
esp_err_t esp_http_client_set_method(esp_http_client_handle_t client, esp_http_client_method_t method);
esp_err_t esp_http_client_set_header(esp_http_client_handle_t client, const char *key, const char *value);
esp_err_t esp_http_client_set_post_field(esp_http_client_handle_t client, const char *data, int len);
int esp_http_client_get_status_code(esp_http_client_handle_t client);
