// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "s2_common.h"
#include "s2_client_internal.h"

#include "freertos/semphr.h"
#include "esp_err.h"

/**
 * @brief Response context that will contain the response of a HTTP request
 */
typedef struct {
    char    buffer[1024];
    int     len;
    bool    is_complete;
} s2_client_http_response_ctx_t;

/**
 * @brief Parser function definition parsing request responses
 */
typedef esp_err_t (*s2_client_response_parser_fn_t)(const s2_client_http_response_ctx_t *ctx, void *out);

/* default timeout for a request, in milliseconds */
#define S2_HTTP_DEFAULT_TIMEOUT_MS 5000

/* waitForPairing timeout. The CEM holds that response for up to 25s, so the
 * client has to allow more than that before giving up (S2 Connect 1.0). */
#define S2_LONG_POLL_TIMEOUT_MS 35000

/**
 * @brief S2 HTTP request configuration
 */
typedef struct {
    const char *endpoint;                           /*!< Endpoint appended to the S2 server base URL */
    const char *access_token;                       /*!< Optional access token that will be included as Authorization header when passed */
    const cJSON *body;                              /*!< JSON body included in the request */
    s2_client_response_parser_fn_t parser;          /*!< Optional parser function callback used to parse the response */
    void *parser_out;                               /*!< Output of the parser function */
    uint32_t timeout_ms;                            /*!< Optional timeout override, 0 keeps the client default */
    SemaphoreHandle_t done;                         /*!< Semaphore used to create a blocking/synchronous request */
    esp_err_t result;                               /*!< Will hold the request result status */
} s2_request_t;

/**
 * @brief   Processes events received from the HTTP client
 * 
 * @param   event Pointer to the event reveived from the client
 * 
 * @return
 *      - ESP_OK on success
 *
 *      - ESP_FAIL on failure
 */
esp_err_t s2_client_http_event_handler(esp_http_client_event_t *event);

/**
 * @brief   Performs a request to the S2 server node REST API
 * 
 * @param   handle          S2 client handle obtained from initialization
 * @param   req             Request configuration (e.g. endpoint, body etc.)
 * 
 * @return
 *      - ESP_OK on succes
 * 
 *      - ESP_ERR_INVALID_ARG on invalid function arguments
 * 
 *      - ESP_ERR_TIMEOUT when no HTTP client lock could be acquired
 * 
 *      - ESP_FAIL on failure
 */
esp_err_t s2_client_http_perform_request(s2_client_t *handle, s2_request_t *req);

/**
 * @brief   Adds a HTTP request to the request queue
 * 
 * @param handle        S2 client handle obtained from initialization
 * @param request       Request configuration
 * 
 * @return
 *      - ESP_OK on success
 * 
 *      - ESP_ERR_NO_MEM if memory allocation failed
 * 
 *      - ESP_ERR_INVALID_ARG on invalid function arguments
 * 
 *      - ESP_ERR_TIMEOUT if request could not be added to queue within time
 * 
 *      - ESP_FAIL on failure
 */
esp_err_t s2_client_http_add_request(s2_client_t *handle, const s2_request_t *request);

/**
 * Extracts host from server base URL
 * 
 * Example: https://homeassistant.local:8123/api -> homeassistant.local:8123
 * 
 * @param url       S2 server base URL to extract host from
 * @param out       Host will be stored in this field
 * @param out_len   Byte length of the out variable
 */
void s2_client_http_extract_host(const char *url, char *out, size_t out_len);
