// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

/* Minimal mock of espressif/nvs_flash for unit tests. */

#pragma once

#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t nvs_handle_t;

typedef enum {
    NVS_READONLY  = 0x01,
    NVS_READWRITE = 0x02,
} nvs_open_mode_t;

esp_err_t nvs_open(const char *namespace_name, nvs_open_mode_t open_mode,
                   nvs_handle_t *out_handle);

void nvs_close(nvs_handle_t handle);

esp_err_t nvs_get_blob(nvs_handle_t handle, const char *key,
                       void *out_value, size_t *length);

esp_err_t nvs_set_blob(nvs_handle_t handle, const char *key,
                       const void *value, size_t length);

esp_err_t nvs_commit(nvs_handle_t handle);

#ifdef __cplusplus
}
#endif
