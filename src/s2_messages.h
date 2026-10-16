// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "s2_client_internal.h"

/**
 * Builds a ResourceManagerDetails JSON message from the client config.
 * Caller must free() the returned string to prevent memory leaks.
 * Returns NULL on failure.
 * See spec for more details: https://docs.s2standard.org/model-reference/Common/ResourceManagerDetails/
 */
char *s2_build_resource_manager_details(const s2_client_t *handle, char msg_id_out[37]);

/**
 * Output IDs written by s2_build_power_constraints.
 * id:     S2 object id (used for RevokeObject if needed later).
 * msg_id: message_id (CEM references this in ReceptionStatus).
 */
typedef struct {
    char id[37];
    char msg_id[37];
} s2_pc_ids_t;

/**
 * Builds a PEBC.PowerConstraints message. valid_until is optional per spec.
 * valid_until: 0 = omit (constraint stays valid until replaced/revoked). Non-zero = include as ISO8601 deadline.
 * Caller must free() the returned string. Returns NULL on failure.
 */
char *s2_build_power_constraints(const char *quantity, const s2_pending_constraints_t *pc, bool upper_abn, bool lower_abn, time_t valid_until, bool defer, s2_pc_ids_t *ids_out);

/**
 * Builds a PEBC.EnergyConstraint message. valid_until is mandatory unlike for PowerConstraints.
 * valid_until: deadline for the energy budget. 0 = auto (now + 24h sliding window).
 * id_out: 37-byte (UUID) buffer filled with the constraint id (for RevokeObject if necessary later).
 * Caller must free() the returned string itself. Returns NULL on failure.
 * See: https://docs.s2standard.org/model-reference/PEBC/PEBC.EnergyConstraint/
 */
char *s2_build_energy_constraint(const char *quantity, const s2_pending_constraints_t *pc, time_t valid_until, char id_out[37], char msg_id_out[37]);

/**
 * Builds a PowerForecast message with a single element for the given quantity.
 * id_out: 37-byte (UUID) buffer filled with the message_id (for RevokeObject if necessary later).
 * Caller must free() the returned string itself. Returns NULL on failure.
 * See: https://docs.s2standard.org/model-reference/Common/PowerForecast/
 */
char *s2_build_power_forecast(const char *quantity, float value_expected, uint32_t duration_ms, char id_out[37]);

/**
 * Builds a PowerMeasurement message. 
 * IMPORTANT: timestamp = when the value was read from hardware.
 * Caller must free() the returned string. Returns NULL on failure.
 * See: https://docs.s2standard.org/model-reference/Common/PowerMeasurement/
 */
char *s2_build_power_measurement(const char *quantity, float value, time_t timestamp, char msg_id_out[37]);

/**
 * Builds an InstructionStatusUpdate message.
 * status_type: "NEW" / "ACCEPTED" / "REJECTED" / "STARTED" / "SUCCEEDED" / "ABORTED" / "REVOKED"
 * Caller must free() the returned string. Returns NULL on failure.
 * See: https://docs.s2standard.org/model-reference/Common/InstructionStatusUpdate/
 */
char *s2_build_instruction_status_update(const char *instruction_id, const char *status_type, char msg_id_out[37]);

/**
 * Builds a RevokeObject message for a previously sent S2 object.
 * object_type: the message_type string of the object being revoked (e.g. "PEBC.PowerConstraints").
 * Caller must free() the returned string itself. Returns NULL on failure.
 * See: https://docs.s2standard.org/model-reference/Common/RevokeObject/
 */
char *s2_build_revoke_object(const char *object_type, const char *object_id, char msg_id_out[37]);
