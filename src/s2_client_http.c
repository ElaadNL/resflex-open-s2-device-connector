// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

#include "s2_client_http.h"

#include <esp_http_client.h>
#include "http_parser.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "s2_client_http";

/**
 * @brief   Builds a full URL by concatenating server base URL with given path
 * 
 *          NOTE: Caller should free url
 *
 * @param   handle  S2 client handle obtained from initialization
 * @param   path    Path to place after the base URL (without /)
 *
 * @return Concatenated URL
 */
static char *s2_client_http_build_url(s2_client_t *handle, const char *path) {
    if (!handle || !path) return NULL;

    size_t base_len = 0;
    size_t path_len = 0;
    if (!get_bounded_strlen(handle->server.base_url, sizeof(handle->server.base_url), &base_len)) return NULL;
    if (!get_bounded_strlen(path, 256, &path_len)) return NULL;

    // Calculate required buffer size (base + / + path + null terminator)
    size_t len = base_len + 1 + path_len + 1;

    // Allocate memory for the URL
    char *url = malloc(len);
    if (!url) return NULL;

    // Construct full URL and return it
    snprintf(url, len, "%s/%s", handle->server.base_url, path);
    return url;
}

esp_err_t s2_client_http_event_handler(esp_http_client_event_t *event) {
    // "user_data" is a pointer to a custom memory block (context) provided by the
    // "s2_client_http_perform_request" function.
    // Because the event handler's signature is fixed by ESP-IDF, we use this pointer 
    // to bridge the gap between the callback and our application logic, allowing 
    // us to extract the response into a local buffer or struct.
    void *user_data = NULL;
    esp_http_client_get_user_data(event->client, &user_data);
    s2_client_http_response_ctx_t *ctx = (s2_client_http_response_ctx_t *)user_data;

    // If no context is defined, return immediately
    if (ctx == NULL) return ESP_OK;

    switch (event->event_id) {
        case HTTP_EVENT_ON_DATA:
            // Copy event data into response
            if (ctx->len + event->data_len < sizeof(ctx->buffer) - 1) {
                memcpy(ctx->buffer + ctx->len, event->data, event->data_len);
                ctx->len += event->data_len;
                ctx->buffer[ctx->len] = '\0';
            }
            else {
                ESP_LOGE(TAG, "Response buffer overflow!");
            }
            break;
        case HTTP_EVENT_ON_FINISH:
            ctx->is_complete = true;
            break;
        case HTTP_EVENT_DISCONNECTED:
        case HTTP_EVENT_ERROR:
            ctx->is_complete = false;
            break;
        default:
            break;
    }

    return ESP_OK;
}

/**
 * @brief Point the shared client at this request: URL, headers and body.
 *
 * @return ESP_OK on success, ESP_FAIL when one of the calls fails.
 */
static esp_err_t s2_client_http_configure_request(s2_client_t *handle, s2_request_t *req,
                                                  const char *url, const char *req_body,
                                                  size_t req_body_len,
                                                  s2_client_http_response_ctx_t *ctx) {
    if (
        esp_http_client_set_user_data(handle->http_client, ctx) != ESP_OK ||
        esp_http_client_set_url(handle->http_client, url) != ESP_OK ||
        esp_http_client_set_method(handle->http_client, HTTP_METHOD_POST) != ESP_OK ||
        esp_http_client_set_header(handle->http_client, "Content-Type", "application/json") != ESP_OK ||
        esp_http_client_set_header(handle->http_client, "Host", handle->server.host) != ESP_OK ||
        esp_http_client_set_post_field(handle->http_client, req_body, req_body_len) != ESP_OK
    ) return ESP_FAIL;

    // Not every request needs auth
    // Example: preparePairing, cancelPreparePairing and requestPairing can run before a token exists.
    // We add access_token header only if we have one available.
    if (req->access_token == NULL) return ESP_OK;

    char auth_header[140];
    snprintf(auth_header, sizeof(auth_header), "Bearer %s", req->access_token);
    if (esp_http_client_set_header(handle->http_client, "Authorization", auth_header) != ESP_OK) return ESP_FAIL;
    return ESP_OK;
}

esp_err_t s2_client_http_perform_request(s2_client_t *handle, s2_request_t *req) {
    char *url = NULL;
    char *req_body = NULL;
    esp_err_t err = ESP_FAIL;
    
    // Validate function parameters
    if (handle == NULL || req == NULL || req->endpoint == NULL || req->body == NULL) return ESP_ERR_INVALID_ARG;

    // Convert JSON body to string for request
    req_body = cJSON_PrintUnformatted(req->body);
    if (!req_body) goto cleanup;

    // Build URL based on S2 server base URL
    url = s2_client_http_build_url(handle, req->endpoint);
    if (!url) goto cleanup;

    ESP_LOGI(TAG, "Request URL: %s", url);
    ESP_LOGD(TAG, "Request body: %s", req_body);

    // Clear all previous headers
    if (esp_http_client_delete_all_headers(handle->http_client) != ESP_OK) goto cleanup;

    // Configure user data to either catch the error message or the response if available
    s2_client_http_response_ctx_t ctx = {0};
    size_t req_body_len = 0;
    if (!get_bounded_strlen(req_body, 16384, &req_body_len)) goto cleanup;

    // Configure request
    if (s2_client_http_configure_request(handle, req, url, req_body, req_body_len, &ctx) != ESP_OK) goto cleanup;

    // Perform the request
    // Long poll needs a longer timeout, so apply it just for this call
    if (req->timeout_ms) esp_http_client_set_timeout_ms(handle->http_client, req->timeout_ms);
    err = esp_http_client_perform(handle->http_client);
    // Restore the default, the client is reused for the next request
    if (req->timeout_ms) esp_http_client_set_timeout_ms(handle->http_client, S2_HTTP_DEFAULT_TIMEOUT_MS);
    if (err != ESP_OK) {
        // a failed request can leave the client "connected" on a dead socket, so close it
        esp_http_client_close(handle->http_client);
        goto cleanup;
    }

    // Verify status code
    int status = esp_http_client_get_status_code(handle->http_client);

    if (status >= 300) {
        // Print error response to give client context on what went wrong
        if (status != 500) {
            ESP_LOGE(TAG, "Error response: %s", ctx.buffer);
        }

        err = ESP_FAIL;
        goto cleanup;
    }
    // Parse the response if parser is included
    else if (req->parser) {
        err = req->parser(&ctx, req->parser_out);
    }

cleanup:
    free(url);
    free(req_body);
    esp_http_client_set_user_data(handle->http_client, NULL);

    return err;
}

esp_err_t s2_client_http_add_request(s2_client_t *handle, const s2_request_t *request) {
    if (
        handle == NULL ||
        request == NULL ||
        request->body == NULL ||
        request->endpoint == NULL ||
        // If parser is set, result should be set as well
        (request->parser != NULL && request->parser_out == NULL)
    ) return ESP_ERR_INVALID_ARG;

    if (!request->done) return ESP_ERR_NO_MEM;

    if (xQueueSend(handle->request_queue, &request, pdMS_TO_TICKS(5000)) != pdTRUE) {
        vSemaphoreDelete(request->done);
        return ESP_ERR_TIMEOUT;
    }

    xSemaphoreTake(request->done, portMAX_DELAY); // Wait till request has finished
    vSemaphoreDelete(request->done);

    return request->result;
}

void s2_client_http_extract_host(const char *url, char *out, size_t out_len) {
    struct http_parser_url parsed;
    if (!url || !out || out_len == 0) return;

    size_t url_len = 0;
    if (!get_bounded_strlen(url, 2048, &url_len)) return;

    http_parser_url_init(&parsed);
    http_parser_parse_url(url, url_len, 0, &parsed);

    // Extract hostname
    char host[128] = {0};
    snprintf(host, sizeof(host), "%.*s", parsed.field_data[UF_HOST].len, url + parsed.field_data[UF_HOST].off);

    // Append port if present
    if (parsed.field_set & (1 << UF_PORT)) {
        snprintf(out, out_len, "%s:%d", host, parsed.port);
    }
    else {
        snprintf(out, out_len, "%s", host);
    }
}