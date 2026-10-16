// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "s2_client.h"
#include "s2_client_http.h"

/* Expose internal helpers for unit testing */

/**
 * @brief Build the preparePairing request body.
 */
cJSON *s2_client_build_prepare_pairing_body(s2_client_t *handle,
                                             const char *server_node_id);

/**
 * @brief Build the requestPairing request body.
 */
cJSON *s2_client_build_request_pairing_body(s2_client_t *handle,
                                             const char *client_challenge_b64,
                                             bool is_lan,
                                             bool force_pairing);

/**
 * @brief Parse the requestPairing response.
 */
esp_err_t s2_client_parse_request_pairing_response(const s2_client_http_response_ctx_t *raw,
                                                    void *out);

/**
 * @brief Build the requestConnectionDetails request body.
 */
cJSON *s2_client_build_connection_details_body(s2_client_t *handle,
                                                const char *server_challenge_response_b64);

/**
 * @brief Parse the requestConnectionDetails response.
 */
esp_err_t s2_client_parse_connection_details_response(const s2_client_http_response_ctx_t *raw,
                                                       void *out);

/**
 * @brief Build the finalizePairing request body.
 */
cJSON *s2_client_build_finalize_pairing_body(s2_client_t *handle, bool success);

/**
 * @brief Build the unpair request body.
 */
cJSON *s2_client_build_unpair_body(s2_client_t *handle, const char *server_node_id);

/**
 * @brief Parse the confirmAccessToken response.
 */
esp_err_t s2_client_parse_confirm_access_token_response(const s2_client_http_response_ctx_t *raw,
                                                         void *out);

/**
 * @brief Verify the server supports our pairing version (GET /pairing/).
 */
esp_err_t s2_client_verify_pairing_version(s2_client_t *handle);

/**
 * @brief Fetch the server's node ID (GET /pairing/v1/nodes).
 */
esp_err_t s2_client_fetch_server_node_id(s2_client_t *handle,
                                          char *node_id_out, size_t node_id_size);

/**
 * @brief Fetch server TLS cert at runtime and pin its fingerprint.
 */
esp_err_t s2_client_fetch_server_certificate(s2_client_handle_t client_handle);

/**
 * @brief No-verify TLS attach for TOFU; checks pinned fingerprint after pairing.
 */
esp_err_t s2_client_no_verify_attach(void *conf);

/**
 * @brief Set the handle used by the verify callback.
 */
void s2_client_set_pinned_handle(s2_client_t *handle);