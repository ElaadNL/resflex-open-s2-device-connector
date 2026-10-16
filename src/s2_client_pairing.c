// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

#include "s2_client_pairing.h"
#include "s2_client_http.h"
#include "s2_common.h"
#include "s2_client_internal.h"
#include "s2_crypto.h"

#include "cJSON.h"
#include "esp_log.h"
#include <string.h>
#ifndef UNITY_INCLUDE_CONFIG_H
#include "esp_crt_bundle.h"
#include "esp_tls.h"
#include "mbedtls/ssl.h"
#include "mbedtls/x509_crt.h"
#endif

static const char *TAG = "s2_client_pairing";

#ifndef UNITY_INCLUDE_CONFIG_H
static s2_client_t *g_pinned_handle;

void s2_client_set_pinned_handle(s2_client_t *handle) {
    g_pinned_handle = handle;
}

/**
 * @brief TLS verify callback: TOFU before pairing, fingerprint check after.
 */
static int verify_callback(void *data, mbedtls_x509_crt *crt, int depth, uint32_t *flags) {
    (void)data;
    (void)depth;
    if (!g_pinned_handle || !g_pinned_handle->tls_cert_pinned) {
        *flags = 0;  /* TOFU */
        return 0;
    }
    /* check fingerprint matches pinned cert */
    if (crt && crt->raw.p && crt->raw.len > 0) {
        uint8_t fp[32];
        s2_certificate_fingerprint_sha256(crt->raw.p, crt->raw.len, fp);
        if (memcmp(fp, g_pinned_handle->tls_cert_fingerprint, 32) != 0) {
            *flags |= MBEDTLS_X509_BADCERT_NOT_TRUSTED;
        }
    }
    return 0;
}

esp_err_t s2_client_no_verify_attach(void *conf) {
    esp_crt_bundle_attach(conf);
    mbedtls_ssl_conf_verify((mbedtls_ssl_config *)conf, verify_callback, NULL);
    mbedtls_ssl_conf_authmode((mbedtls_ssl_config *)conf, MBEDTLS_SSL_VERIFY_OPTIONAL);
    return ESP_OK;
}
#endif

void s2_client_pin_server_certificate(s2_client_handle_t client_handle,
                               const uint8_t *der_cert, size_t cert_len) {
    s2_client_t *handle = client_handle;
    if (!handle || !der_cert || cert_len == 0) return;

    s2_certificate_fingerprint_sha256(der_cert, cert_len,
                               handle->tls_cert_fingerprint);
}

#ifndef UNITY_INCLUDE_CONFIG_H
/**
 * @brief Connect via TLS, extract peer cert DER, compute SHA-256 fingerprint.
 */
esp_err_t s2_client_fetch_server_certificate(s2_client_handle_t client_handle) {
    s2_client_t *handle = client_handle;
    if (!handle) return ESP_ERR_INVALID_ARG;

    // build https URL from base_url host:port
    const char *base = handle->server.base_url;
    const char *p = strstr(base, "://");
    if (!p) return ESP_FAIL;
    p += 3;
    const char *slash = strchr(p, '/');
    size_t host_len;
    if (slash) {
        host_len = (size_t)(slash - p);
    } else {
        if (!get_bounded_strlen(p, sizeof(handle->server.base_url) - (size_t)(p - base), &host_len))
            return ESP_FAIL;
    }
    char url[256];
    snprintf(url, sizeof(url), "https://%.*s", (int)host_len, p);

    esp_tls_cfg_t cfg = {
        .skip_common_name = true,
        .crt_bundle_attach = s2_client_no_verify_attach,
        .timeout_ms = 5000,
    };
    esp_tls_t *tls = esp_tls_init();
    if (!tls) { ESP_LOGE(TAG, "fetch_cert: init failed"); return ESP_FAIL; }

    int ret = esp_tls_conn_http_new_sync(url, &cfg, tls);
    if (ret != 1) {
        ESP_LOGE(TAG, "fetch_cert: connect to %s failed (ret=%d)", url, ret);
        esp_tls_conn_destroy(tls);
        return ESP_FAIL;
    }

    void *ssl_ctx = esp_tls_get_ssl_context(tls);
    if (!ssl_ctx) {
        ESP_LOGE(TAG, "fetch_cert: no ssl context");
        esp_tls_conn_destroy(tls);
        return ESP_FAIL;
    }

    const mbedtls_x509_crt *cert = mbedtls_ssl_get_peer_cert(ssl_ctx);
    if (!cert || !cert->raw.p || cert->raw.len == 0) {
        ESP_LOGE(TAG, "fetch_cert: no peer cert");
        esp_tls_conn_destroy(tls);
        return ESP_FAIL;
    }

    s2_client_pin_server_certificate(client_handle, cert->raw.p, cert->raw.len);
    ESP_LOGI(TAG, "fetch_cert: pinned fingerprint");
    esp_tls_conn_destroy(tls);
    return ESP_OK;
}
#endif

cJSON *s2_client_build_prepare_pairing_body(s2_client_t *handle,
                                             const char *server_node_id) {
    cJSON *root = cJSON_CreateObject();
    if (!root) return NULL;

    cJSON *node_desc = s2_node_to_json(handle->node);
    if (!node_desc) {
        cJSON_Delete(root);
        return NULL;
    }

    if (!cJSON_AddItemToObject(root, "clientNodeDescription", node_desc)) {
        cJSON_Delete(root);
        cJSON_Delete(node_desc);
        return NULL;
    }

    cJSON *endpoint = cJSON_CreateObject();
    if (!endpoint) { cJSON_Delete(root); return NULL; }
    cJSON_AddStringToObject(endpoint, "deployment",
        handle->node.deployment == S2_DEPLOYMENT_LAN ? "LAN" : "WAN");
    cJSON_AddItemToObject(root, "clientEndpointDescription", endpoint);

    if (server_node_id && server_node_id[0] != '\0') {
        cJSON_AddStringToObject(root, "serverNodeId", server_node_id);
    }

    return root;
}

esp_err_t s2_client_prepare_pairing(s2_client_handle_t client_handle,
                                     const char *server_node_id) {
    s2_client_t *handle = client_handle;
    if (!handle) return ESP_ERR_INVALID_ARG;

    cJSON *body = s2_client_build_prepare_pairing_body(handle, server_node_id);
    if (!body) {
        ESP_LOGE(TAG, "prepare_pairing: Failed to build body");
        return ESP_FAIL;
    }

    s2_request_t request = {
        .endpoint = "pairing/" S2_CONNECT_PAIRING_API_VERSION "/preparePairing",
        .body = body,
        .done = xSemaphoreCreateBinary(),
        .result = ESP_FAIL
    };

    esp_err_t err = s2_client_http_add_request(handle, &request);
    cJSON_Delete(body);
    return err;
}

esp_err_t s2_client_cancel_prepare_pairing(s2_client_handle_t client_handle,
                                            const char *server_node_id) {
    s2_client_t *handle = client_handle;
    if (!handle) return ESP_ERR_INVALID_ARG;

    cJSON *root = cJSON_CreateObject();
    if (!root) {
        ESP_LOGE(TAG, "cancel_prepare_pairing: Failed to create JSON");
        return ESP_FAIL;
    }
    cJSON_AddStringToObject(root, "clientNodeId", handle->node.id);
    if (server_node_id && server_node_id[0] != '\0') {
        cJSON_AddStringToObject(root, "serverNodeId", server_node_id);
    }

    s2_request_t request = {
        .endpoint = "pairing/" S2_CONNECT_PAIRING_API_VERSION "/cancelPreparePairing",
        .body = root,
        .done = xSemaphoreCreateBinary(),
        .result = ESP_FAIL
    };

    esp_err_t err = s2_client_http_add_request(handle, &request);
    cJSON_Delete(root);
    return err;
}

cJSON *s2_client_build_request_pairing_body(s2_client_t *handle,
                                             const char *client_challenge_b64,
                                             bool is_lan,
                                             bool force_pairing) {
    cJSON *root = cJSON_CreateObject();
    if (!root) return NULL;

    // clientNodeDescription
    cJSON *node_desc = s2_node_to_json(handle->node);
    if (!node_desc) {
        cJSON_Delete(root);
        return NULL;
    }
    cJSON_AddItemToObject(root, "clientNodeDescription", node_desc);

    // clientEndpointDescription
    cJSON *endpoint = cJSON_CreateObject();
    if (!endpoint) { cJSON_Delete(root); return NULL; }
    cJSON_AddStringToObject(endpoint, "deployment", is_lan ? "LAN" : "WAN");
    cJSON_AddItemToObject(root, "clientEndpointDescription", endpoint);

    cJSON *protocols = cJSON_CreateArray();
    cJSON_AddItemToArray(protocols, cJSON_CreateString("WebSocket"));
    cJSON_AddItemToObject(root, "supportedCommunicationProtocols", protocols);

    cJSON *versions = cJSON_CreateArray();
    for (int i = 0; i < S2_NODE_MAX_SUPPORTED_VERSIONS; i++) {
        if (handle->node.supported_versions[i][0] != '\0') {
            cJSON_AddItemToArray(versions, cJSON_CreateString(handle->node.supported_versions[i]));
        }
    }
    cJSON_AddItemToObject(root, "supportedS2MessageVersions", versions);

    cJSON *algos = cJSON_CreateArray();
    cJSON_AddItemToArray(algos, cJSON_CreateString("SHA256"));
    cJSON_AddItemToObject(root, "supportedHmacHashingAlgorithms", algos);

    cJSON_AddStringToObject(root, "clientHmacChallenge", client_challenge_b64);

    return root;
}

esp_err_t s2_client_parse_request_pairing_response(const s2_client_http_response_ctx_t *raw,
                                                    void *out) {
    s2_client_request_pairing_response_t *parsed =
        (s2_client_request_pairing_response_t *)out;
    cJSON *json = cJSON_ParseWithLength(raw->buffer, raw->len);
    if (!json) {
        ESP_LOGE(TAG, "request_pairing: Failed to parse JSON response");
        return ESP_FAIL;
    }

    // pairingAttemptId
    cJSON *attempt_id = cJSON_GetObjectItem(json, "pairingAttemptId");
    if (!cJSON_IsString(attempt_id)) {
        ESP_LOGE(TAG, "request_pairing: Missing pairingAttemptId");
        cJSON_Delete(json);
        return ESP_FAIL;
    }
    strlcpy(parsed->pairing_attempt_id, attempt_id->valuestring,
            sizeof(parsed->pairing_attempt_id));

    // serverNodeDescription
    cJSON *server_node = cJSON_GetObjectItem(json, "serverNodeDescription");
    if (!cJSON_IsObject(server_node)) {
        ESP_LOGE(TAG, "request_pairing: Missing serverNodeDescription");
        cJSON_Delete(json);
        return ESP_FAIL;
    }
    // parse server node fields into s2_node_info_t
    cJSON *id = cJSON_GetObjectItem(server_node, "id");
    cJSON *brand = cJSON_GetObjectItem(server_node, "brand");
    cJSON *type = cJSON_GetObjectItem(server_node, "type");
    cJSON *model = cJSON_GetObjectItem(server_node, "modelName");
    cJSON *name = cJSON_GetObjectItem(server_node, "userDefinedName");
    cJSON *role = cJSON_GetObjectItem(server_node, "role");
    if (!cJSON_IsString(id) || !cJSON_IsString(brand) ||
        !cJSON_IsString(type) || !cJSON_IsString(model) ||
        !cJSON_IsString(role)) {
        ESP_LOGE(TAG, "request_pairing: Invalid serverNodeDescription fields");
        cJSON_Delete(json);
        return ESP_FAIL;
    }
    strlcpy(parsed->server_node.id, id->valuestring, sizeof(parsed->server_node.id));
    strlcpy(parsed->server_node.brand, brand->valuestring, sizeof(parsed->server_node.brand));
    strlcpy(parsed->server_node.type, type->valuestring, sizeof(parsed->server_node.type));
    strlcpy(parsed->server_node.model, model->valuestring, sizeof(parsed->server_node.model));
    if (cJSON_IsString(name))
        strlcpy(parsed->server_node.user_defined_name, name->valuestring,
                sizeof(parsed->server_node.user_defined_name));
    if (!s2_role_to_enum(role->valuestring, &parsed->server_node.role)) {
        ESP_LOGE(TAG, "request_pairing: Unknown S2 role: %s", role->valuestring);
        cJSON_Delete(json);
        return ESP_FAIL;
    }

    cJSON *client_resp = cJSON_GetObjectItem(json, "clientHmacChallengeResponse");
    if (cJSON_IsString(client_resp))
        strlcpy(parsed->client_hmac_challenge_response, client_resp->valuestring,
                sizeof(parsed->client_hmac_challenge_response));

    cJSON *server_chal = cJSON_GetObjectItem(json, "serverHmacChallenge");
    if (cJSON_IsString(server_chal))
        strlcpy(parsed->server_hmac_challenge, server_chal->valuestring,
                sizeof(parsed->server_hmac_challenge));

    cJSON_Delete(json);
    return ESP_OK;
}

esp_err_t s2_client_request_pairing(s2_client_handle_t client_handle,
                                     const char *client_challenge_b64,
                                     bool is_lan,
                                     bool force_pairing,
                                     s2_client_request_pairing_response_t *response) {
    s2_client_t *handle = client_handle;
    if (!handle || !client_challenge_b64 || !response) return ESP_ERR_INVALID_ARG;

    // clear the output first: a failed request must not leave stale data behind
    memset(response, 0, sizeof(*response));

    cJSON *body = s2_client_build_request_pairing_body(handle,
                                                        client_challenge_b64,
                                                        is_lan,
                                                        force_pairing);
    if (!body) {
        ESP_LOGE(TAG, "request_pairing: Failed to build body");
        return ESP_FAIL;
    }

    s2_request_t request = {
        .endpoint = "pairing/" S2_CONNECT_PAIRING_API_VERSION "/requestPairing",
        .body = body,
        .parser = s2_client_parse_request_pairing_response,
        .parser_out = response,
        .done = xSemaphoreCreateBinary(),
        .result = ESP_FAIL
    };

    esp_err_t err = s2_client_http_add_request(handle, &request);
    cJSON_Delete(body);

    // remember which CEM this node is paired to, re-initiating on reconnect needs it
    if (err == ESP_OK && response->server_node.id[0] != '\0') {
        strlcpy(handle->server_node_id, response->server_node.id, sizeof(handle->server_node_id));
    }
    return err;
}

cJSON *s2_client_build_connection_details_body(s2_client_t *handle,
                                                const char *server_challenge_response_b64) {
    (void)handle;
    cJSON *root = cJSON_CreateObject();
    if (!root) return NULL;

    // pairingAttemptId is sent as Authorization: Bearer header, not in body
    if (!cJSON_AddStringToObject(root, "serverHmacChallengeResponse",
                                  server_challenge_response_b64)) {
        cJSON_Delete(root);
        return NULL;
    }

    return root;
}

esp_err_t s2_client_parse_connection_details_response(const s2_client_http_response_ctx_t *raw,
                                                       void *out) {
    s2_client_connection_details_response_t *parsed =
        (s2_client_connection_details_response_t *)out;
    cJSON *json = cJSON_ParseWithLength(raw->buffer, raw->len);
    if (!json) {
        ESP_LOGE(TAG, "connection_details: Failed to parse JSON");
        return ESP_FAIL;
    }

    cJSON *url = cJSON_GetObjectItem(json, "initiateSessionUrl");
    cJSON *token = cJSON_GetObjectItem(json, "accessToken");
    if (!cJSON_IsString(url) || !cJSON_IsString(token)) {
        ESP_LOGE(TAG, "connection_details: Missing initiateSessionUrl or accessToken");
        cJSON_Delete(json);
        return ESP_FAIL;
    }

    strlcpy(parsed->initiate_session_url, url->valuestring,
            sizeof(parsed->initiate_session_url));
    strlcpy(parsed->access_token, token->valuestring,
            sizeof(parsed->access_token));

    cJSON_Delete(json);
    return ESP_OK;
}

esp_err_t s2_client_request_connection_details(s2_client_handle_t client_handle,
                                                const char *pairing_attempt_id,
                                                const char *server_challenge_response_b64,
                                                s2_client_connection_details_response_t *response) {
    s2_client_t *handle = client_handle;
    if (!handle || !pairing_attempt_id || !server_challenge_response_b64 || !response)
        return ESP_ERR_INVALID_ARG;

    // clear the output first: a failed request must not leave stale data behind
    memset(response, 0, sizeof(*response));

    cJSON *body = s2_client_build_connection_details_body(handle,
                                                           server_challenge_response_b64);
    if (!body) {
        ESP_LOGE(TAG, "connection_details: Failed to build body");
        return ESP_FAIL;
    }

    // pairingAttemptId as Bearer token
    s2_request_t request = {
        .endpoint = "pairing/" S2_CONNECT_PAIRING_API_VERSION "/requestConnectionDetails",
        .body = body,
        .access_token = pairing_attempt_id,
        .parser = s2_client_parse_connection_details_response,
        .parser_out = response,
        .done = xSemaphoreCreateBinary(),
        .result = ESP_FAIL
    };

    esp_err_t err = s2_client_http_add_request(handle, &request);
    cJSON_Delete(body);

    // persist the new access token so it survives reboots
    if (err == ESP_OK) {
        s2_client_add_access_token(handle, response->access_token);
    }

    return err;
}

cJSON *s2_client_build_finalize_pairing_body(s2_client_t *handle, bool success) {
    (void)handle;
    cJSON *root = cJSON_CreateObject();
    if (!root) return NULL;
    cJSON_AddBoolToObject(root, "success", success);
    return root;
}

esp_err_t s2_client_finalize_pairing(s2_client_handle_t client_handle,
                                      const char *pairing_attempt_id,
                                      bool success) {
    s2_client_t *handle = client_handle;
    if (!handle || !pairing_attempt_id) return ESP_ERR_INVALID_ARG;

    cJSON *body = s2_client_build_finalize_pairing_body(handle, success);
    if (!body) {
        ESP_LOGE(TAG, "finalize_pairing: Failed to build body");
        return ESP_FAIL;
    }

    // pairingAttemptId as Bearer token
    s2_request_t request = {
        .endpoint = "pairing/" S2_CONNECT_PAIRING_API_VERSION "/finalizePairing",
        .body = body,
        .access_token = pairing_attempt_id,
        .done = xSemaphoreCreateBinary(),
        .result = ESP_FAIL
    };

    esp_err_t err = s2_client_http_add_request(handle, &request);
    cJSON_Delete(body);
    return err;
}

esp_err_t s2_client_confirm_access_token(s2_client_handle_t client_handle,
                                          const char *access_token) {
    s2_client_t *handle = client_handle;
    if (!handle || !access_token) return ESP_ERR_INVALID_ARG;

    // empty body. token goes in Authorization header only
    cJSON *body = cJSON_CreateObject();
    if (!body) return ESP_FAIL;

    s2_request_t request = {
        .endpoint = "connection/" S2_CONNECT_PAIRING_API_VERSION "/confirmAccessToken",
        .body = body,
        .access_token = access_token,
        .done = xSemaphoreCreateBinary(),
        .result = ESP_FAIL
    };

    esp_err_t err = s2_client_http_add_request(handle, &request);
    cJSON_Delete(body);

    // on success, prune old tokens. server activated this one
    if (err == ESP_OK) {
        s2_client_prune_access_tokens(handle);
    }

    return err;
}

/* ---- S2 Connect session initiation ---- */

static esp_err_t s2_client_parse_initiate_session_response(const s2_client_http_response_ctx_t *raw,
                                                            void *out) {
    s2_client_initiate_session_response_t *parsed = (s2_client_initiate_session_response_t *)out;
    cJSON *json = cJSON_ParseWithLength(raw->buffer, raw->len);
    if (!json) return ESP_FAIL;

    cJSON *proto = cJSON_GetObjectItem(json, "selectedCommunicationProtocol");
    cJSON *ver = cJSON_GetObjectItem(json, "selectedS2MessageVersion");
    cJSON *tok = cJSON_GetObjectItem(json, "accessToken");

    if (!cJSON_IsString(tok)) { cJSON_Delete(json); return ESP_FAIL; }

    if (!cJSON_IsString(proto) || strcmp(proto->valuestring, "WebSocket") != 0) {
        ESP_LOGE(TAG, "initiate_session: server selected unsupported protocol '%s'",
                 cJSON_IsString(proto) ? proto->valuestring : "(null)");
        cJSON_Delete(json);
        return ESP_FAIL;
    }

    if (cJSON_IsString(ver)) {
        strlcpy(parsed->selected_s2_version, ver->valuestring,
                sizeof(parsed->selected_s2_version));
    }

    strlcpy(parsed->access_token, tok->valuestring, sizeof(parsed->access_token));
    cJSON_Delete(json);
    return ESP_OK;
}

esp_err_t s2_client_initiate_session(s2_client_handle_t client_handle,
                                      const char *server_node_id,
                                      const char *access_token,
                                      s2_client_initiate_session_response_t *response) {
    s2_client_t *handle = client_handle;
    if (!handle || !server_node_id || !access_token || !response) return ESP_ERR_INVALID_ARG;

    // clear the output first: a failed request must not leave stale data behind
    memset(response, 0, sizeof(*response));

    cJSON *body = cJSON_CreateObject();
    if (!body) return ESP_FAIL;
    cJSON_AddStringToObject(body, "clientNodeId", handle->node.id);
    cJSON_AddStringToObject(body, "serverNodeId", server_node_id);
    cJSON *vers = cJSON_CreateArray();
    cJSON_AddItemToArray(vers, cJSON_CreateString("0.0.2-beta"));
    cJSON_AddItemToObject(body, "supportedS2MessageVersions", vers);
    cJSON *protos = cJSON_CreateArray();
    cJSON_AddItemToArray(protos, cJSON_CreateString("WebSocket"));
    cJSON_AddItemToObject(body, "supportedCommunicationProtocols", protos);

    s2_request_t request = {
        .endpoint = "connection/" S2_CONNECT_PAIRING_API_VERSION "/initiateSession",
        .body = body,
        .access_token = access_token,
        .parser = s2_client_parse_initiate_session_response,
        .parser_out = response,
        .done = xSemaphoreCreateBinary(),
        .result = ESP_FAIL
    };
    esp_err_t err = s2_client_http_add_request(handle, &request);
    cJSON_Delete(body);

    // store it before confirming, the CEM activates it even if the confirm response gets lost
    if (err == ESP_OK && response->access_token[0] != '\0') {
        s2_client_add_access_token(handle, response->access_token);
    }
    return err;
}

esp_err_t s2_client_parse_confirm_access_token_response(const s2_client_http_response_ctx_t *raw,
                                                                void *out) {
    s2_client_confirm_access_token_response_t *parsed = (s2_client_confirm_access_token_response_t *)out;
    cJSON *json = cJSON_ParseWithLength(raw->buffer, raw->len);
    if (!json) return ESP_FAIL;

    cJSON *proto = cJSON_GetObjectItem(json, "communicationProtocol");
    if (!cJSON_IsString(proto) || strcmp(proto->valuestring, "WebSocket") != 0) {
        ESP_LOGE(TAG, "confirm_access_token: unsupported protocol '%s'",
                 cJSON_IsString(proto) ? proto->valuestring : "(null)");
        cJSON_Delete(json);
        return ESP_FAIL;
    }

    cJSON *tk = cJSON_GetObjectItem(json, "websocketToken");
    cJSON *wu = cJSON_GetObjectItem(json, "websocketUrl");
    if (!cJSON_IsString(tk) || !cJSON_IsString(wu)) { cJSON_Delete(json); return ESP_FAIL; }
    strlcpy(parsed->websocket_token, tk->valuestring, sizeof(parsed->websocket_token));
    strlcpy(parsed->websocket_url, wu->valuestring, sizeof(parsed->websocket_url));
    cJSON_Delete(json);
    return ESP_OK;
}

esp_err_t s2_client_confirm_access_token_extended(s2_client_handle_t client_handle,
                                              const char *access_token,
                                              s2_client_confirm_access_token_response_t *response) {
    s2_client_t *handle = client_handle;
    if (!handle || !access_token || !response) return ESP_ERR_INVALID_ARG;

    // clear the output first: a failed request must not leave stale data behind
    memset(response, 0, sizeof(*response));

    cJSON *body = cJSON_CreateObject();
    if (!body) return ESP_FAIL;

    s2_request_t request = {
        .endpoint = "connection/" S2_CONNECT_PAIRING_API_VERSION "/confirmAccessToken",
        .body = body,
        .access_token = access_token,
        .parser = s2_client_parse_confirm_access_token_response,
        .parser_out = response,
        .done = xSemaphoreCreateBinary(),
        .result = ESP_FAIL
    };
    esp_err_t err = s2_client_http_add_request(handle, &request);
    cJSON_Delete(body);

    // the CEM activated the token stored at initiateSession, keep only that one
    if (err == ESP_OK) s2_client_prune_access_tokens(handle);
    return err;
}

cJSON *s2_client_build_unpair_body(s2_client_t *handle, const char *server_node_id) {
    if (!handle || !server_node_id || server_node_id[0] == '\0') return NULL;

    cJSON *root = cJSON_CreateObject();
    if (!root) return NULL;

    if (!cJSON_AddStringToObject(root, "clientNodeId", handle->node.id)) {
        cJSON_Delete(root);
        return NULL;
    }
    if (!cJSON_AddStringToObject(root, "serverNodeId", server_node_id)) {
        cJSON_Delete(root);
        return NULL;
    }

    return root;
}

esp_err_t s2_client_unpair(s2_client_handle_t client_handle,
                            const char *access_token,
                            const char *server_node_id) {
    s2_client_t *handle = client_handle;
    if (!handle || !access_token || !server_node_id || server_node_id[0] == '\0')
        return ESP_ERR_INVALID_ARG;

    cJSON *body = s2_client_build_unpair_body(handle, server_node_id);
    if (!body) {
        ESP_LOGE(TAG, "unpair: Failed to build body");
        return ESP_FAIL;
    }

    s2_request_t request = {
        .endpoint = "connection/" S2_CONNECT_PAIRING_API_VERSION "/unpair",
        .body = body,
        .access_token = access_token,
        .done = xSemaphoreCreateBinary(),
        .result = ESP_FAIL
    };

    esp_err_t err = s2_client_http_add_request(handle, &request);
    cJSON_Delete(body);

    // wipe local tokens and fingerprint on success
    if (err == ESP_OK) {
        memset(handle->access_tokens, 0, sizeof(handle->access_tokens));
        handle->access_token_count = 0;
        handle->last_access_token[0] = '\0';
        handle->session_url[0] = '\0';
        memset(handle->tls_cert_fingerprint, 0, S2_TLS_FINGERPRINT_SIZE);
        s2_client_prune_access_tokens(handle); // saves empty state to NVS
    }

    return err;
}

#ifndef UNITY_INCLUDE_CONFIG_H
/* ---- Version negotiation ---- */

/**
 * @brief Verify the GET /pairing/ response contains a supported version.
 */
static esp_err_t verify_pairing_version(const char *buf, int len) {
    if (len <= 0) {
        ESP_LOGE(TAG, "version negotiation: empty response");
        return ESP_FAIL;
    }

    cJSON *json = cJSON_ParseWithLength(buf, (size_t)len);
    if (!json || !cJSON_IsArray(json)) {
        cJSON_Delete(json);
        ESP_LOGE(TAG, "version negotiation: invalid JSON response");
        return ESP_FAIL;
    }

    bool found = false;
    for (int i = 0; i < cJSON_GetArraySize(json); i++) {
        cJSON *v = cJSON_GetArrayItem(json, i);
        if (cJSON_IsString(v) && strcmp(v->valuestring, S2_CONNECT_PAIRING_API_VERSION) == 0) {
            found = true;
            break;
        }
    }
    cJSON_Delete(json);

    if (!found) {
        ESP_LOGE(TAG, "version negotiation: " S2_CONNECT_PAIRING_API_VERSION " not supported by server");
        return ESP_FAIL;
    }
    return ESP_OK;
}

/**
 * @brief Perform a GET request to path (relative to base_url).
 *
 * Returns ESP_OK and fills buf/len_out/status_out on success, even for a
 * non-200 status.
 */
static esp_err_t s2_client_http_get(s2_client_t *handle, const char *path,
                                    int timeout_ms, char *buf, size_t buf_size,
                                    int *len_out, int *status_out) {
    if (!handle || !path || !buf || !len_out || !status_out) return ESP_ERR_INVALID_ARG;

    char url[256];
    snprintf(url, sizeof(url), "%s/%s", handle->server.base_url, path);

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = timeout_ms,
        .skip_cert_common_name_check = handle->server.skip_cert_cn_check,
        .crt_bundle_attach = s2_client_no_verify_attach,
    };
    if (handle->server.cert_pem && handle->server.cert_pem_len > 0) {
        config.cert_pem = handle->server.cert_pem;
        config.cert_len = handle->server.cert_pem_len;
    }

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return ESP_FAIL;

    esp_http_client_set_header(client, "Host", handle->server.host);
    // Drive the request manually instead of using esp_http_client_perform(): we want the
    // raw body and status code without an event handler, and we use a dedicated client
    // here because the shared client belongs to the http worker task
    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "http get %s: open failed 0x%x", url, err);
        esp_http_client_cleanup(client);
        return err;
    }
    if (esp_http_client_fetch_headers(client) < 0) {
        ESP_LOGE(TAG, "http get %s: no response", url);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }

    *status_out = esp_http_client_get_status_code(client);
    int len = esp_http_client_read_response(client, buf, (int)buf_size - 1);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    if (len < 0) len = 0;
    buf[len] = '\0';
    *len_out = len;
    return ESP_OK;
}

esp_err_t s2_client_verify_pairing_version(s2_client_t *handle) {
    if (!handle) return ESP_ERR_INVALID_ARG;

    char buf[256] = {0};
    int len = 0;
    int status = 0;
    esp_err_t err = s2_client_http_get(handle, "pairing/", 10000,
                                       buf, sizeof(buf), &len, &status);
    if (err != ESP_OK) return ESP_FAIL;  // http_get already logged why

    if (status != 200) {
        ESP_LOGE(TAG, "version negotiation: HTTP %d", status);
        return ESP_FAIL;
    }

    return verify_pairing_version(buf, len);
}

/**
 * @brief GET /pairing/v1/nodes and return the first node's id.
 */
esp_err_t s2_client_fetch_server_node_id(s2_client_t *handle,
                                          char *node_id_out, size_t node_id_size) {
    if (!handle || !node_id_out || node_id_size == 0) return ESP_ERR_INVALID_ARG;
    node_id_out[0] = '\0';

    char buf[512] = {0};
    int len = 0;
    int status = 0;
    esp_err_t err = s2_client_http_get(handle,
                                       "pairing/" S2_CONNECT_PAIRING_API_VERSION "/nodes",
                                       10000, buf, sizeof(buf), &len, &status);
    if (err != ESP_OK) return ESP_FAIL;

    if (status != 200) {
        ESP_LOGW(TAG, "fetch_nodes: HTTP %d", status);
        return ESP_FAIL;
    }

    if (len <= 0) return ESP_FAIL;

    cJSON *json = cJSON_ParseWithLength(buf, (size_t)len);
    if (!json || !cJSON_IsArray(json)) {
        cJSON_Delete(json);
        return ESP_FAIL;
    }

    cJSON *first = cJSON_GetArrayItem(json, 0);
    if (!first) { cJSON_Delete(json); return ESP_FAIL; }

    cJSON *id = cJSON_GetObjectItem(first, "id");
    if (!cJSON_IsString(id)) { cJSON_Delete(json); return ESP_FAIL; }

    strlcpy(node_id_out, id->valuestring, node_id_size);
    cJSON_Delete(json);
    return ESP_OK;
}
#endif
