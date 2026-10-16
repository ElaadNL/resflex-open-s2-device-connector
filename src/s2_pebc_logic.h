// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "s2_client_internal.h"
#include "cJSON.h"

/**
 * @brief Start the PEBC session: starts measurement, constraint, and forecast timers
 *        and fires initial callbacks so the CEM receives data immediately.
 *        Call once after SelectControlType PEBC is received.
 */
void s2_pebc_on_session_started(s2_client_t *h);

/**
 * @brief Handle a PEBC.Instruction message received from the CEM.
 *        Validates, stores, and schedules execution. Sends InstructionStatusUpdate responses.
 */
esp_err_t s2_pebc_handle_instruction(s2_client_t *h, cJSON *msg);

/**
 * @brief Handle a RevokeObject message from the CEM for PEBC objects
 *        (PEBC.Instruction, PEBC.PowerConstraints, PEBC.EnergyConstraint).
 */
esp_err_t s2_pebc_handle_revoke_object(s2_client_t *h, cJSON *msg);
