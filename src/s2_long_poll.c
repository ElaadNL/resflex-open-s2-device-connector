// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

#include "s2_long_poll.h"
#include "s2_client_http.h"
#include "s2_client_internal.h"
#include "s2_common.h"

#include "cJSON.h"

#include <string.h>

/**
 * @brief Parse an action string to its enum value.
 *
 * @return Matching action, or S2_LP_ERROR if unknown.
 */
s2_long_poll_action_t parse_action(const char *s) {
    if (!s) return S2_LP_ERROR;
    if (strcmp(s, "sendNodeDescription") == 0)   return S2_LP_SEND_NODE_DESC;
    if (strcmp(s, "preparePairing") == 0)        return S2_LP_PREPARE_PAIRING;
    if (strcmp(s, "cancelPreparePairing") == 0)  return S2_LP_CANCEL_PREPARE;
    if (strcmp(s, "requestPairing") == 0)        return S2_LP_REQUEST_PAIRING;
    return S2_LP_ERROR;
}

/**
 * @brief Parse the waitForPairing response.
 *
 * Extracts the first action from the JSON array response into out.
 */
esp_err_t parse_long_poll_response(const s2_client_http_response_ctx_t *raw,
                                    void *out) {
    s2_long_poll_result_t *r = (s2_long_poll_result_t *)out;
    r->action = S2_LP_NO_ACTION;

    // 204 = empty body = no action
    if (raw->len == 0) return ESP_OK;

    cJSON *json = cJSON_ParseWithLength(raw->buffer, raw->len);
    if (!json || !cJSON_IsArray(json)) {
        cJSON_Delete(json);
        r->action = S2_LP_ERROR;
        return ESP_FAIL;
    }

    cJSON *item = cJSON_GetArrayItem(json, 0);
    if (cJSON_IsObject(item)) {
        cJSON *id = cJSON_GetObjectItem(item, "clientNodeId");
        cJSON *act = cJSON_GetObjectItem(item, "action");
        if (cJSON_IsString(id))
            strlcpy(r->client_node_id, id->valuestring, sizeof(r->client_node_id));
        if (cJSON_IsString(act))
            r->action = parse_action(act->valuestring);
        else
            r->action = S2_LP_ERROR;
    }

    cJSON_Delete(json);
    return ESP_OK;
}

/**
 * @brief Build the waitForPairing request body.
 *
 * Creates [{"clientNodeId": ..., optional fields}].
 */
static cJSON *s2_long_poll_build_body(s2_client_t *h,
                                      const char *client_node_id,
                                      const s2_long_poll_request_options_t *options) {
    cJSON *item = cJSON_CreateObject();
    if (!item) return NULL;
    cJSON_AddStringToObject(item, "clientNodeId", client_node_id);

    if (options && options->include_descriptions) {
        cJSON *node_desc = s2_node_to_json(h->node);
        if (node_desc) {
            cJSON_AddItemToObject(item, "clientNodeDescription", node_desc);
        }

        cJSON *endpoint = cJSON_CreateObject();
        if (endpoint) {
            cJSON_AddStringToObject(endpoint, "deployment",
                h->node.deployment == S2_DEPLOYMENT_LAN ? "LAN" : "WAN");
            cJSON_AddItemToObject(item, "clientEndpointDescription", endpoint);
        }
    }

    if (options && options->no_valid_token) {
        cJSON_AddStringToObject(item, "errorMessage", "NoValidTokenOnPairingClient");
    }

    cJSON *body = cJSON_CreateArray();
    if (!body) {
        cJSON_Delete(item);
        return NULL;
    }
    cJSON_AddItemToArray(body, item);
    return body;
}

esp_err_t s2_long_poll_wait_extended(s2_client_handle_t client_handle,
                               const char *client_node_id,
                               const s2_long_poll_request_options_t *options,
                               s2_long_poll_result_t *result) {
    s2_client_t *h = client_handle;
    if (!h || !client_node_id || !result) return ESP_ERR_INVALID_ARG;

    /* A reconnect needs the HTTP worker now. Waiting behind a long poll would push
     * the session requests out past the 30s life of the pending access token. */
    if (h->reconnecting) return ESP_ERR_INVALID_STATE;

    memset(result, 0, sizeof(*result));
    result->action = S2_LP_NO_ACTION;

    cJSON *body = s2_long_poll_build_body(h, client_node_id, options);
    if (!body) return ESP_FAIL;

    s2_request_t request = {
        .endpoint = "pairing/" S2_CONNECT_PAIRING_API_VERSION "/waitForPairing",
        .body = body,
        .parser = parse_long_poll_response,
        .parser_out = result,
        .timeout_ms = S2_LONG_POLL_TIMEOUT_MS,
        .done = xSemaphoreCreateBinary(),
        .result = ESP_FAIL
    };

    esp_err_t err = s2_client_http_add_request(h, &request);
    cJSON_Delete(body);
    return err;
}

esp_err_t s2_long_poll_wait(s2_client_handle_t client_handle,
                             const char *client_node_id,
                             s2_long_poll_result_t *result) {
    return s2_long_poll_wait_extended(client_handle, client_node_id, NULL, result);
}
