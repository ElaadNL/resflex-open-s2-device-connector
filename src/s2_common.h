// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <stdbool.h>
#include <time.h>
#include "s2_types.h"
#include "cJSON.h"
#include "esp_timer.h"

/**
 * @brief S2 node information
 */
typedef struct {
    char id[37];
    char brand[64];
    char logo_uri[128];
    char type[64];
    char model[64];
    char user_defined_name[64];
    s2_role role;
    s2_deployment deployment;
    char supported_versions[S2_NODE_MAX_SUPPORTED_VERSIONS][32];
} s2_node_t;

/**
 * @brief   Generates a RFC 4122 compliant UUID v4 string
 * 
 * @param   out Buffer of atleast 37 bytes the string will be stored in
 */
void s2_generate_uuid(char *out);

/**
 * Converts Unix timestamp (time_t) to ISO 8601 UTC (YYYY-MM-DDTHH:MM:SSZ) string format.
 * This is the standard S2 time format.
 * Returns false if formatting fails.
 */
bool s2_format_iso8601(time_t t, char *out, size_t len);

/**
 * Parses an ISO 8601 UTC string (YYYY-MM-DDTHH:MM:SS) to time_t.
 * Returns 0 on failure.
 */
time_t s2_parse_iso8601(const char *s);

/**
 * @brief   Stops and deletes a timer and clears its handle. Ignores a NULL handle, so it is
 *          safe to call before esp_timer_create() to make sure a previous timer is gone.
 *
 * @param   timer   Pointer to the handle, set to NULL on return
 */
void s2_timer_release(esp_timer_handle_t *timer);

/**
 * @brief   Validates if a string is a well-formed JSON object/array
 * 
 * @param   json_str    String that is validated
 * 
 * @return true if valid, false if invalid or NULL
 */
bool s2_is_valid_json(const char *json_str);

/**
 * @brief   Converts commodity enum to string
 * 
 * @param   commodity   Commodity enum value
 * 
 * @return  
 *      - String representation of the commodity, 
 *      - NULL if no mapping found
 */
const char *s2_commodity_to_string(s2_commodity_t commodity);

/**
 * @brief   Converts role type enum to string
 * 
 * @param   role_type   Role type enum value
 * 
 * @return  
 *      - String representation of the role type, 
 *      - NULL if no mapping found
 */
const char *s2_role_type_to_string(s2_role_type_t role_type);

/**
 * @brief   Converts commodity quantity enum to string
 * 
 * @param   quantity   Commodity quantity enum value
 * 
 * @return  
 *      - String representation of the commodity quantity
 *      - NULL if no mapping found
 */
const char *s2_commodity_quantity_to_string(s2_commodity_quantity_t quantity);

/**
 * @brief   Converts client role enum (int) to string
 *
 * @param   role Numeric role that should be converted
 *
 * @return  String representation of role
 */
char *s2_role_to_string(s2_role role);

/**
 * @brief   Converts client role string to enum (int)
 * 
 * @param   role Textual role that will be converted
 * @param   out Pointer to enum that will hold the result
 * 
 * @return  True if succeeded, false if not
 */
bool s2_role_to_enum(const char *role, s2_role *out);

/**
 * @brief   Converts client deployment enum (int) to string
 *
 * @param   deployment Numeric deployment that should be converted
 *
 * @return  String representation of deployment
 */
char *s2_deployment_to_string(s2_deployment deployment);

/**
 * @brief   Converts deployment string to enum (int)
 * 
 * @param   deployment Textual deployment that will be converted
 * @param   out Pointer to enum that will hold the result
 * 
 * @return  True if succeeded, false if not
 */
bool s2_deployment_to_enum(const char *deployment, s2_deployment *out);

/**
 * Converts S2 client node information to JSON object
 * 
 * NOTE:    Caller should free memory using cJSON_Delete() on success.
 *          On failure, memory is cleared in the function
 * 
 * @param   node Information about the S2 client node
 *
 * @return 
 *      - JSON object with node information that can be included in requests
 *      - NULL if failed
 */
cJSON *s2_node_to_json(s2_node_t node);

/** Safely get string length with upper bound. Returns false if unterminated or too long. */
bool get_bounded_strlen(const char *s, size_t max_len, size_t *out_len);