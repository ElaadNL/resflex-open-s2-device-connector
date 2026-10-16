// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

#include "s2_messages.h"
#include "s2_common.h"

#include "cJSON.h"
#include "esp_log.h"
#include <time.h>

static const char *TAG = "s2_messages";

/**
 * @brief Add optional string fields to RMD root if non-empty.
 */
static void rmd_add_optional_fields(cJSON *root, const s2_rm_config_t *rm) {
    if (rm->name[0] != '\0')             cJSON_AddStringToObject(root, "name", rm->name);
    if (rm->model[0] != '\0')            cJSON_AddStringToObject(root, "model", rm->model);
    if (rm->serial_number[0] != '\0')    cJSON_AddStringToObject(root, "serial_number", rm->serial_number);
    if (rm->firmware_version[0] != '\0') cJSON_AddStringToObject(root, "firmware_version", rm->firmware_version);
    if (rm->manufacturer[0] != '\0')     cJSON_AddStringToObject(root, "manufacturer", rm->manufacturer);
}

/**
 * @brief Build a single role object and append it to the roles array.
 *
 * @return false on out of memory.
 */
static bool rmd_add_role(cJSON *roles, const s2_role_t *r) {
    cJSON *role = cJSON_CreateObject();
    if (!role || !cJSON_AddItemToArray(roles, role)) return false;
    return cJSON_AddStringToObject(role, "commodity", s2_commodity_to_string(r->commodity)) &&
           cJSON_AddStringToObject(role, "role", s2_role_type_to_string(r->role_type));
}

/**
 * @brief Build one AllowedLimitRange object and append it to ranges.
 *
 * @return false on out of memory.
 */
static bool build_limit_range(cJSON *ranges, const char *quantity, const char *limit_type, bool abnormal_only, float start, float end) {
    cJSON *entry = cJSON_CreateObject();
    if (!entry || !cJSON_AddItemToArray(ranges, entry)) return false;
    if (!cJSON_AddStringToObject(entry, "commodity_quantity", quantity) ||
        !cJSON_AddStringToObject(entry, "limit_type", limit_type) ||
        !cJSON_AddBoolToObject(entry, "abnormal_condition_only", abnormal_only)) return false;
    cJSON *rb = cJSON_AddObjectToObject(entry, "range_boundary");
    return rb &&
           cJSON_AddNumberToObject(rb, "start_of_range", start) &&
           cJSON_AddNumberToObject(rb, "end_of_range", end);
}

char *s2_build_resource_manager_details(const s2_client_t *handle, char msg_id_out[37]) {
    // defined so cleanup can jump here
    char *result = NULL;

    cJSON *root = cJSON_CreateObject();
    // check that root exists (enough memory available for root JSON)
    if (!root) return NULL;

    s2_generate_uuid(msg_id_out);

    // all mandatory ResourceManagerDetails fields
    if (
        !cJSON_AddStringToObject(root, "message_type", "ResourceManagerDetails") ||
        !cJSON_AddStringToObject(root, "message_id", msg_id_out) ||
        !cJSON_AddStringToObject(root, "resource_id", handle->node.id) ||
        !cJSON_AddBoolToObject(root, "provides_forecast", handle->rm.provides_forecast) ||
        !cJSON_AddNumberToObject(root, "instruction_processing_delay", handle->rm.instruction_processing_delay_ms)
    ) goto cleanup; // if any are empty, we are out of memory so jump to cleanup and return null

    // add optional fields if the firmware set them during config
    rmd_add_optional_fields(root, &handle->rm);

    // Build roles array. example: [{"commodity": "ELECTRICITY", "role": "ENERGY_CONSUMER"}]
    cJSON *roles = cJSON_AddArrayToObject(root, "roles");
    if (!roles) goto cleanup;
    for (int i = 0; i < handle->rm.roles_count; i++)
        if (!rmd_add_role(roles, &handle->rm.roles[i])) goto cleanup;

    // init available_control_types and populate based on configured control type
    cJSON *control_types = cJSON_AddArrayToObject(root, "available_control_types");
    if (!control_types) goto cleanup;
    const char *ct_str = NULL;
    switch (handle->rm.control_type) {
        case S2_CONTROL_TYPE_PEBC: ct_str = "POWER_ENVELOPE_BASED_CONTROL"; break;
        case S2_CONTROL_TYPE_PPBC: ct_str = "POWER_PROFILE_BASED_CONTROL";  break;
        case S2_CONTROL_TYPE_OMBC: ct_str = "OPERATION_MODE_BASED_CONTROL"; break;
        case S2_CONTROL_TYPE_FRBC: ct_str = "FILL_RATE_BASED_CONTROL";      break;
        case S2_CONTROL_TYPE_DDBC: ct_str = "DEMAND_DRIVEN_BASED_CONTROL";  break;
        default: goto cleanup;
    }
    cJSON *ct_item = cJSON_CreateString(ct_str);
    if (!ct_item || !cJSON_AddItemToArray(control_types, ct_item)) goto cleanup;

    // init provides_power_measurement_types from rm.commodity_quantities
    cJSON *measurement_types = cJSON_AddArrayToObject(root, "provides_power_measurement_types");
    if (!measurement_types) goto cleanup;
    for (int i = 0; i < handle->rm.commodity_quantities_count; i++) {
        cJSON *qt = cJSON_CreateString(s2_commodity_quantity_to_string(handle->rm.commodity_quantities[i]));
        if (!qt || !cJSON_AddItemToArray(measurement_types, qt)) goto cleanup;
    }

    result = cJSON_PrintUnformatted(root);

// caller responsible for freeing the returned string once its done with it.
cleanup:
    if (!result) ESP_LOGE(TAG, "s2_build_resource_manager_details: Failed to build message");
    cJSON_Delete(root);
    return result;
}

char *s2_build_power_constraints(const char *quantity, const s2_pending_constraints_t *pc, bool upper_abn, bool lower_abn, time_t valid_until, bool defer, s2_pc_ids_t *ids_out) {
    char *result = NULL;
    char now[32];
    s2_format_iso8601(time(NULL), now, sizeof(now));

    cJSON *root = cJSON_CreateObject();
    if (!root) return NULL;

    // uuid
    char msg_id[37];
    s2_generate_uuid(msg_id);      // message_id: CEM references this in ReceptionStatus
    s2_generate_uuid(ids_out->id); // id: separate object id used for RevokeObject, must differ per spec

    // try build PEBC.PowerConstraints message with all required fields. If any fail, cleanup 
    // Very unlikely that firmware can provide valid_until for limits but the option is available.
    if (!cJSON_AddStringToObject(root, "message_type", "PEBC.PowerConstraints") ||
        !cJSON_AddStringToObject(root, "message_id", msg_id) ||
        !cJSON_AddStringToObject(root, "id", ids_out->id) ||
        !cJSON_AddStringToObject(root, "valid_from", now) ||
        !cJSON_AddStringToObject(root, "consequence_type", defer ? "DEFER" : "VANISH")) goto cleanup;

    // valid_until is optional for PowerConstraints: only include if config provides a window.
    if (valid_until) {
        char until[32];
        s2_format_iso8601(valid_until, until, sizeof(until));
        if (!cJSON_AddStringToObject(root, "valid_until", until)) goto cleanup;
    }

    cJSON *ranges = cJSON_AddArrayToObject(root, "allowed_limit_ranges");
    if (!ranges) goto cleanup;

    // upper and lower limit ranges: the boundaries within which CEM may instruct each limit
    if (!build_limit_range(ranges, quantity, "UPPER_LIMIT", upper_abn, pc->power_upper_min, pc->power_upper_max)) goto cleanup;
    if (!build_limit_range(ranges, quantity, "LOWER_LIMIT", lower_abn, pc->power_lower_min, pc->power_lower_max)) goto cleanup;

    // caller must free the returned string itself.
    result = cJSON_PrintUnformatted(root);
    if (result) strlcpy(ids_out->msg_id, msg_id, 37); // only set if build succeeded

cleanup:
    if (!result) ESP_LOGE(TAG, "s2_build_power_constraints: failed");
    cJSON_Delete(root);
    return result;
}

char *s2_build_energy_constraint(const char *quantity, const s2_pending_constraints_t *pc, time_t valid_until, char id_out[37], char msg_id_out[37]) {
    char *result = NULL;
    char now[32];
    char until[32];
    time_t t = time(NULL);
    s2_format_iso8601(t, now, sizeof(now));
    // valid_until is mandatory per spec. default = sliding 24h window from now.
    s2_format_iso8601(valid_until ? valid_until : t + 86400, until, sizeof(until));

    cJSON *root = cJSON_CreateObject();
    if (!root) return NULL;

    char msg_id[37];
    s2_generate_uuid(msg_id);      // message_id: CEM references this in ReceptionStatus
    s2_generate_uuid(id_out);      // id: separate object id used for RevokeObject, must differ per spec

    // energy_upper_max = highest average power the device may draw (upper bound) CEM uses this to plan power limit instruction ranges
    // energy_lower_min = lowest average power (could be negative for producers)
    // try build PEBC.EnergyConstraint message with all required fields. If any fail, cleanup
    if (!cJSON_AddStringToObject(root, "message_type", "PEBC.EnergyConstraint") ||
        !cJSON_AddStringToObject(root, "message_id", msg_id) ||
        !cJSON_AddStringToObject(root, "id", id_out) ||
        !cJSON_AddStringToObject(root, "commodity_quantity", quantity) ||
        !cJSON_AddStringToObject(root, "valid_from", now) ||
        !cJSON_AddStringToObject(root, "valid_until", until) ||
        !cJSON_AddNumberToObject(root, "upper_average_power", pc->energy_upper_max) ||
        !cJSON_AddNumberToObject(root, "lower_average_power", pc->energy_lower_min)) goto cleanup;

    result = cJSON_PrintUnformatted(root);
    if (result) strlcpy(msg_id_out, msg_id, 37); // only set if build succeeded

cleanup:
    if (!result) ESP_LOGE(TAG, "s2_build_energy_constraint: failed");
    cJSON_Delete(root);
    return result;
}

char *s2_build_power_forecast(const char *quantity, float value_expected, uint32_t duration_ms, char id_out[37]) {
    char *result = NULL;
    char now[32];
    s2_format_iso8601(time(NULL), now, sizeof(now));

    cJSON *root = cJSON_CreateObject();
    if (!root) return NULL;

    // PowerForecast has no separate "id" field, so id_out = message_id for tracking
    // (retry needed if ReceptionStatus with TEMPORARY_ERROR received from CEM)
    s2_generate_uuid(id_out);

    // try build PowerForecast message with all required fields. If any fail, cleanup
    if (!cJSON_AddStringToObject(root, "message_type", "PowerForecast") ||
        !cJSON_AddStringToObject(root, "message_id", id_out) ||
        !cJSON_AddStringToObject(root, "start_time", now)) goto cleanup;

    cJSON *elements = cJSON_AddArrayToObject(root, "elements");
    if (!elements) goto cleanup;

    cJSON *elem = cJSON_CreateObject();
    if (!elem || !cJSON_AddItemToArray(elements, elem)) goto cleanup;
    if (!cJSON_AddNumberToObject(elem, "duration", duration_ms)) goto cleanup;

    cJSON *pvs = cJSON_AddArrayToObject(elem, "power_values");
    if (!pvs) goto cleanup;

    cJSON *pv = cJSON_CreateObject();
    if (!pv || !cJSON_AddItemToArray(pvs, pv)) goto cleanup;
    if (!cJSON_AddStringToObject(pv, "commodity_quantity", quantity) ||
        !cJSON_AddNumberToObject(pv, "value_expected", value_expected)) goto cleanup;

    // caller must free the returned string itself.
    result = cJSON_PrintUnformatted(root);

cleanup:
    if (!result) ESP_LOGE(TAG, "s2_build_power_forecast: failed");
    cJSON_Delete(root);
    return result;
}

char *s2_build_power_measurement(const char *quantity, float value, time_t timestamp, char msg_id_out[37]) {
    char *result = NULL;
    char ts[32];
    s2_format_iso8601(timestamp, ts, sizeof(ts));

    cJSON *root = cJSON_CreateObject();
    if (!root) return NULL;

    s2_generate_uuid(msg_id_out);
    
    // try build PowerMeasurement message with all required fields. If any fail, cleanup
    if (!cJSON_AddStringToObject(root, "message_type", "PowerMeasurement") ||
        !cJSON_AddStringToObject(root, "message_id", msg_id_out) ||
        !cJSON_AddStringToObject(root, "measurement_timestamp", ts)) goto cleanup;

    cJSON *values = cJSON_AddArrayToObject(root, "values");
    if (!values) goto cleanup;
    cJSON *v = cJSON_CreateObject();
    if (!v || !cJSON_AddItemToArray(values, v)) goto cleanup;
    if (!cJSON_AddStringToObject(v, "commodity_quantity", quantity) ||
        !cJSON_AddNumberToObject(v, "value", value)) goto cleanup;

    result = cJSON_PrintUnformatted(root);

cleanup:
    if (!result) ESP_LOGE(TAG, "s2_build_power_measurement: failed");
    cJSON_Delete(root);
    return result;
}

char *s2_build_instruction_status_update(const char *instruction_id, const char *status_type, char msg_id_out[37]) {
    char *result = NULL;
    char now[32];
    s2_format_iso8601(time(NULL), now, sizeof(now));

    cJSON *root = cJSON_CreateObject();
    if (!root) return NULL;

    s2_generate_uuid(msg_id_out);

    // try build InstructionStatusUpdate message with all required fields. If any fail, cleanup
    if (!cJSON_AddStringToObject(root, "message_type", "InstructionStatusUpdate") ||
        !cJSON_AddStringToObject(root, "message_id", msg_id_out) ||
        !cJSON_AddStringToObject(root, "instruction_id", instruction_id) ||
        !cJSON_AddStringToObject(root, "status_type", status_type) ||
        !cJSON_AddStringToObject(root, "timestamp", now)) goto cleanup;

    result = cJSON_PrintUnformatted(root);

cleanup:
    if (!result) ESP_LOGE(TAG, "s2_build_instruction_status_update: failed");
    cJSON_Delete(root);
    return result;
}

char *s2_build_revoke_object(const char *object_type, const char *object_id, char msg_id_out[37]) {
    char *result = NULL;
    if (!object_type || !object_id) return NULL;
    s2_generate_uuid(msg_id_out);

    cJSON *root = cJSON_CreateObject();
    if (!root) return NULL;

    if (!cJSON_AddStringToObject(root, "message_type", "RevokeObject") ||
        !cJSON_AddStringToObject(root, "message_id", msg_id_out) ||
        !cJSON_AddStringToObject(root, "object_type", object_type) ||
        !cJSON_AddStringToObject(root, "object_id", object_id))
        goto cleanup;

    result = cJSON_PrintUnformatted(root);

cleanup:
    if (!result) ESP_LOGE(TAG, "s2_build_revoke_object: failed");
    cJSON_Delete(root);
    return result;
}
