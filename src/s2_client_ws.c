// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

#include "s2_client_ws.h"
#include "s2_client_internal.h"
#include "s2_client_pairing.h"
#include "s2_pebc_logic.h"
#include "s2_common.h"
#include "s2_messages.h"
#include "log_forwarder.h"

#include "esp_crt_bundle.h"
#include "esp_websocket_client.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_bit_defs.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

#define EVENT_CONNECTED_BIT BIT0   // Bit used to detect established connection
#define S2_MAX_TEXT_MESSAGE_LEN 4096

// Reconnect: exponential backoff
// delay_n = random(0, min(max_delay, base_delay x 2^n))
#define RECONNECT_BASE_DELAY_MS  2000
#define RECONNECT_MAX_DELAY_MS 600000
#define RECONNECT_MAX_RETRIES       16

// How long the WebSocket handshake may take before an attempt counts as failed. The client
// itself uses network_timeout_ms (10 s), so allow a little more than that.
#define S2_WS_CONNECT_TIMEOUT_MS 15000

static const char *TAG = "s2_client_websocket";

/**
 * @brief Helper to make JSON logging more readable.
 */
static void log_websocket(const char *dir, const char *json, int len) { // dir: "RM -> CEM" or "CEM -> RM"
    if (!json) {
        ESP_LOGD(TAG, "%s\n<null>", dir);
        return;
    }

    size_t parse_len = 0;
    if (len < 0) {
        if (!get_bounded_strlen(json, S2_MAX_TEXT_MESSAGE_LEN, &parse_len)) {
            ESP_LOGW(TAG, "log_websocket: unterminated text or too long, skipping pretty print");
            ESP_LOGD(TAG, "%s\n<invalid-text>", dir);
            return;
        }
    } else {
        parse_len = (size_t)len;
    }

    ESP_LOGI(TAG, "%s\n%.*s", dir, (int)parse_len, json);
}

/**
 * @brief Store a sent message in the first free tracking slot.
 *
 * Allows it to be retried on TEMPORARY_ERROR.
 */
static bool s2_track_message(s2_client_t *handle, const char *id, const char *body) {
    // find first free slot array is zero-init'd so all start in_use=false
    for (int i = 0; i < S2_TRACKED_MSG_MAX; i++) {
        if (!handle->tracked_msgs[i].in_use) {
            strlcpy(handle->tracked_msgs[i].id, id, sizeof(handle->tracked_msgs[i].id));
            strlcpy(handle->tracked_msgs[i].body, body, sizeof(handle->tracked_msgs[i].body));
            handle->tracked_msgs[i].client_handle = handle;
            handle->tracked_msgs[i].in_use = true;
            return true;
        }
    }
    ESP_LOGW(TAG, "s2_track_message: tracking table full, message %s will not be retried", id);
    return false;
}

/**
 * @brief Free the tracking slot for the given message id.
 *
 * Stops any pending retry timer.
 */
static void s2_untrack_message(s2_client_t *handle, const char *id) {
    for (int i = 0; i < S2_TRACKED_MSG_MAX; i++) {
        if (handle->tracked_msgs[i].in_use && strcmp(handle->tracked_msgs[i].id, id) == 0) {
            s2_timer_release(&handle->tracked_msgs[i].retry_timer);
            handle->tracked_msgs[i].retry_count = 0;
            handle->tracked_msgs[i].in_use = false;
            return;
        }
    }
}

esp_err_t s2_client_websocket_send_message(s2_client_handle_t client_handle, const char *message, uint32_t timeout_ms) {
    // Cast handle to correct type
    s2_client_t *handle = client_handle;
    if (!handle || !message || !s2_is_valid_json(message)) return ESP_ERR_INVALID_ARG;
    if (handle->websocket_conn_event_group == NULL) {
        ESP_LOGE(TAG, "s2_client_websocket_send_message: WebSocket connection has not been initialized");
        return ESP_ERR_INVALID_STATE;
    }

    // Wait for WebSocket to be connected
    EventBits_t bits = xEventGroupWaitBits(
        handle->websocket_conn_event_group,
        EVENT_CONNECTED_BIT,
        pdFALSE,    // Do not clear bits on exit
        pdTRUE,     // Wait for the bit to be set
        pdMS_TO_TICKS(timeout_ms)
    );
    if (bits == 0) {
        ESP_LOGE(TAG, "s2_client_websocket_send_message: WebSocket not connected (Timeout)");
        return ESP_ERR_TIMEOUT;
    }

    // Double check the connection state
    xSemaphoreTake(handle->websocket_state_mutex, portMAX_DELAY);
    if (handle->websocket_conn_state != S2_CLIENT_WEBSOCKET_CONN_STATE_READY) {
        xSemaphoreGive(handle->websocket_state_mutex);
        ESP_LOGE(TAG, "s2_client_websocket_send_message: Connection not ready");
        return ESP_ERR_INVALID_STATE;
    }
    xSemaphoreGive(handle->websocket_state_mutex);

    // Send message over WebSocket connection
    size_t msg_len = 0;
    if (!get_bounded_strlen(message, S2_MAX_TEXT_MESSAGE_LEN, &msg_len)) {
        ESP_LOGE(TAG, "s2_client_websocket_send_message: invalid or unterminated message");
        return ESP_ERR_INVALID_ARG;
    }
    int len = esp_websocket_client_send_text(handle->websocket_client, message, msg_len, pdMS_TO_TICKS(SEND_TIMEOUT_MS));

    // Verify success
    if (len < 0) {
        ESP_LOGE(TAG, "s2_client_websocket_send_message: Error sending message");
        return ESP_FAIL;
    }

    if ((size_t)len < msg_len) {
        ESP_LOGE(TAG, "s2_client_websocket_send_message: Partial send! Sent %d of %zu bytes", len, msg_len);
        return ESP_FAIL;
    }

    return ESP_OK;
}

/**
 * @brief Tear down the WebSocket client, timers, and tracked-message table on disconnect.
 */
static void s2_client_clear_stale_state(s2_client_t *h) {
    if (h->websocket_client) {
        esp_websocket_client_stop(h->websocket_client);
        esp_websocket_client_destroy(h->websocket_client);
        h->websocket_client = NULL;
    }
    // release every session timer so none keep firing after the connection drops
    s2_timer_release(&h->timers.measurement);
    s2_timer_release(&h->timers.constraints);
    s2_timer_release(&h->timers.forecast);
    // clear message tracking table so stale retry timers don't leak
    for (int i = 0; i < S2_TRACKED_MSG_MAX; i++) {
        s2_timer_release(&h->tracked_msgs[i].retry_timer);
        h->tracked_msgs[i] = (s2_tracked_msg_t){0};
    }
    // clear last-sent constraint IDs so constraints are re-sent on reconnect
    memset(h->last_sent, 0, sizeof(h->last_sent));
}

/**
 * @brief Re-initiate the session so the WebSocket gets a fresh commToken.
 *
 * A commToken is good for one connection and expires in 30 s, so a reconnect has to run
 * initiateSession and confirmAccessToken again.
 */
static esp_err_t s2_client_reinitiate_session(s2_client_t *h, const char *access_token) {
    s2_client_initiate_session_response_t init_resp = {0};
    esp_err_t err = s2_client_initiate_session(h, h->server_node_id, access_token, &init_resp);
    if (err != ESP_OK) {
        // hand the real code back: ESP_ERR_NOT_SUPPORTED means the CEM rejected the token
        ESP_LOGW(TAG, "s2_client_websocket_reconnect: initiateSession failed 0x%x", err);
        return err;
    }

    // the CEM issues a new pending access token, confirm that one to get the commToken
    const char *pending = init_resp.access_token[0] ? init_resp.access_token : access_token;
    s2_client_confirm_access_token_response_t confirm_resp = {0};
    err = s2_client_confirm_access_token_extended(h, pending, &confirm_resp);
    if (err != ESP_OK) {
        // the pending token is short lived: a failure here means it expired, not that the
        // stored token was refused, so report it as transient and let the caller retry
        ESP_LOGW(TAG, "s2_client_websocket_reconnect: confirmAccessToken failed 0x%x", err);
        return ESP_ERR_INVALID_STATE;
    }

    const char *url = confirm_resp.websocket_url[0] ? confirm_resp.websocket_url : h->session_url;
    xEventGroupClearBits(h->websocket_conn_event_group, EVENT_CONNECTED_BIT);
    err = s2_client_websocket_connect(h, url, confirm_resp.websocket_token, pending);
    if (err != ESP_OK) return err;

    // esp_websocket_client_start only spawns the handshake task, so wait for the connection
    // to exist: otherwise a failed handshake is reported as a restored session
    if (xEventGroupWaitBits(h->websocket_conn_event_group, EVENT_CONNECTED_BIT,
                            pdFALSE, pdTRUE, pdMS_TO_TICKS(S2_WS_CONNECT_TIMEOUT_MS)) == 0) {
        ESP_LOGW(TAG, "s2_client_websocket_reconnect: connect timed out");
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

/**
 * @brief Reconnect with one stored token, backing off between attempts.
 *
 * @return ESP_OK when the session is back, ESP_ERR_NOT_SUPPORTED when the CEM refused
 *         this token, ESP_FAIL when every attempt for it failed.
 */
static esp_err_t s2_client_reconnect_with_token(s2_client_t *h, int token_index) {
    const char *token = h->access_tokens[token_index];

    for (int attempt = 0; attempt < RECONNECT_MAX_RETRIES; attempt++) {
        s2_client_clear_stale_state(h);
        esp_err_t err = s2_client_reinitiate_session(h, token);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "s2_client_websocket_reconnect: re-initiated and connected token %d attempt %d",
                     token_index, attempt + 1);
            return ESP_OK;
        }
        // CEM refused this token, so retrying it is pointless: move to the next stored one
        if (err == ESP_ERR_NOT_SUPPORTED) return ESP_ERR_NOT_SUPPORTED;

        // full jitter, so RMs coming back after the same outage don't retry in lockstep
        uint32_t max_delay = RECONNECT_BASE_DELAY_MS << (attempt < 9 ? attempt : 9);
        if (max_delay > RECONNECT_MAX_DELAY_MS) max_delay = RECONNECT_MAX_DELAY_MS;
        uint32_t delay_ms = esp_random() % (max_delay + 1);
        ESP_LOGW(TAG, "s2_client_websocket_reconnect: token %d attempt %d/%d failed, retry in %lu ms",
                 token_index, attempt + 1, RECONNECT_MAX_RETRIES, (unsigned long)delay_ms);
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }

    return ESP_FAIL;
}

/**
 * @brief Try every stored token once.
 *
 * @return true when a session is back or every token was refused, false to start over.
 */
static bool s2_client_reconnect_all_tokens(s2_client_t *h) {
    bool all_rejected = true;

    for (int t = 0; t < h->access_token_count; t++) {
        if (h->access_tokens[t][0] == '\0') continue;

        esp_err_t err = s2_client_reconnect_with_token(h, t);
        if (err == ESP_OK) return true;
        if (err != ESP_ERR_NOT_SUPPORTED) all_rejected = false;
    }

    // the CEM refused every token, so this node has to pair again before it can hold a session
    if (all_rejected) {
        ESP_LOGE(TAG, "s2_client_websocket_reconnect: CEM rejected every token, re-pairing required");
        return true;
    }
    return false;
}

/**
 * @brief FreeRTOS task: reconnect with exponential backoff.
 */
static void s2_client_websocket_reconnect_task(void *arg) {
    s2_client_t *h = arg;
    h->reconnecting = true;

    if (h->access_token_count == 0 || h->session_url[0] == '\0' || h->server_node_id[0] == '\0') {
        ESP_LOGE(TAG, "s2_client_websocket_reconnect: missing session URL, tokens or server node id");
    }
    else {
        // keep retrying: the CEM may be down a long time
        while (!s2_client_reconnect_all_tokens(h)) {
            ESP_LOGW(TAG, "s2_client_websocket_reconnect: %d attempts per token failed, starting over", RECONNECT_MAX_RETRIES);
        }
    }

    h->reconnecting = false;
    vTaskDelete(NULL);
}


esp_err_t s2_client_websocket_process_reception_status(s2_client_t *handle, cJSON *message) {
    cJSON *subject_id = cJSON_GetObjectItem(message, "subject_message_id");
    cJSON *status     = cJSON_GetObjectItem(message, "status");
    if (!cJSON_IsString(subject_id) || !cJSON_IsString(status)) return ESP_ERR_INVALID_ARG;

    const char *id = subject_id->valuestring;
    const char *st = status->valuestring;

    if (strcmp(st, "OK") == 0) {
        s2_untrack_message(handle, id);

    // TEMPORARY_ERROR: retry with exponential backoff per message
    } else if (strcmp(st, "TEMPORARY_ERROR") == 0) {
        for (int i = 0; i < S2_TRACKED_MSG_MAX; i++) {
            if (!handle->tracked_msgs[i].in_use || strcmp(handle->tracked_msgs[i].id, id) != 0) continue;
            // exponential backoff: 1s, 2s, 4s... max 64s (2^6)
            uint8_t exp = handle->tracked_msgs[i].retry_count < 6 ? handle->tracked_msgs[i].retry_count : 6;
            uint64_t delay_us = (1ULL << exp) * 1000000ULL;

            // create timer once per slot, reuse on subsequent retries
            if (!handle->tracked_msgs[i].retry_timer) {
                esp_timer_create_args_t args = { .callback = s2_retry_timer_cb, .arg = &handle->tracked_msgs[i], .name = "s2_retry" };
                esp_timer_create(&args, &handle->tracked_msgs[i].retry_timer);
            }
            esp_timer_start_once(handle->tracked_msgs[i].retry_timer, delay_us);
            handle->tracked_msgs[i].retry_count++;
            break;
        }
    } else {
        // INVALID_CONTENT / INVALID_MESSAGE / INVALID_DATA / PERMANENT_ERROR: log and drop
        ESP_LOGW(TAG, "s2_client_websocket_process_reception_status: %s for msg %s, dropping", st, id);
        s2_untrack_message(handle, id);
    }
    return ESP_OK;
}

/**
 * @brief Build and send a ReceptionStatus for the given subject id and status.
 */
static void s2_client_websocket_send_reception_status(s2_client_t *handle, const char *subject_id, const char *status) {
    cJSON *root = cJSON_CreateObject();
    if (!root) return;

    cJSON_AddStringToObject(root, "message_type", "ReceptionStatus");
    cJSON_AddStringToObject(root, "subject_message_id", subject_id);
    cJSON_AddStringToObject(root, "status", status);

    char *body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!body) return;

    log_websocket("RM -> CEM:", body, -1);
    size_t body_len = 0;
    if (!get_bounded_strlen(body, S2_MAX_TEXT_MESSAGE_LEN, &body_len)) {
        free(body);
        return;
    }
    esp_websocket_client_send_text(handle->websocket_client, body, body_len, pdMS_TO_TICKS(SEND_TIMEOUT_MS));
    free(body);
}

/**
 * @brief Lock, send a body, track for retry, then unlock.
 *
 * Caller must free the body.
 */
esp_err_t s2_websocket_send_tracked(s2_client_t *h, char *body, const char *id) {
    log_websocket("RM -> CEM:", body, -1);
    size_t body_len = 0;
    if (!get_bounded_strlen(body, S2_MAX_TEXT_MESSAGE_LEN, &body_len)) return ESP_ERR_INVALID_ARG;
    int sent = esp_websocket_client_send_text(h->websocket_client, body, body_len, pdMS_TO_TICKS(SEND_TIMEOUT_MS));
    if (sent < 0) {
        ESP_LOGE(TAG, "s2_websocket_send_tracked: send failed for message %s (err=%d)", id, sent);
        return ESP_FAIL;
    }
    if (!s2_track_message(h, id, body)) {
        ESP_LOGW(TAG, "s2_websocket_send_tracked: message %s sent but could not be tracked for retry", id);
    }
    return ESP_OK;
}

/**
 * @brief Build and send a PowerMeasurement to the CEM.
 */
esp_err_t s2_client_provide_measurement(s2_client_handle_t handle, s2_commodity_quantity_t q, float value) {
    time_t now = time(NULL);
    s2_client_t *h = (s2_client_t *)handle;
    const char *qty = s2_commodity_quantity_to_string(q);
    char msg_id[37];
    char *body = s2_build_power_measurement(qty, value, now, msg_id);
    if (!body) return ESP_FAIL;
    esp_err_t err = s2_websocket_send_tracked(h, body, msg_id);
    free(body);
    return err;
}

/**
 * @brief Find the index in rm.commodity_quantities for a given quantity.
 *
 * @return Index, or -1 if not found.
 */
static int find_commodity_slot(const s2_client_t *h, s2_commodity_quantity_t q) {
    for (int i = 0; i < h->rm.commodity_quantities_count; i++) {
        if (h->rm.commodity_quantities[i] == q) return i;
    }
    return -1;
}

/**
 * @brief Send a PowerForecast revocation for the given slot and clear it on success.
 */
static void s2_try_revoke_forecast(s2_client_t *h, s2_forecast_state_t *fs) {
    char msg_id[37];
    char *body = s2_build_revoke_object("PowerForecast", fs->forecast_id, msg_id);
    if (!body) return;
    esp_err_t err = s2_websocket_send_tracked(h, body, msg_id);
    free(body);
    if (err == ESP_OK) {
        fs->forecast_id[0] = '\0';
        fs->forecast_miss_ticks = 0;
    }
}

/**
 * @brief Repeatedly request forecasts; revoke the last forecast if firmware stops responding.
 */
static void s2_forecast_timer_cb(void *arg) {
    s2_client_t *h = arg;
    for (int i = 0; i < h->rm.commodity_quantities_count; i++) {
        s2_forecast_state_t *fs = &h->forecast[i];
        // forecast_miss_ticks > 0 means firmware missed the last tick, revoke
        if (fs->forecast_miss_ticks >= 1 && fs->forecast_id[0] != '\0') {
            s2_try_revoke_forecast(h, fs);
        } else {
            fs->forecast_miss_ticks++;
        }
        h->rm.on_request_forecast(h->rm.commodity_quantities[i]);
    }
}

/**
 * @brief Build and send a PowerForecast to the CEM.
 *
 * Skips if the value is unchanged within the current interval.
 */
esp_err_t s2_client_provide_forecast(s2_client_handle_t handle, s2_commodity_quantity_t q, float value) {
    s2_client_t *h = (s2_client_t *)handle;
    int slot = find_commodity_slot(h, q);
    if (slot < 0) return ESP_ERR_INVALID_ARG;
    s2_forecast_state_t *fs = &h->forecast[slot];
    time_t now = time(NULL);
    bool value_same = fabsf(value - fs->forecast_value) < 0.01f && fs->forecast_id[0] != '\0';
    bool interval_elapsed = (now - fs->last_forecast_sent) >= (time_t)(h->rm.forecast_interval_ms / 1000);
    // only skip if value is the same AND the forecast interval has not elapsed yet
    if (value_same && !interval_elapsed) return ESP_OK;
    const char *qty = s2_commodity_quantity_to_string(q);
    char id[37];
    char *body = s2_build_power_forecast(qty, value, h->rm.forecast_interval_ms, id);
    if (!body) return ESP_FAIL;
    esp_err_t err = s2_websocket_send_tracked(h, body, id);
    free(body);
    if (err != ESP_OK) return err;
    strlcpy(fs->forecast_id, id, sizeof(fs->forecast_id));
    fs->forecast_value = value;
    fs->last_forecast_sent = now;
    fs->forecast_miss_ticks = 0;
    return ESP_OK;
}

esp_err_t s2_client_websocket_process_select_control_type(s2_client_t *handle, cJSON *message) {
    // extracts control type from received SelectControlType JSON message
    cJSON *control_type = cJSON_GetObjectItem(message, "control_type");
    // must exist
    if (!cJSON_IsString(control_type)) {
        ESP_LOGE(TAG, "s2_client_websocket_process_select_control_type: Missing control_type");
        return ESP_ERR_INVALID_ARG;
    }

    // Validate the selected control type matches what the RM is configured for.
    bool accepted = false;
    if (handle->rm.control_type == S2_CONTROL_TYPE_PEBC)
        accepted = strcmp(control_type->valuestring, "POWER_ENVELOPE_BASED_CONTROL") == 0;

    if (!accepted) {
        ESP_LOGE(TAG, "s2_client_websocket_process_select_control_type: Unexpected control type '%s', closing", control_type->valuestring);
        esp_websocket_client_close(handle->websocket_client, portMAX_DELAY);
        return ESP_FAIL;
    }

    // signals to firmware that s2 session is fully live/valid and measurements, constraints, + instructions from CEM will begin to be sent.
    handle->rm.on_session_ready();

    // start forecast timer - PowerForecast is common to all control types
    if (handle->rm.provides_forecast) {
        s2_timer_release(&handle->timers.forecast);
        esp_timer_create_args_t fargs = { .callback = s2_forecast_timer_cb, .arg = handle, .name = "s2_forecast" };
        if (esp_timer_create(&fargs, &handle->timers.forecast) != ESP_OK) {
            ESP_LOGE(TAG, "s2_client_websocket_process_select_control_type: Failed to create forecast timer");
            return ESP_FAIL;
        }
        if (esp_timer_start_periodic(handle->timers.forecast, (uint64_t)handle->rm.forecast_interval_ms * 1000) != ESP_OK) {
            ESP_LOGE(TAG, "s2_client_websocket_process_select_control_type: Failed to start forecast timer");
            return ESP_FAIL;
        }
        s2_forecast_timer_cb(handle); // fire immediately so CEM gets forecast without waiting a full interval
    }

    if (handle->rm.control_type == S2_CONTROL_TYPE_PEBC)
        s2_pebc_on_session_started(handle);

    return ESP_OK;
}

/**
 * @brief Fired after a TEMPORARY_ERROR backoff delay.
 *
 * Resends the tracked message body.
 */
void s2_retry_timer_cb(void *arg) {
    s2_tracked_msg_t *msg = (s2_tracked_msg_t *)arg;
    s2_client_t *handle = (s2_client_t *)msg->client_handle;
    size_t body_len = 0;
    if (!get_bounded_strlen(msg->body, sizeof(msg->body), &body_len)) {
        ESP_LOGE(TAG, "s2_retry_timer_cb: tracked body is not null-terminated");
        return;
    }
    esp_websocket_client_send_text(handle->websocket_client, msg->body, body_len, pdMS_TO_TICKS(SEND_TIMEOUT_MS));
}

/**
 * @brief Map an esp_err_t to the matching ReceptionStatus string.
 */
static const char *err_to_reception_status(esp_err_t err) {
    if (err == ESP_OK)              return "OK";
    if (err == ESP_ERR_INVALID_ARG) return "INVALID_CONTENT";
    return "PERMANENT_ERROR";
}

/**
 * Process any incoming messages on the WebSocket connection
 * 
 * @param   handle  Client handle obtained from initialization. As it contains the WebSocket client
 *                  instance we can use it to send a response
 * @param   data    Raw data received from the WebSocket server
 * 
 * @return
 *      - ESP_OK on successful parsing
 *
 *      - ESP_FAIL when something went wrong
 */
esp_err_t s2_client_websocket_process_message(s2_client_t *handle, esp_websocket_event_data_t *data) {
    esp_err_t err = ESP_FAIL;

    cJSON *json = cJSON_ParseWithLength((char *)data->data_ptr, data->data_len);
    if (!json) {
        ESP_LOGE(TAG, "s2_client_websocket_process_message: Failed to parse message to JSON");
        // best-effort: send INVALID_DATA with empty subject since we have no message_id
        s2_client_websocket_send_reception_status(handle, "", "INVALID_DATA");
        goto cleanup;
    }

    cJSON *message_type = cJSON_GetObjectItem(json, "message_type");
    cJSON *incoming_msg_id = cJSON_GetObjectItem(json, "message_id");
    // build subject_id for ReceptionStatus message later
    const char *subject_id = cJSON_IsString(incoming_msg_id) ? incoming_msg_id->valuestring : "";

    // send INVALID_DATA  ReceptionStatus for messages without JSON string message type
    if (!cJSON_IsString(message_type)) {
        ESP_LOGE(TAG, "s2_client_websocket_process_message: Received invalid message - no message type");
        s2_client_websocket_send_reception_status(handle, subject_id, "INVALID_DATA");
        goto cleanup;
    }

    if (strcmp(message_type->valuestring, "ReceptionStatus") == 0)
        err = s2_client_websocket_process_reception_status(handle, json);
        // dont send a ReceptionStatus in response to a ReceptionStatus message.
    else if (strcmp(message_type->valuestring, "SelectControlType") == 0) {
        err = s2_client_websocket_process_select_control_type(handle, json);
        s2_client_websocket_send_reception_status(handle, subject_id, err_to_reception_status(err));
        goto cleanup;
    }
    else if (strcmp(message_type->valuestring, "PEBC.Instruction") == 0) {
        err = s2_pebc_handle_instruction(handle, json);
        s2_client_websocket_send_reception_status(handle, subject_id, err_to_reception_status(err));
        goto cleanup;
    }
    else if (strcmp(message_type->valuestring, "RevokeObject") == 0) {
        err = s2_pebc_handle_revoke_object(handle, json);
        s2_client_websocket_send_reception_status(handle, subject_id, err_to_reception_status(err));
        goto cleanup;
    }
    else if (strcmp(message_type->valuestring, "SessionRequest") == 0) {
        cJSON *req_type = cJSON_GetObjectItem(json, "request");
        if (cJSON_IsString(req_type) && strcmp(req_type->valuestring, "RECONNECT") == 0) {
            ESP_LOGI(TAG, "SessionRequest RECONNECT from CEM - closing and reconnecting");
            s2_client_websocket_send_reception_status(handle, subject_id, "OK");
            esp_websocket_client_close(handle->websocket_client, portMAX_DELAY);
            err = ESP_OK;
        } else {
            s2_client_websocket_send_reception_status(handle, subject_id, "OK");
            err = ESP_OK;
        }
        goto cleanup;
    }
    else {
        ESP_LOGW(TAG, "s2_client_websocket_process_message: Unhandled message type '%s'", message_type->valuestring);
        // unhandled/unexpected message types get ReceptionStatus INVALID_MESSAGE.
        s2_client_websocket_send_reception_status(handle, subject_id, "INVALID_MESSAGE");
        goto cleanup;
    }

    err = ESP_OK;
cleanup:
    cJSON_Delete(json);
    return err;
}

// Forward declarations for event sub-handlers called from the main WebSocket event handler.
static void s2_client_websocket_event_handler_401(s2_client_t *handle);
static void s2_client_websocket_event_handler_disconnected(s2_client_t *handle);

/**
 * @brief Start the reconnect task on disconnect when an access token is available.
 */
static void s2_client_websocket_event_handler_disconnected(s2_client_t *handle) {
    if (handle->last_access_token[0] != '\0' && !handle->reconnecting) {
        ESP_LOGW(TAG, "s2_client_websocket: Disconnected - scheduling re-initiate and reconnect");
        handle->reconnecting = true;
        if (xTaskCreate(s2_client_websocket_reconnect_task, "s2_websocket_reconnect", 8192, handle, 5, NULL) != pdPASS) {
            ESP_LOGE(TAG, "s2_client_websocket: Failed to create reconnect task on disconnect");
            handle->reconnecting = false;
        }
    }
}

/**
 * @brief Handle HTTP 401 on the handshake by re-initiating the connection.
 */
static void s2_client_websocket_event_handler_401(s2_client_t *handle) {
    ESP_LOGW(TAG, "s2_client_websocket: Handshake returned 401, scheduling re-initiate and reconnect");
    if (handle->reconnecting) {
        ESP_LOGI(TAG, "s2_client_websocket: Reconnect already in progress, skipping spawn");
        return;
    }
    handle->reconnecting = true;
    if (xTaskCreate(s2_client_websocket_reconnect_task, "s2_websocket_reconnect", 8192, handle, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "s2_client_websocket: Failed to create reconnect task");
        handle->reconnecting = false;
    }
}

/**
 * @brief Main WebSocket event handler.
 *
 * Dispatches connect, data (with fragment reassembly), disconnect, and error events.
 */
static void s2_client_websocket_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;
    s2_client_t *handle = (s2_client_t *)handler_args;

    switch (event_id) {
        case WEBSOCKET_EVENT_CONNECTED:
            // transition to READY, send ResourceManagerDetails
            xSemaphoreTake(handle->websocket_state_mutex, portMAX_DELAY);
            handle->websocket_conn_state = S2_CLIENT_WEBSOCKET_CONN_STATE_READY;
            xSemaphoreGive(handle->websocket_state_mutex);
            xEventGroupSetBits(handle->websocket_conn_event_group, EVENT_CONNECTED_BIT);

            // send initial ResourceManagerDetails
            char rmd_id[37];
            char *rmd = s2_build_resource_manager_details(handle, rmd_id);
            if (rmd) {
                s2_websocket_send_tracked(handle, rmd, rmd_id);
                free(rmd);
            }
            log_forwarder_init(handle);
            ESP_LOGI(TAG, "s2_client_websocket: Connected to S2 server!");
            break;
        case WEBSOCKET_EVENT_DATA:
            // Ignore empty messages
            if (data->data_len == 0) break;

            log_websocket("CEM -> RM:", (char *)data->data_ptr, data->data_len);

            // Handle potentially fragmented messages from CEM.
            // safety: reject fragments that claim absurd total size
            if (data->payload_len > S2_WEBSOCKET_RECV_BUFFER_SIZE) {
                ESP_LOGE(TAG, "s2_client_websocket: incoming payload too large (%d), dropping", data->payload_len);
                handle->websocket_rx_len = 0;
                s2_client_websocket_send_reception_status(handle, "", "INVALID_DATA");
                break;
            }

            size_t copy_offset = (size_t)data->payload_offset;
            if (copy_offset + (size_t)data->data_len > S2_WEBSOCKET_RECV_BUFFER_SIZE) {
                ESP_LOGE(TAG, "s2_client_websocket: fragment overflow, dropping message");
                handle->websocket_rx_len = 0;
                s2_client_websocket_send_reception_status(handle, "", "INVALID_DATA");
                break;
            }
            memcpy(handle->websocket_rx_buffer + copy_offset, data->data_ptr, (size_t)data->data_len);
            // update current assembled length: max(current, offset+len)
            size_t new_len = copy_offset + (size_t)data->data_len;
            if (new_len > handle->websocket_rx_len) handle->websocket_rx_len = new_len;

            // If this is not the final fragment, wait for more
            if (!data->fin && handle->websocket_rx_len < (size_t)data->payload_len) break;

            esp_websocket_event_data_t assembled = {0};
            assembled.data_ptr = handle->websocket_rx_buffer;
            assembled.data_len = (int)handle->websocket_rx_len;
            if (s2_client_websocket_process_message(handle, &assembled) != ESP_OK) {
                ESP_LOGE(TAG, "s2_client_websocket: Failed to process assembled message");
                ESP_LOGE(TAG, "s2_client_websocket: Message = %.*s", assembled.data_len, (char *)assembled.data_ptr);
            }
            handle->websocket_rx_len = 0;
            break;
        case WEBSOCKET_EVENT_DISCONNECTED:
        case WEBSOCKET_EVENT_CLOSED:
            ESP_LOGE(TAG, "s2_client_websocket: Disconnected!");
            xEventGroupClearBits(handle->websocket_conn_event_group, EVENT_CONNECTED_BIT);
            handle->websocket_conn_state = S2_CLIENT_WEBSOCKET_CONN_STATE_DISCONNECTED;
            s2_client_websocket_event_handler_disconnected(handle);
            break;
        case WEBSOCKET_EVENT_ERROR:
            ESP_LOGE(TAG, "s2_client_websocket: Received error from S2 server WebSocket");
            if (data && data->error_handle.esp_ws_handshake_status_code == 401) {
                s2_client_websocket_event_handler_401(handle);
            }
            break;
    }
}

/**
 * @brief Test-only wrapper to invoke the static event handler.
 */
void s2_client_websocket_invoke_event_handler_for_test(void *handle, int32_t event_id, void *data) {
    s2_client_websocket_event_handler(handle, NULL, event_id, data);
}

/**
 * @brief Test-only wrapper to invoke the static reconnect task.
 */
void s2_client_websocket_reconnect_task_for_test(void *arg) {
    s2_client_websocket_reconnect_task(arg);
}

/**
 * @brief Convert an HTTP(S) URL to the corresponding WebSocket(S) scheme.
 */
static void convert_to_websocket_url(const char *url, char *websocket_url, size_t websocket_url_size) {
    if (strncmp(url, "https://", 8) == 0) {
        snprintf(websocket_url, websocket_url_size, "wss://%s", url + 8);
    } else if (strncmp(url, "http://", 7) == 0) {
        ESP_LOGW(TAG, "s2_client_websocket_connect: HTTP is insecure, prefer HTTPS");
        snprintf(websocket_url, websocket_url_size, "ws://%s", url + 7);
    } else {
        strlcpy(websocket_url, url, websocket_url_size);
    }
}

/**
 * @brief Apply TLS or TCP transport settings based on server config.
 */
static void apply_websocket_transport(esp_websocket_client_config_t *config,
                               const s2_client_t *handle) {
    if (handle->server.enable_tls) {
        config->transport = WEBSOCKET_TRANSPORT_OVER_SSL;
        config->skip_cert_common_name_check = handle->server.skip_cert_cn_check;
        if (handle->server.cert_pem && handle->server.cert_pem_len > 0) {
            config->cert_pem = handle->server.cert_pem;
            config->cert_len = handle->server.cert_pem_len;
        }
    } else {
        config->transport = WEBSOCKET_TRANSPORT_OVER_TCP;
    }
}

esp_err_t s2_client_websocket_connect(s2_client_handle_t client_handle, const char *url, const char *communication_token, const char *access_token) {
    esp_err_t err = ESP_FAIL;
    char *headers = NULL;
    bool own_mutex = false;
    bool own_event_group = false;

    // Cast handle to correct type
    s2_client_t *handle = client_handle;
    if (!handle || !url || !communication_token) return ESP_ERR_INVALID_ARG;

    // Sync objects live for the lifetime of the client, so only create the ones that are missing
    if (!handle->websocket_state_mutex) {
        handle->websocket_state_mutex = xSemaphoreCreateMutex();
        own_mutex = handle->websocket_state_mutex != NULL;
    }
    if (!handle->websocket_conn_event_group) {
        handle->websocket_conn_event_group = xEventGroupCreate();
        own_event_group = handle->websocket_conn_event_group != NULL;
    }
    if (handle->websocket_state_mutex == NULL || handle->websocket_conn_event_group == NULL) {
        ESP_LOGE(TAG, "s2_client_websocket_connect: Out of memory");
        err = ESP_ERR_NO_MEM;
        goto cleanup;
    }

    // Update state to "Connecting"
    xSemaphoreTake(handle->websocket_state_mutex, portMAX_DELAY);
    handle->websocket_conn_state = S2_CLIENT_WEBSOCKET_CONN_STATE_CONNECTING;
    xSemaphoreGive(handle->websocket_state_mutex);
    
    // Retain access_token for potential reconnect
    if (access_token) strlcpy(handle->last_access_token, access_token, sizeof(handle->last_access_token));

    // Build headers
    size_t token_len = 0;
    if (!get_bounded_strlen(communication_token, 512, &token_len)) {
        ESP_LOGE(TAG, "s2_client_websocket_connect: invalid communication token");
        err = ESP_ERR_INVALID_ARG;
        goto cleanup;
    }
    size_t header_len = sizeof("Authorization: Bearer ") - 1 + token_len + 3;
    headers = malloc(header_len);
    if (!headers) {
        ESP_LOGE(TAG, "s2_client_websocket_connect: Failed to allocate header buffer");
        err = ESP_ERR_NO_MEM;
        goto cleanup;
    }
    snprintf(headers, header_len, "Authorization: Bearer %s\r\n", communication_token);

    // Convert https:// to wss:// (WebSocket Secure scheme)
    char websocket_url[256];
    convert_to_websocket_url(url, websocket_url, sizeof(websocket_url));

    // CEM returns /api/s2_pebc/connection, but WebSocket is at /api/s2_pebc/ws
    char *conn = strstr(websocket_url, "/connection");
    if (conn) { memcpy(conn, "/ws", 3); conn[3] = '\0'; }

    // Store the wss:// URL for potential reconnect
    strlcpy(handle->session_url, websocket_url, sizeof(handle->session_url));

    // Configure the WebSocket client
    esp_websocket_client_config_t config = {
        .uri = websocket_url,
        .headers = headers,
        .reconnect_timeout_ms = 5000,
        .network_timeout_ms = 10000,
        .task_stack = 8192,
        .ping_interval_sec = 30,
        .pingpong_timeout_sec = 15,
    };

    // If interface is defined, configure it for the WS client
    if (handle->server.interface[0] != '\0') {
        struct ifreq if_name = {0};
        strlcpy(if_name.ifr_name, handle->server.interface, sizeof(if_name.ifr_name));
        config.if_name = &if_name;
    }

    // Configure TLS if enabled
    apply_websocket_transport(&config, handle);
    config.crt_bundle_attach = s2_client_no_verify_attach;

    // Connect to the WebSocket server and register for events
    ESP_LOGI(TAG, "s2_client_websocket_connect: Connecting to %s...", websocket_url);
    // If a previous client exists, destroy it first to ensure a clean start
    if (handle->websocket_client) {
        esp_websocket_client_destroy(handle->websocket_client);
        handle->websocket_client = NULL;
    }
    handle->websocket_client = esp_websocket_client_init(&config);

    if (handle->websocket_client == NULL) {
        ESP_LOGE(TAG, "s2_client_websocket_connect: Failed to initialize WebSocket client");
        err = ESP_ERR_NO_MEM;
        goto cleanup;
    }

    esp_websocket_register_events(handle->websocket_client, WEBSOCKET_EVENT_ANY, s2_client_websocket_event_handler, handle);
    esp_websocket_client_start(handle->websocket_client);

    free(headers);
    return ESP_OK;

cleanup:
    free(headers);
    // release only what this call created, clearing the reference straight after
    if (own_mutex) {
        vSemaphoreDelete(handle->websocket_state_mutex);
        handle->websocket_state_mutex = NULL;
    }
    if (own_event_group) {
        vEventGroupDelete(handle->websocket_conn_event_group);
        handle->websocket_conn_event_group = NULL;
    }

    return err;
}

/**
 * @brief Test wrappers to allow unit tests to call internal static helpers.
 */
const char *test_err_to_reception_status(esp_err_t err) { return err_to_reception_status(err); }
void test_s2_forecast_timer_cb(void *arg) { s2_forecast_timer_cb(arg); }