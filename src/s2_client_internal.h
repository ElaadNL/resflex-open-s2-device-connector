// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "s2_common.h"
#include "s2_client.h"
#include <esp_http_client.h>
#include "esp_websocket_client.h"
#ifdef UNITY_INCLUDE_CONFIG_H
#include "Mockesp_timer.h"
#else
#include "esp_timer.h"
#endif

// Timeout (in milliseconds) applied when sending a message over WebSockets.
#define SEND_TIMEOUT_MS 15000

// Receive buffer for assembling fragmented WebSocket frames
#define S2_WEBSOCKET_RECV_BUFFER_SIZE 8192

// max number of outgoing messages tracked simultaneously waiting for ReceptionStatus OK
#define S2_TRACKED_MSG_MAX 20 // max tracked in-flight messages. Removed once OK reception status received.

// max raw JSON body size per tracked message. Messages exceeding this cannot be tracked/retried.
// power forecasts could be very large if the intervals are small
// if we send a 24 hour 15 min interval forecast for 3 phase device that would be around 5KB
// which is way too much. We can set limit of 1KB which is enough for most messages.
#define S2_TRACKED_MSG_BODY_MAX 1024 

/**
 * @brief Entry in the sent-message tracking table (message_id -> raw JSON body)
 */
typedef struct {
    char id[37];                        // UUID of the sent message
    char body[S2_TRACKED_MSG_BODY_MAX]; // raw JSON to resend on retry
    bool in_use;                        // whether this slot is occupied
    uint8_t retry_count;               // backoff exponent (delay = 2^retry_count seconds, capped at 64s)
    esp_timer_handle_t retry_timer;    // NULL until first TEMPORARY_ERROR
    void *client_handle;               // back pointer to s2_client_t needed in timer callback
} s2_tracked_msg_t;

// pending limit range values per quantity, collected until all are ready before building power/energy constraint messages
typedef struct {
    bool power_upper_max_ready; float power_upper_max; // highest value CEM may set for upper power limit
    bool power_upper_min_ready; float power_upper_min; // lowest value for upper power limit
    bool power_lower_max_ready; float power_lower_max; // highest value for lower power limit
    bool power_lower_min_ready; float power_lower_min; // lowest value for lower power limit
    bool energy_upper_max_ready; float energy_upper_max; // highest upper average power the device supports
    bool energy_upper_min_ready; float energy_upper_min; // lowest upper average power 
    bool energy_lower_max_ready; float energy_lower_max; // highest lower average power 
    bool energy_lower_min_ready; float energy_lower_min; // lowest lower average power 
    time_t energy_valid_until;   // optional override for energy constraint valid_until (0 = use default)
} s2_pending_constraints_t;

// per-quantity forecast state, indexed by rm.commodity_quantities
typedef struct {
    float forecast_value;
    char forecast_id[37];        // empty = not yet sent
    uint8_t forecast_miss_ticks;
    time_t last_forecast_sent;
} s2_forecast_state_t;

// last successfully sent PEBC constraint values per quantity slot.
// used for duplicate message checking and RevokeObject.
typedef struct {
    // last sent power constraint range values. all zero = nothing sent yet.
    float power_upper_max;
    float power_upper_min;
    float power_lower_max;
    float power_lower_min;
    float energy_upper_max; // only the two that go into EnergyConstraint
    float energy_lower_min;

    // S2 object IDs. empty string = not yet sent OR already revoked.
    char power_constraints_id[37];   // PEBC.PowerConstraints object id
    char energy_constraint_id[37];   // PEBC.EnergyConstraint object id
} s2_last_sent_t;

// max power_envelope_elements per envelope in a single PEBC.Instruction
#define S2_PEBC_MAX_ENVELOPE_ELEMENTS 24 // probably enough for most cases. Can handle 1 day of 1 hour instruction elements.

// max duration of a single instruction element. The S2 schema only requires >= 0, so this is
// a sanity bound that keeps the value inside the range the timer code can handle.
#define S2_PEBC_MAX_ELEMENT_DURATION_MS (24 * 60 * 60 * 1000) // 1 day

// one element in a power envelope: per-commodity limits + how long to apply them
typedef struct {
    float upper_limit[S2_MAX_COMMODITY_QUANTITIES];
    float lower_limit[S2_MAX_COMMODITY_QUANTITIES];
    uint32_t duration_ms;
} s2_instruction_element_t;

// active/pending PEBC instruction. Can have multiple instruction elements that execute in order (chain together).
typedef struct {
    char id[37]; // id[0] == '\0' means no active/pending instruction
    s2_instruction_element_t elements[S2_PEBC_MAX_ENVELOPE_ELEMENTS]; // array of instruction elements from the current PEBC.Instruction 
    uint8_t element_count;   // total elements received
    uint8_t current_element; // index of the currently executing element
    time_t execution_time;
    esp_timer_handle_t exec_timer;     // fires at execution_time
    esp_timer_handle_t duration_timer; // fires when current element's duration expires
} s2_active_instruction_t;

typedef struct {
    esp_timer_handle_t measurement;
    esp_timer_handle_t constraints;
    esp_timer_handle_t forecast;
} s2_session_timers_t;

/**
 * @brief WebSocket connection state
 */
typedef enum {
    S2_CLIENT_WEBSOCKET_CONN_STATE_CONNECTING,
    S2_CLIENT_WEBSOCKET_CONN_STATE_DISCONNECTED,
    S2_CLIENT_WEBSOCKET_CONN_STATE_READY
} s2_client_websocket_conn_state_t;

/**
 * @brief S2 client handle
 */
struct s2_client {
    /* Set at initialization, never change */
    s2_node_t node;
    s2_rm_config_t rm;
    struct {
        char base_url[128]; 
        char host[128];
        char interface[IF_NAMESIZE];
        bool enable_tls;
        bool skip_cert_cn_check;
        size_t cert_pem_len;
        char *cert_pem;
    } server;

    /* Runtime configuration */

    /* Runtime state fields */
    esp_http_client_handle_t http_client;
    esp_websocket_client_handle_t websocket_client;
    /* Buffer for assembling fragmented websocket payloads */
    char websocket_rx_buffer[S2_WEBSOCKET_RECV_BUFFER_SIZE];
    size_t websocket_rx_len;

    /*
     * Queue of requests that should be performed between S2 client and server
     */
    QueueHandle_t request_queue;

    /*
     * Dedicated task used for performing and processing HTTP requests between S2
     * client and server
     */
    TaskHandle_t http_task;

    /* 
     * Mutex to protect the internal state machine (e.g., CONNECTING, READY, DISCONNECTED).
     * Ensures that state transitions are atomic and prevents tasks from attempting 
     * operations (like sending data) while the connection is in an intermediate or invalid state.
     */
    SemaphoreHandle_t websocket_state_mutex;

    /*
     * Event group to keep track of connection state of the websocket. We wait till handshake
     * has finished before messages can be processed
     */
    EventGroupHandle_t websocket_conn_event_group;

    /*
     * Current connection state of the websocket
     */
    s2_client_websocket_conn_state_t websocket_conn_state;

    /* S2 session timers */
    s2_session_timers_t timers;

    /* Last access token used to obtain communication tokens - retained to re-initiate connection on auth failures */
    char last_access_token[128];

    /* WebSocket session URL from requestConnectionDetails (for reconnect) */
    char session_url[256];

    /* NodeId of the paired CEM, needed to re-initiate a session on reconnect */
    char server_node_id[40];

    /* access token list (newest first) for reconnection attempts */
    char access_tokens[S2_MAX_ACCESS_TOKENS][S2_ACCESS_TOKEN_LEN];
    uint8_t access_token_count;

    /* SHA256 fingerprint of the CEM's TLS leaf certificate (32 bytes, zeroed = not set) */
    uint8_t tls_cert_fingerprint[S2_TLS_FINGERPRINT_SIZE];

    /* cert fingerprint pinned after successful HMAC verification */
    bool tls_cert_pinned;

    /* DNS-SD discovery enabled */
    bool dns_sd_enabled;

    /* Whether a reconnect task is currently scheduled/running to avoid duplicates */
    bool reconnecting;

    /* Active or pending PEBC instruction. id[0] == '\0' means none. */
    s2_active_instruction_t instruction;

    /* Sent-message tracking: maps message_id -> body for retry on TEMPORARY_ERROR */
    s2_tracked_msg_t tracked_msgs[S2_TRACKED_MSG_MAX];

    /* Active slot index during constraint timer callback. Set before calling on_request_param so
     * the correct slot is used even when two slots share the same
     * commodity_quantity (example: one normal L1 slot and one abnormal_condition_only L1 slot).
     * -1 means no active constraint slot. */
    int active_constraint_slot;

    /* pending constraint range values per quantity slot, indexed same as rm.pebc.default_limits */
    s2_pending_constraints_t pending_constraints[S2_MAX_COMMODITY_QUANTITIES];

    /* last sent PEBC constraint values per quantity slot, for duplicate check and RevokeObject */
    s2_last_sent_t last_sent[S2_MAX_COMMODITY_QUANTITIES];

    /* forecast state per commodity quantity, indexed same as rm.commodity_quantities */
    s2_forecast_state_t forecast[S2_MAX_COMMODITY_QUANTITIES];
};
typedef struct s2_client s2_client_t;

/**
 * @brief Prune the access token list to just the newest entry and persist to NVS.
 *
 * Called after confirmAccessToken succeeds.
 */
void s2_client_prune_access_tokens(s2_client_t *c);

/**
 * @brief   Acquires the WebSocket TX mutex, sends a tracked message body, registers it for
 *          retry on TEMPORARY_ERROR, then releases the mutex.
 *          Declared here so s2_pebc_logic.c can call it without including websocket internals.
 *
 * @param   h       S2 client handle
 * @param   body    JSON body string to send (must remain valid for the duration of the call)
 * @param   id      UUID of the message, used as key in the tracking table
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_ARG if the body exceeds the maximum message length
 *      - ESP_FAIL if the WebSocket send failed
 */
esp_err_t s2_websocket_send_tracked(s2_client_t *h, char *body, const char *id);

/**
 * @brief   Processes a ReceptionStatus message received from the CEM.
 *          Removes the acknowledged message from the tracking table on OK,
 *          or schedules a retry on TEMPORARY_ERROR.
 *
 * @param   handle  S2 client handle
 * @param   message Parsed cJSON object of the incoming message
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_FAIL if the message is malformed or the status is invalid
 */
esp_err_t s2_client_websocket_process_reception_status(s2_client_t *handle, cJSON *message);


/**
 * @brief   Processes an incoming SelectControlType message from the CEM.
 *          Validates the requested control type and sends a confirmation.
 *
 * @param   handle  S2 client handle
 * @param   message Parsed cJSON object of the incoming message
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_FAIL if the control type is unsupported or the response cannot be sent
 */
esp_err_t s2_client_websocket_process_select_control_type(s2_client_t *handle, cJSON *message);

/**
 * @brief   Timer callback that fires when a tracked message retry delay expires.
 *          Resends the stored message body and reschedules with exponential backoff.
 *
 * @param   arg     Pointer to the s2_tracked_msg_t slot that triggered the retry
 */
void s2_retry_timer_cb(void *arg);

/**
 * @brief   Dispatches an incoming WebSocket message to the appropriate handler
 *          based on the message_type field.
 *
 * @param   handle  S2 client handle
 * @param   data    WebSocket event data containing the raw message payload
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_FAIL if the message cannot be parsed or the handler reports an error
 */
esp_err_t s2_client_websocket_process_message(s2_client_t *handle, esp_websocket_event_data_t *data);
