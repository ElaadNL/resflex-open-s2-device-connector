// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

#include "s2_client.h"
#include "s2_client_http.h"
#include "s2_client_internal.h"
#include "s2_client_pairing.h"

#include <string.h>
#include "esp_log.h"
#include "nvs.h"
#include <ctype.h>

static const char *TAG = "s2_client";

// NVS blob: [count:1][token0:33]...[tokenN:33]
#define NVS_BLOB_SIZE (1 + S2_MAX_ACCESS_TOKENS * S2_ACCESS_TOKEN_LEN)

static void nvs_load_tokens(s2_client_t *client) {
    nvs_handle_t nvs;
    if (nvs_open(S2_NVS_NAMESPACE, NVS_READONLY, &nvs) != ESP_OK) return;

    uint8_t buf[NVS_BLOB_SIZE];
    size_t size = sizeof(buf);
    if (nvs_get_blob(nvs, S2_NVS_KEY_ACCESS_TOKENS, buf, &size) == ESP_OK && size >= 1) {
        client->access_token_count = buf[0];
        if (client->access_token_count > S2_MAX_ACCESS_TOKENS)
            client->access_token_count = S2_MAX_ACCESS_TOKENS;
        for (int i = 0; i < client->access_token_count; i++) {
            memcpy(client->access_tokens[i],
                   buf + 1 + i * S2_ACCESS_TOKEN_LEN,
                   S2_ACCESS_TOKEN_LEN - 1);
            client->access_tokens[i][S2_ACCESS_TOKEN_LEN - 1] = '\0';
        }
    }

    size = S2_TLS_FINGERPRINT_SIZE;
    nvs_get_blob(nvs, S2_NVS_KEY_TLS_FINGERPRINT, client->tls_cert_fingerprint, &size);

    nvs_close(nvs);
}

static void nvs_save_all(const s2_client_t *client) {
    nvs_handle_t nvs;
    if (nvs_open(S2_NVS_NAMESPACE, NVS_READWRITE, &nvs) != ESP_OK) return;

    uint8_t buf[NVS_BLOB_SIZE];
    memset(buf, 0, sizeof(buf));
    buf[0] = client->access_token_count;
    for (int i = 0; i < client->access_token_count; i++) {
        memcpy(buf + 1 + i * S2_ACCESS_TOKEN_LEN,
               client->access_tokens[i], S2_ACCESS_TOKEN_LEN - 1);
    }
    nvs_set_blob(nvs, S2_NVS_KEY_ACCESS_TOKENS, buf,
                 1 + client->access_token_count * S2_ACCESS_TOKEN_LEN);
    nvs_set_blob(nvs, S2_NVS_KEY_TLS_FINGERPRINT,
                 client->tls_cert_fingerprint, S2_TLS_FINGERPRINT_SIZE);
    nvs_commit(nvs);
    nvs_close(nvs);
}

void s2_client_add_access_token(s2_client_handle_t client_handle,
                                const char *token) {
    s2_client_t *c = client_handle;
    if (!c || !token || token[0] == '\0') return;

    // shift right, drop oldest if full
    int shift = c->access_token_count >= S2_MAX_ACCESS_TOKENS
        ? S2_MAX_ACCESS_TOKENS - 1 : c->access_token_count;
    for (int i = shift; i > 0; i--) {
        strlcpy(c->access_tokens[i], c->access_tokens[i - 1], S2_ACCESS_TOKEN_LEN);
    }
    strlcpy(c->access_tokens[0], token, S2_ACCESS_TOKEN_LEN);
    if (c->access_token_count < S2_MAX_ACCESS_TOKENS) c->access_token_count++;

    // also update last_access_token for immediate reconnect use
    strlcpy(c->last_access_token, token, sizeof(c->last_access_token));

    nvs_save_all(c);
}

void s2_client_prune_access_tokens(s2_client_t *c) {
    if (!c || c->access_token_count == 0) return;

    // keep only tokens[0], clear the rest. handles count=0 (wipes NVS).
    if (c->access_token_count > 0) {
        for (int i = 1; i < S2_MAX_ACCESS_TOKENS; i++) {
            c->access_tokens[i][0] = '\0';
        }
        c->access_token_count = 1;
    }
    nvs_save_all(c);
}

/**
 * @brief Validate a UUID string (xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx).
 */
static bool is_valid_uuid(const char *s) {
    if (!s) return false;
    if (strnlen(s, 64) != 36) return false;
    const int hyphen_pos[] = {8,13,18,23};
    for (int i = 0; i < 36; i++) {
        bool is_hyphen = false;
        for (int h = 0; h < 4; h++) if (i == hyphen_pos[h]) is_hyphen = true;
        if (is_hyphen) { if (s[i] != '-') return false; }
        else { if (!isxdigit((unsigned char)s[i])) return false; }
    }
    return true;
}

/**
 * @brief   Task processing all requests added to the request queue
 * 
 * @param   arg Should contain the S2 client handle containing the HTTP client
 */
static void s2_client_http_worker(void *arg) {
    s2_client_t *handle = arg;
    s2_request_t *req;

    // Process requests added to queue
    while (xQueueReceive(handle->request_queue, &req, portMAX_DELAY)) {
        req->result = s2_client_http_perform_request(handle, req);
        xSemaphoreGive(req->done); // Mark request as finished (release lock)
    }

    vTaskDelete(NULL);
}

/**
 * @brief Initialize all HTTP related properties of the S2 client handle.
 */
bool s2_client_init_http(s2_client_handle_t client, const s2_client_config_t *config) {
    // Initialize HTTP client
    esp_http_client_config_t http_cfg = {
        .url = config->server_config.base_url,
        .event_handler = s2_client_http_event_handler,
        .skip_cert_common_name_check = config->server_config.skip_cert_cn_check,
        .crt_bundle_attach = s2_client_no_verify_attach,
        .timeout_ms = S2_HTTP_DEFAULT_TIMEOUT_MS,
    };
    strlcpy(client->server.base_url, config->server_config.base_url, sizeof(config->server_config.base_url));
    s2_client_http_extract_host(client->server.base_url, client->server.host, sizeof(client->server.host));

    /* register handle for TLS cert pinning */
    s2_client_set_pinned_handle(client);

    // Configure TLS if enabled
    if (config->server_config.enable_tls) {
        if (config->server_config.cert_pem && config->server_config.cert_pem_len > 0) {
            client->server.cert_pem = malloc(config->server_config.cert_pem_len);
            if (!client->server.cert_pem) return false;
            memcpy(client->server.cert_pem, config->server_config.cert_pem, config->server_config.cert_pem_len);
            client->server.cert_pem_len = config->server_config.cert_pem_len;
            http_cfg.cert_pem = client->server.cert_pem;
            http_cfg.cert_len = client->server.cert_pem_len;
        }
        // cert_pem = NULL is valid for TOFU: cert verified via HMAC challenge-response
    }
    client->server.enable_tls = config->server_config.enable_tls;
    client->server.skip_cert_cn_check = config->server_config.skip_cert_cn_check;

    // Configure interface to use when set
    if (config->server_config.interface[0] != '\0') {
        struct ifreq if_name = {0};
        strlcpy(if_name.ifr_name, config->server_config.interface, sizeof(if_name.ifr_name));
        http_cfg.if_name = &if_name;

        strlcpy(client->server.interface, config->server_config.interface, sizeof(config->server_config.interface));
    }

    // Initialize the client with configuration
    client->http_client = esp_http_client_init(&http_cfg);
    if (client->http_client == NULL) {
        s2_client_destroy(client);
        return false;
    }

    // Create queue for HTTP requests
    client->request_queue = xQueueCreate(4, sizeof(s2_request_t *));
    if (client->request_queue == NULL) {
        s2_client_destroy(client);
        return false;
    }

    // Create HTTP worker task
    if (xTaskCreate(s2_client_http_worker, "s2_http", 16384, client, 5, &client->http_task) != pdTRUE) {
        s2_client_destroy(client);
        return false;
    }

    return true;
}

/**
 * @brief Check the node/server config fields are valid.
 *
 * @return true if required fields are non-empty and fit in their buffers.
 */
static bool node_config_valid(const s2_client_config_t *config) {
    if (config == NULL) return false;
    // Verify id is a UUID (xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx)

    if (config->node.id[0] == '\0'    || strnlen(config->node.id,    sizeof(config->node.id))    >= sizeof(config->node.id))    return false;
    if (!is_valid_uuid(config->node.id)) return false;
    if (config->node.brand[0] == '\0' || strnlen(config->node.brand, sizeof(config->node.brand)) >= sizeof(config->node.brand)) return false;
    if (config->node.type[0] == '\0'  || strnlen(config->node.type,  sizeof(config->node.type))  >= sizeof(config->node.type))  return false;
    if (config->node.model[0] == '\0' || strnlen(config->node.model, sizeof(config->node.model)) >= sizeof(config->node.model)) return false;
    if (strnlen(config->node.logo_uri,          sizeof(config->node.logo_uri))          >= sizeof(config->node.logo_uri))          return false;
    if (strnlen(config->node.user_defined_name, sizeof(config->node.user_defined_name)) >= sizeof(config->node.user_defined_name)) return false;
    if (config->server_config.base_url[0] == '\0' || strnlen(config->server_config.base_url, sizeof(config->server_config.base_url)) >= sizeof(config->server_config.base_url)) return false;
    if (config->server_config.interface[0] != '\0' && strnlen(config->server_config.interface, sizeof(config->server_config.interface)) >= sizeof(config->server_config.interface)) return false;
    return true;
}

static bool has_supported_version(const s2_client_config_t *config) {
    for (int i = 0; i < S2_NODE_MAX_SUPPORTED_VERSIONS; i++) {
        if (config->node.supported_versions[i][0] != '\0') return true;
    }
    return false;
}

void s2_client_destroy(s2_client_handle_t client) {
    if (client == NULL) return;

    // Stop and cleanup HTTP resources
    if (client->http_client != NULL) {
        esp_http_client_cleanup(client->http_client);
        client->http_client = NULL;
    }

    if (client->request_queue != NULL) {
        vQueueDelete(client->request_queue);
        client->request_queue = NULL;
    }

    if (client->http_task != NULL) {
        vTaskDelete(client->http_task);
        client->http_task = NULL;
    }

    // Cleanup WebSocket resources
    if (client->websocket_client != NULL) {
        esp_websocket_client_destroy(client->websocket_client);
        client->websocket_client = NULL;
    }

    // Cleanup TLS certificate if it was allocated
    if (client->server.cert_pem != NULL) {
        free(client->server.cert_pem);
        client->server.cert_pem = NULL;
    }

    // Cleanup Mutexes and Event Groups
    if (client->websocket_state_mutex != NULL) {
        vSemaphoreDelete(client->websocket_state_mutex);
        client->websocket_state_mutex = NULL;
    }
    if (client->websocket_conn_event_group != NULL) {
        vEventGroupDelete(client->websocket_conn_event_group);
        client->websocket_conn_event_group = NULL;
    }

    // Free the handle itself
    free(client);
}

s2_client_handle_t s2_client_init(const s2_client_config_t *config) {
    // Validate config
    if (!node_config_valid(config)) return NULL;
    if (!has_supported_version(config)) return NULL;

    // Validate mandatory RM config fields
    if (
        config->rm_config.roles_count == 0 || config->rm_config.roles_count > S2_RM_MAX_ROLES ||
        config->rm_config.commodity_quantities_count == 0 ||
        config->rm_config.commodity_quantities_count > S2_MAX_COMMODITY_QUANTITIES ||
        config->rm_config.on_request_measurement == NULL ||
        config->rm_config.on_session_ready == NULL
    ) {
        ESP_LOGE(TAG, "s2_client_init: Invalid RM config");
        return NULL;
    }

    // Validate PEBC-specific config fields when PEBC control type is selected
    if (config->rm_config.control_type == S2_CONTROL_TYPE_PEBC) {
        if (
            config->rm_config.pebc.default_limits_count == 0 ||
            config->rm_config.pebc.default_limits_count > S2_MAX_COMMODITY_QUANTITIES ||
            config->rm_config.pebc.on_instruction == NULL ||
            config->rm_config.pebc.on_request_param == NULL
        ) {
            ESP_LOGE(TAG, "s2_client_init: Invalid PEBC config");
            return NULL;
        }
    }

    // Allocate memory to store the S2 client class
    s2_client_handle_t client = calloc(1, sizeof(s2_client_t));
    if (!client) return NULL;

    // Copy config into the handle
    strlcpy(client->node.id, config->node.id, sizeof(client->node.id));
    strlcpy(client->node.brand, config->node.brand, sizeof(client->node.brand));
    strlcpy(client->node.type, config->node.type, sizeof(client->node.type));
    strlcpy(client->node.model, config->node.model, sizeof(client->node.model));
    client->node.role = config->node.role;
    client->node.deployment = config->node.deployment;

    for (int i = 0; i < S2_NODE_MAX_SUPPORTED_VERSIONS; i++) {
        if (config->node.supported_versions[i][0] != '\0' &&
            strnlen(config->node.supported_versions[i], sizeof(config->node.supported_versions[i])) < sizeof(config->node.supported_versions[i])) {
            strlcpy(client->node.supported_versions[i], config->node.supported_versions[i], sizeof(config->node.supported_versions[i]));
        }
    }

    // Optional fields
    if (config->node.logo_uri[0] != '\0') strlcpy(client->node.logo_uri, config->node.logo_uri, sizeof(config->node.logo_uri));
    if (config->node.user_defined_name[0] != '\0') strlcpy(client->node.user_defined_name, config->node.user_defined_name, sizeof(config->node.user_defined_name));

    // Copy RM config and apply defaults for optional interval fields
    client->rm = config->rm_config;
    if (client->rm.measurement_interval_ms == 0)  client->rm.measurement_interval_ms  = S2_DEFAULT_MEASUREMENT_INTERVAL_MS;
    if (client->rm.forecast_interval_ms == 0)     client->rm.forecast_interval_ms     = S2_DEFAULT_FORECAST_INTERVAL_MS;
    if (client->rm.instruction_processing_delay_ms == 0) client->rm.instruction_processing_delay_ms = S2_DEFAULT_INSTRUCTION_PROCESSING_DELAY_MS;
    if (client->rm.control_type == S2_CONTROL_TYPE_PEBC && client->rm.pebc.constraint_interval_ms == 0)
        client->rm.pebc.constraint_interval_ms = S2_DEFAULT_CONSTRAINT_INTERVAL_MS;

    // Copy configuration fields
    memcpy(client->tls_cert_fingerprint, config->tls_cert_fingerprint, S2_TLS_FINGERPRINT_SIZE);
    client->dns_sd_enabled = config->dns_sd_enabled;
    client->access_token_count = config->access_token_count > S2_MAX_ACCESS_TOKENS
        ? S2_MAX_ACCESS_TOKENS : config->access_token_count;
    for (int i = 0; i < client->access_token_count; i++) {
        strlcpy(client->access_tokens[i], config->access_tokens[i], S2_ACCESS_TOKEN_LEN);
    }

    // Load persisted tokens from NVS (survives reboots, overrides config)
    nvs_load_tokens(client);

    // Configure HTTP client used to communicate with S2 server API
    if (!s2_client_init_http(client, config)) {
        ESP_LOGE(TAG, "Failed to initialize HTTP client");
        return NULL;
    }
 
    // Initialize WebSocket state properties to NULL (will be set in WebSocket connect)
    client->websocket_state_mutex = NULL;
    client->websocket_conn_event_group = NULL;
    client->websocket_conn_state = S2_CLIENT_WEBSOCKET_CONN_STATE_DISCONNECTED;
    client->active_constraint_slot = -1;
    client->reconnecting = false;

    return client;
}

void s2_client_confirm_certificate_pin(s2_client_handle_t client_handle) {
    if (!client_handle) return;
    client_handle->tls_cert_pinned = true;
}
