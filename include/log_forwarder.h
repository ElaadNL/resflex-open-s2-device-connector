// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "s2_client_internal.h"

/**
 * @brief Initialize the log forwarder.
 *
 * Call after the WebSocket is connected and the handshake is complete.
 */
void log_forwarder_init(s2_client_t *handle);

/**
 * @brief Enqueue a log entry for forwarding.
 *
 * Copy-safe: the caller retains ownership of the strings.
 */
void log_forwarder_enqueue(const char *level, const char *source, const char *message);

/**
 * @brief Trigger an immediate flush of queued entries (test helper).
 */
void log_forwarder_flush_now_for_test(void);

/**
 * @brief Reset internal state (test helper).
 */
void log_forwarder_reset_for_test(void);
