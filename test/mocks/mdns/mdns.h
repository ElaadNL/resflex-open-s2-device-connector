// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

/* Minimal mock of espressif/mdns for unit tests. Tests don't exercise DNS-SD. */

#pragma once

#include "esp_err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct { uint32_t addr; } esp_ip4_addr_t;
static inline char *esp_ip4addr_ntoa(const esp_ip4_addr_t *a, char *buf, int len) {
    (void)a; (void)len; buf[0] = '\0'; return buf;
}

typedef struct {
    struct {
        union {
            esp_ip4_addr_t ip4;
        } u_addr;
    } addr;
} mdns_ip_addr_t;

typedef struct mdns_txt_item {
    const char *key;
    const char *value;
} mdns_txt_item_t;

typedef struct mdns_result {
    struct mdns_result *next;
    mdns_txt_item_t *txt;
    size_t txt_count;
    uint16_t port;
    const char *hostname;
    mdns_ip_addr_t *addr;
} mdns_result_t;

esp_err_t mdns_query_ptr(const char *service_type, const char *proto,
                          int timeout_ms, size_t max_results,
                          mdns_result_t **results);

esp_err_t mdns_query_a(const char *hostname, int timeout_ms,
                        esp_ip4_addr_t *addr);

void mdns_query_results_free(mdns_result_t *results);

#ifdef __cplusplus
}
#endif
