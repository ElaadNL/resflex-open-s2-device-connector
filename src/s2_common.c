// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

#include "s2_common.h"
#include <string.h>
#include <stdio.h>
#include <time.h>
#include "cJSON.h"
#include "esp_random.h"
#include "esp_log.h"

static const char *TAG = "s2_common";

void s2_generate_uuid(char *out) {
    uint8_t uuid[16];

    // Fill buffer with random bytes
    esp_fill_random(uuid, 16);

    // Set UUID version to 4
    uuid[6] = 0x40 | (uuid[6] & 0xF);

    // Set UUID variant to RFC 4122
    uuid[8] = (uuid[8] & 0x3F) | 0x80;

    // Format as string: xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx
    snprintf(out, 37,
            "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
            uuid[0], uuid[1], uuid[2], uuid[3],
            uuid[4], uuid[5],
            uuid[6], uuid[7],
            uuid[8], uuid[9],
            uuid[10], uuid[11], uuid[12], uuid[13], uuid[14], uuid[15]);
}

bool s2_format_iso8601(time_t t, char *out, size_t len) {
    struct tm tm;
    gmtime_r(&t, &tm);
    return strftime(out, len, "%Y-%m-%dT%H:%M:%SZ", &tm) > 0;
}

time_t s2_parse_iso8601(const char *s) {
    int Y   = 0;
    int Mo  = 0;
    int D   = 0;
    int hh  = 0;
    int mm  = 0;
    int sec = 0;
    // field widths stop a long numeric run from overflowing the ints
    if (sscanf(s, "%4d-%2d-%2dT%2d:%2d:%2d", &Y, &Mo, &D, &hh, &mm, &sec) != 6) return 0;
    // every field is range-checked before use, Mo indexes mdays[] below
    if (Y < 1970 || Y > 9999) return 0;
    if (Mo < 1 || Mo > 12) return 0;
    if (D < 1 || D > 31) return 0;
    if (hh < 0 || hh > 23) return 0;
    if (mm < 0 || mm > 59) return 0;
    if (sec < 0 || sec > 60) return 0;
    // Compute UTC epoch directly to avoid mktime()'s local-TZ dependency.
    static const int8_t mdays[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    int days = (Y - 1970) * 365 + (Y - 1969) / 4 - (Y - 1901) / 100 + (Y - 1601) / 400;
    for (int i = 0; i < Mo - 1; i++) days += mdays[i];
    if (Mo > 2 && (Y % 4 == 0) && (Y % 100 != 0 || Y % 400 == 0)) days++;
    days += D - 1;
    return (time_t)days * 86400 + hh * 3600 + mm * 60 + sec;
}   

void s2_timer_release(esp_timer_handle_t *timer) {
    if (*timer == NULL) return;
    esp_timer_stop(*timer);
    esp_timer_delete(*timer);
    *timer = NULL;
}

bool s2_is_valid_json(const char *json_str) {
    if (json_str == NULL) return false;

    // Attempt to parse string to JSON
    cJSON *root = cJSON_Parse(json_str);

    if (!root) {
        const char *error_ptr = cJSON_GetErrorPtr();
        ESP_LOGE(TAG, "Invalid JSON: %s", error_ptr ? error_ptr : "Unknown error");
        return false;
    }

    cJSON_Delete(root);
    return true;
}

const char *s2_commodity_to_string(s2_commodity_t commodity) {
    switch (commodity) {
        case S2_COMMODITY_ELECTRICITY: return "ELECTRICITY";
        case S2_COMMODITY_GAS:         return "GAS";
        case S2_COMMODITY_HEAT:        return "HEAT";
        case S2_COMMODITY_OIL:         return "OIL";
        default:                       return NULL;
    }
}

const char *s2_role_type_to_string(s2_role_type_t role_type) {
    switch (role_type) {
        case S2_ROLE_TYPE_ENERGY_CONSUMER: return "ENERGY_CONSUMER";
        case S2_ROLE_TYPE_ENERGY_PRODUCER: return "ENERGY_PRODUCER";
        case S2_ROLE_TYPE_ENERGY_STORAGE:  return "ENERGY_STORAGE";
        default:                           return NULL;
    }
}

const char *s2_commodity_quantity_to_string(s2_commodity_quantity_t quantity) {
    switch (quantity) {
        case S2_COMMODITY_QUANTITY_ELECTRIC_POWER_L1:                return "ELECTRIC.POWER.L1";
        case S2_COMMODITY_QUANTITY_ELECTRIC_POWER_L2:                return "ELECTRIC.POWER.L2";
        case S2_COMMODITY_QUANTITY_ELECTRIC_POWER_L3:                return "ELECTRIC.POWER.L3";
        case S2_COMMODITY_QUANTITY_ELECTRIC_POWER_3_PHASE_SYMMETRIC: return "ELECTRIC.POWER.3_PHASE_SYMMETRIC";
        case S2_COMMODITY_QUANTITY_NATURAL_GAS_FLOW_RATE:            return "NATURAL_GAS.FLOW_RATE";
        case S2_COMMODITY_QUANTITY_HYDROGEN_FLOW_RATE:               return "HYDROGEN.FLOW_RATE";
        case S2_COMMODITY_QUANTITY_HEAT_THERMAL_POWER:               return "HEAT.THERMAL_POWER";
        case S2_COMMODITY_QUANTITY_HEAT_FLOW_RATE:                   return "HEAT.FLOW_RATE";
        case S2_COMMODITY_QUANTITY_HEAT_TEMPERATURE:                 return "HEAT.TEMPERATURE";
        case S2_COMMODITY_QUANTITY_OIL_FLOW_RATE:                    return "OIL.FLOW_RATE";
        default:                                                     return NULL;
    }
}

char *s2_role_to_string(s2_role role) {
    switch (role) {
        case S2_ROLE_CEM:
            return "CEM";
        case S2_ROLE_RM:
            return "RM";
        default:
            return NULL;
    }
}

bool s2_role_to_enum(const char *role, s2_role *out) {
    if (strcmp(role, "CEM") == 0) { *out = S2_ROLE_CEM; return true; }
    if (strcmp(role, "RM") == 0) { *out = S2_ROLE_RM; return true; }
    return false;
}

char *s2_deployment_to_string(s2_deployment deployment) {
    switch (deployment) {
        case S2_DEPLOYMENT_LAN:
            return "LAN";
        case S2_DEPLOYMENT_WAN:
            return "WAN";
        default:
            return NULL;
    }
}

bool s2_deployment_to_enum(const char *deployment, s2_deployment *out) {
    if (strcmp(deployment, "LAN") == 0) { *out = S2_DEPLOYMENT_LAN; return true; }
    if (strcmp(deployment, "WAN") == 0) { *out = S2_DEPLOYMENT_WAN; return true; }
    return false;
}

/**
 * @brief   Adds a string value or NULL if not defined to a JSON object
 * 
 * @param   root JSON object to add the string to
 * @param   key Object key linked to the value
 * @param   val String value to add to the object
 * 
 * @return true if succeeded, false if not
 */
static bool s2_add_string_or_null(cJSON *root, const char *key, const char *val) {
    if (val == NULL || val[0] == '\0') {
        return cJSON_AddNullToObject(root, key) != NULL;
    }
    return cJSON_AddStringToObject(root, key, val) != NULL;
}

cJSON *s2_node_to_json(s2_node_t node) {
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) return NULL;

    // Convert role enum to string
    char *role = s2_role_to_string(node.role);
    if (role == NULL) {
        ESP_LOGE(TAG, "s2_node_to_json: No mapping found for role");
        goto cleanup;
    }
    if (!cJSON_AddStringToObject(root, "role", role)) goto cleanup;
    
    // Convert deployment enum to string
    char *deployment = s2_deployment_to_string(node.deployment);
    if (deployment == NULL) {
        ESP_LOGE(TAG, "s2_node_to_json: No mapping found for deployment");
        goto cleanup;
    }
    
    // Construct versions array
    cJSON *versions_array = cJSON_CreateArray();
    if (versions_array == NULL) {
        ESP_LOGE(TAG, "s2_node_to_json: Failed to create JSON versions array");
        goto cleanup;
    }
    
    for (int i = 0; i < S2_NODE_MAX_SUPPORTED_VERSIONS; i++) {
        if (node.supported_versions[i][0] != '\0') {
            cJSON_AddItemToArray(versions_array, cJSON_CreateString(node.supported_versions[i]));
        }
    }

    // Add all node properties to the JSON object
    if (
        !cJSON_AddStringToObject(root, "id", node.id) ||
        !cJSON_AddStringToObject(root, "brand", node.brand) ||
        !s2_add_string_or_null(root, "logoUrl", node.logo_uri) ||
        !cJSON_AddStringToObject(root, "type", node.type) ||
        !cJSON_AddStringToObject(root, "modelName", node.model) ||
        !s2_add_string_or_null(root, "userDefinedName", node.user_defined_name) ||
        !cJSON_AddStringToObject(root, "role", role) ||
        !cJSON_AddStringToObject(root, "deployment", deployment) ||
        !cJSON_AddItemToObject(root, "supportedS2MessageVersions", versions_array)
    ) goto cleanup;

    return root;

cleanup:
    cJSON_Delete(root);
    return NULL;
}

bool get_bounded_strlen(const char *s, size_t max_len, size_t *out_len) {
    if (!s || !out_len || max_len == 0) return false;
    size_t n = strnlen(s, max_len);
    if (n == max_len) return false;
    *out_len = n;
    return true;
}
