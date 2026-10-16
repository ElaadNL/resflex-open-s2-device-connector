// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

#include "mdns.h"

esp_err_t mdns_query_ptr(const char *service_type, const char *proto,
                          int timeout_ms, size_t max_results,
                          mdns_result_t **results) {
    (void)service_type; (void)proto; (void)timeout_ms; (void)max_results;
    *results = NULL;
    return ESP_OK;
}

void mdns_query_results_free(mdns_result_t *results) {
    (void)results;
}

esp_err_t mdns_query_a(const char *hostname, int timeout_ms,
                        esp_ip4_addr_t *addr) {
    (void)hostname; (void)timeout_ms; (void)addr;
    return ESP_FAIL;
}
