// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

#include "log_forwarder.h"
#include "s2_common.h"
#include "s2_messages.h"
#include "s2_client_internal.h"

#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdlib.h>

#include <stdio.h>
#include "cJSON.h"

static const char *TAG = "log_forwarder";

#define LOGF_QUEUE_SIZE 64
#define LOGF_MSG_LEN 192
#define LOGF_SOURCE_LEN 32
#define LOGF_LEVEL_LEN 8
#define LOGF_BATCH_MAX 6
#define LOGF_FLUSH_MS 5000

typedef struct {
    bool in_use;
    time_t timestamp;
    char level[LOGF_LEVEL_LEN];
    char source[LOGF_SOURCE_LEN];
    char message[LOGF_MSG_LEN];
} log_entry_t;

static log_entry_t queue[LOGF_QUEUE_SIZE];
static int q_head = 0;
static int q_count = 0;
static SemaphoreHandle_t q_mutex = NULL;
static esp_timer_handle_t flush_timer = NULL;
static s2_client_t *global_handle = NULL;

/**
 * @brief Schedule a one-shot flush after the given delay.
 */
static void schedule_flush_once(uint64_t delay_ms) {
    if (!flush_timer) return;
    esp_timer_start_once(flush_timer, delay_ms * 1000ULL);
}

/**
 * @brief Copy console text for JSON, replacing anything outside printable ASCII.
 *
 * A forwarded line can carry raw bytes (crash dumps, half-written characters,
 * control codes). They would end up in a text frame as invalid UTF-8 and the
 * CEM drops the whole connection on it, so swap them for '?'.
 */
static void copy_text_safe_for_console(char *dst, const char *src, size_t size) {
    size_t i;
    if (size == 0) return;
    for (i = 0; src[i] && i < size - 1; i++) {
        unsigned char c = (unsigned char)src[i];
        dst[i] = (c >= 0x20 && c < 0x7F) ? (char)c : '?';
    }
    dst[i] = '\0';
}

/**
 * @brief Flush up to LOGF_BATCH_MAX queued entries as a Log.Batch message.
 */
static void flush_timer_cb(void *arg) {
    if (xSemaphoreTake(q_mutex, pdMS_TO_TICKS(100)) != pdTRUE) return;
    if (q_count == 0) {
        xSemaphoreGive(q_mutex);
        return;
    }

    int send_n = q_count < LOGF_BATCH_MAX ? q_count : LOGF_BATCH_MAX;

    char batch_id[37];
    char batch_msg_id[37];
    s2_generate_uuid(batch_id);
    s2_generate_uuid(batch_msg_id);

    cJSON *root = cJSON_CreateObject();
    if (!root) {
        xSemaphoreGive(q_mutex);
        schedule_flush_once(LOGF_FLUSH_MS);
        return;
    }
    cJSON_AddStringToObject(root, "message_type", "Log.Batch");
    cJSON_AddStringToObject(root, "message_id", batch_msg_id);
    cJSON_AddStringToObject(root, "batch_id", batch_id);
    cJSON *arr = cJSON_CreateArray();
    cJSON_AddItemToObject(root, "messages", arr);

    for (int i = 0; i < send_n; i++) {
        int idx = (q_head + i) % LOGF_QUEUE_SIZE;
        cJSON *obj = cJSON_CreateObject();
        char item_id[37];
        s2_generate_uuid(item_id);
        cJSON_AddStringToObject(obj, "id", item_id);
        cJSON_AddNumberToObject(obj, "timestamp", (double)queue[idx].timestamp);
        cJSON_AddStringToObject(obj, "level", queue[idx].level);
        cJSON_AddStringToObject(obj, "source", queue[idx].source);
        cJSON_AddStringToObject(obj, "message", queue[idx].message);
        cJSON_AddItemToArray(arr, obj);
    }

    char *body = cJSON_PrintUnformatted(root);
    if (!body) {
        cJSON_Delete(root);
        xSemaphoreGive(q_mutex);
        schedule_flush_once(LOGF_FLUSH_MS);
        return;
    }

    // Don't try to send when the websocket is gone (example: during reconnect if HEMS is down)
    if (global_handle && global_handle->websocket_client) {
        esp_err_t serr = s2_websocket_send_tracked(global_handle, body, batch_msg_id);
        if (serr == ESP_OK) {
            q_head = (q_head + send_n) % LOGF_QUEUE_SIZE;
            q_count -= send_n;
        } else {
            ESP_LOGW(TAG, "flush: s2_websocket_send_tracked failed (0x%x)", (unsigned int)serr);
        }
    }
    free(body);
    cJSON_Delete(root);

    xSemaphoreGive(q_mutex);
    schedule_flush_once(LOGF_FLUSH_MS);
}

void log_forwarder_flush_now_for_test(void) {
    flush_timer_cb(NULL);
}

void log_forwarder_reset_for_test(void) {
    q_mutex = NULL;
}

void log_forwarder_init(s2_client_t *handle) {
    if (!q_mutex) q_mutex = xSemaphoreCreateMutex();
    global_handle = handle;
    if (!flush_timer) {
        const esp_timer_create_args_t args = { .callback = flush_timer_cb, .arg = NULL, .name = "logf_flush" };
        esp_timer_create(&args, &flush_timer);
    }
    // schedule periodic flush
    schedule_flush_once(LOGF_FLUSH_MS);
}

void log_forwarder_enqueue(const char *level, const char *source, const char *message) {
    /* Filter out S2 control messages (e.g. ReceptionStatus) that originate from
     * the protocol layer. Forwarding those back to the CEM creates an ACK echo
     * loop (CEM -> RM ReceptionStatus included in RM logs -> CEM ACKs again).
     */
    if (message && strstr(message, "ReceptionStatus")) {
        ESP_LOGD(TAG, "enqueue: dropping protocol ReceptionStatus payload");
        return;
    }
    /* Lazily create the mutex so logs can be queued before the forwarder is
     * fully initialized (e.g. before handshake completes). This allows the
     * application to call enqueue early; queued messages will be sent once
     * `log_forwarder_init` sets up the flush timer and `global_handle`.
     */
    if (!q_mutex) {
        q_mutex = xSemaphoreCreateMutex();
        if (!q_mutex) {
            ESP_LOGW(TAG, "enqueue: failed to create mutex");
            return;
        }
    }
    if (xSemaphoreTake(q_mutex, pdMS_TO_TICKS(50)) != pdTRUE) return;
    if (q_count >= LOGF_QUEUE_SIZE) {
        // drop oldest to make space
        q_head = (q_head + 1) % LOGF_QUEUE_SIZE;
        q_count--;
        ESP_LOGW(TAG, "enqueue: queue full, dropping oldest");
    }
    int idx = (q_head + q_count) % LOGF_QUEUE_SIZE;
    queue[idx].in_use = true;
    queue[idx].timestamp = time(NULL);
    /* Use strlcpy to ensure null-termination and avoid strncpy pitfalls */
    strlcpy(queue[idx].level, level ? level : "INFO", LOGF_LEVEL_LEN);
    strlcpy(queue[idx].source, source ? source : "RM", LOGF_SOURCE_LEN);
    copy_text_safe_for_console(queue[idx].message, message ? message : "", LOGF_MSG_LEN);
    q_count++;
    xSemaphoreGive(q_mutex);

    // If we reached batch size, flush immediately
    if (q_count >= LOGF_BATCH_MAX) {
        schedule_flush_once(0);
    }
}
