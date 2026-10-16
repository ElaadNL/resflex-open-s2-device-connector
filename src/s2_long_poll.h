// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "s2_client.h"
#include "s2_client_http.h"

// Server actions from /waitForPairing response.
typedef enum {
    S2_LP_NO_ACTION,         // 204, poll again
    S2_LP_SEND_NODE_DESC,    // include node + endpoint description next poll
    S2_LP_PREPARE_PAIRING,   // show pairing code in UI
    S2_LP_CANCEL_PREPARE,    // hide pairing code
    S2_LP_REQUEST_PAIRING,   // initiate pairing (caller must have token ready)
    S2_LP_ERROR,             // parse error or server error
} s2_long_poll_action_t;

// Result of one waitForPairing cycle.
typedef struct {
    s2_long_poll_action_t action;
    char client_node_id[40];
} s2_long_poll_result_t;

// Optional modifiers for the next waitForPairing poll.
// Only set these after the server has requested them:
//  - include_descriptions: after receiving the "sendNodeDescription" action.
//  - no_valid_token: after receiving "requestPairing" when no pairing token is
//    available (server expects errorMessage "NoValidTokenOnPairingClient").
typedef struct {
    bool include_descriptions;   // include clientNodeDescription + clientEndpointDescription
    bool no_valid_token;         // include errorMessage = "NoValidTokenOnPairingClient"
} s2_long_poll_request_options_t;

/**
 * @brief POST /pairing/v1/waitForPairing with optional fields.
 *
 * The server holds the request up to 25s. Call in a loop and check
 * result->action: NO_ACTION means poll again.
 */
esp_err_t s2_long_poll_wait_extended(s2_client_handle_t handle,
                               const char *client_node_id,
                               const s2_long_poll_request_options_t *options,
                               s2_long_poll_result_t *result);

/**
 * @brief Basic waitForPairing poll with clientNodeId only.
 */
esp_err_t s2_long_poll_wait(s2_client_handle_t handle,
                             const char *client_node_id,
                             s2_long_poll_result_t *result);

/**
 * @brief Parse an action string to its enum value (unit testing helper).
 */
s2_long_poll_action_t parse_action(const char *s);

/**
 * @brief Parse the waitForPairing response (unit testing helper).
 */
esp_err_t parse_long_poll_response(const s2_client_http_response_ctx_t *raw,
                                   void *out);
