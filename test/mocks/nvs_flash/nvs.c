// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

/* Minimal mock of espressif/nvs_flash for unit tests.
   Stores blobs in a static in-memory table so tests can verify persistence. */

#include "nvs.h"
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

#define MAX_NVS_HANDLES 8
#define MAX_NVS_ENTRIES 32
#define MAX_BLOB_SIZE   256

typedef struct {
    char key[32];
    uint8_t data[MAX_BLOB_SIZE];
    size_t len;
    bool used;
} nvs_entry_t;

typedef struct {
    char ns[16];
    nvs_entry_t entries[MAX_NVS_ENTRIES];
    bool open;
} nvs_namespace_t;

static nvs_namespace_t s_namespaces[MAX_NVS_HANDLES];
static bool s_initialized = false;

static void ensure_init(void) {
    if (!s_initialized) {
        memset(s_namespaces, 0, sizeof(s_namespaces));
        s_initialized = true;
    }
}

static nvs_namespace_t *find_ns(const char *name) {
    for (int i = 0; i < MAX_NVS_HANDLES; i++) {
        if (s_namespaces[i].open && strcmp(s_namespaces[i].ns, name) == 0) {
            return &s_namespaces[i];
        }
    }
    return NULL;
}

static nvs_namespace_t *alloc_ns(const char *name) {
    for (int i = 0; i < MAX_NVS_HANDLES; i++) {
        if (!s_namespaces[i].open) {
            strncpy(s_namespaces[i].ns, name, sizeof(s_namespaces[i].ns) - 1);
            s_namespaces[i].open = true;
            return &s_namespaces[i];
        }
    }
    return NULL;
}

esp_err_t nvs_open(const char *namespace_name, nvs_open_mode_t open_mode,
                   nvs_handle_t *out_handle) {
    (void)open_mode;
    ensure_init();
    if (!namespace_name || !out_handle) return ESP_ERR_INVALID_ARG;

    nvs_namespace_t *ns = find_ns(namespace_name);
    if (!ns) {
        if (open_mode == NVS_READONLY) return ESP_ERR_NOT_FOUND;
        ns = alloc_ns(namespace_name);
        if (!ns) return ESP_ERR_NO_MEM;
    }

    *out_handle = (nvs_handle_t)(ns - s_namespaces);
    return ESP_OK;
}

void nvs_close(nvs_handle_t handle) {
    if (handle >= MAX_NVS_HANDLES) return;
    /* keep data; just mark closed for reopen detection */
}

esp_err_t nvs_get_blob(nvs_handle_t handle, const char *key,
                       void *out_value, size_t *length) {
    if (handle >= MAX_NVS_HANDLES || !key || !out_value || !length)
        return ESP_ERR_INVALID_ARG;

    nvs_namespace_t *ns = &s_namespaces[handle];
    for (int i = 0; i < MAX_NVS_ENTRIES; i++) {
        if (ns->entries[i].used && strcmp(ns->entries[i].key, key) == 0) {
            size_t copy_len = ns->entries[i].len < *length
                                  ? ns->entries[i].len : *length;
            memcpy(out_value, ns->entries[i].data, copy_len);
            *length = ns->entries[i].len;
            return ESP_OK;
        }
    }
    return ESP_ERR_NOT_FOUND;
}

esp_err_t nvs_set_blob(nvs_handle_t handle, const char *key,
                       const void *value, size_t length) {
    if (handle >= MAX_NVS_HANDLES || !key || !value || length > MAX_BLOB_SIZE)
        return ESP_ERR_INVALID_ARG;

    nvs_namespace_t *ns = &s_namespaces[handle];

    /* find existing or empty slot */
    int slot = -1;
    for (int i = 0; i < MAX_NVS_ENTRIES; i++) {
        if (ns->entries[i].used && strcmp(ns->entries[i].key, key) == 0) {
            slot = i;
            break;
        }
        if (slot < 0 && !ns->entries[i].used) slot = i;
    }
    if (slot < 0) return ESP_ERR_NO_MEM;

    memcpy(ns->entries[slot].data, value, length);
    ns->entries[slot].len = length;
    strncpy(ns->entries[slot].key, key, sizeof(ns->entries[slot].key) - 1);
    ns->entries[slot].used = true;
    return ESP_OK;
}

esp_err_t nvs_commit(nvs_handle_t handle) {
    (void)handle;
    /* in-memory mock: nothing to flush */
    return ESP_OK;
}
