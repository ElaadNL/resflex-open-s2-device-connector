// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "esp_http_client.h"

/**
 * @brief Install recording stubs for esp_http_client_init
 *
 * Must be called before the code under test runs. After the code under tests has run,
 * the helpers below can be used to assert which values have been configured for the HTTP
 * client
 *
 * Resets the recorded configuration on every invocation
 */
void http_client_record_init(void);

/**
 * @brief Returns true when HTTP client interface was set with the given value
 */
bool http_client_was_iface_set(const char *iface);

/**
 * @brief Returns true when HTTP client certificates (PEM) was set with the given value
 */
bool http_client_was_cert_set(const char *cert);
