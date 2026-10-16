// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

#include "s2_pebc_logic.h"
#include "s2_common.h"
#include "s2_messages.h"

#include "esp_log.h"
#include "esp_timer.h"
#include <math.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#define S2_MAX_TEXT_MESSAGE_LEN 16384

// the RM has no battery-backed clock: until time() is set it reads 1970, so treat any
// epoch below this as "never synced" rather than a real date
#define S2_CLOCK_MIN_VALID 1735689600LL // 2025-01-01

static const char *TAG = "s2_pebc_logic";
static const float LIMIT_EPSILON = 0.001f;

/**
 * @brief Find the index in rm.pebc.default_limits for a commodity quantity.
 *
 * @return Index, or -1 if not found.
 */
static int find_limit_slot(const s2_client_t *h, s2_commodity_quantity_t q) {
    for (int i = 0; i < h->rm.pebc.default_limits_count; i++)
        if (h->rm.pebc.default_limits[i].commodity_quantity == q) return i;
    return -1;
}

/**
 * @brief Send a RevokeObject for the given object type and id, then clear the id.
 */
static void send_revoke(s2_client_t *h, const char *type, char id[37]) {
    if (id[0] == '\0') return; // nothing sent yet, nothing to revoke
    char msg_id[37];
    char *body = s2_build_revoke_object(type, id, msg_id);
    if (body) {
        s2_websocket_send_tracked(h, body, msg_id);
        free(body);
    }
    // clear so we don't revoke again
    id[0] = '\0';
}

/**
 * @brief Send constraints for a slot once all 8 range values are ready.
 */
static void try_send_constraints(s2_client_t *h, int slot) {
    s2_pending_constraints_t *pc = &h->pending_constraints[slot];
    if (!pc->power_upper_max_ready || !pc->power_upper_min_ready ||
        !pc->power_lower_max_ready || !pc->power_lower_min_ready ||
        !pc->energy_upper_max_ready || !pc->energy_upper_min_ready ||
        !pc->energy_lower_max_ready || !pc->energy_lower_min_ready) return;

    s2_last_sent_t *ls = &h->last_sent[slot];
    const char *qty = s2_commodity_quantity_to_string(h->rm.pebc.default_limits[slot].commodity_quantity);
    char id[37];
    char msg_id[37];
    char *body;

    bool power_changed = fabsf(pc->power_upper_max - ls->power_upper_max) > 0.01f ||
                         fabsf(pc->power_upper_min - ls->power_upper_min) > 0.01f ||
                         fabsf(pc->power_lower_max - ls->power_lower_max) > 0.01f ||
                         fabsf(pc->power_lower_min - ls->power_lower_min) > 0.01f;
    // send PEBC.PowerConstraints if values changed OR there is no active object id (in case previous were revoked for example)
    if (power_changed || ls->power_constraints_id[0] == '\0') {
        bool upper_abn = h->rm.pebc.default_limits[slot].upper_limit_abnormal_only;
        bool lower_abn = h->rm.pebc.default_limits[slot].lower_limit_abnormal_only;
        s2_pc_ids_t pc_ids;
        body = s2_build_power_constraints(qty, pc, upper_abn, lower_abn,
                    h->rm.pebc.power_constraint_window_ms
                        ? time(NULL) + h->rm.pebc.power_constraint_window_ms / 1000
                        : 0, h->rm.pebc.power_constraint_defer, &pc_ids);
        if (body) {
            s2_websocket_send_tracked(h, body, pc_ids.msg_id); // track by message_id: CEM uses this in ReceptionStatus
            free(body);
            strlcpy(ls->power_constraints_id, pc_ids.id, sizeof(ls->power_constraints_id)); // store object id for RevokeObject
            ls->power_upper_max = pc->power_upper_max; ls->power_upper_min = pc->power_upper_min;
            ls->power_lower_max = pc->power_lower_max; ls->power_lower_min = pc->power_lower_min;
        }
    }

    // send PEBC.EnergyConstraint only if energy values changed.
    // ensure energy_upper_max <= power_upper_max (EnergyConstraint must not contradict PowerConstraints).
    float safe_energy_upper = pc->energy_upper_max < pc->power_upper_max ? pc->energy_upper_max : pc->power_upper_max;
    float safe_energy_lower = pc->energy_lower_min > pc->power_lower_min ? pc->energy_lower_min : pc->power_lower_min;
    bool energy_changed = fabsf(safe_energy_upper - ls->energy_upper_max) > 0.01f ||
                          fabsf(safe_energy_lower - ls->energy_lower_min) > 0.01f;
    if (energy_changed || ls->energy_constraint_id[0] == '\0') {
        float orig_upper = pc->energy_upper_max;
        float orig_lower = pc->energy_lower_min;
        // temporarily patch pending so builder uses the clamped values
        pc->energy_upper_max = safe_energy_upper;
        pc->energy_lower_min = safe_energy_lower;
        time_t energy_vu;
        if (pc->energy_valid_until)
            energy_vu = pc->energy_valid_until;
        else if (h->rm.pebc.energy_constraint_window_ms)
            energy_vu = time(NULL) + h->rm.pebc.energy_constraint_window_ms / 1000;
        else
            energy_vu = 0;
        body = s2_build_energy_constraint(qty, pc, energy_vu, id, msg_id);
        pc->energy_upper_max = orig_upper;
        pc->energy_lower_min = orig_lower;
        if (body) {
            s2_websocket_send_tracked(h, body, msg_id); // track by message_id: CEM uses this in ReceptionStatus
            free(body);
            strlcpy(ls->energy_constraint_id, id, sizeof(ls->energy_constraint_id)); // store object id for RevokeObject
            ls->energy_upper_max = safe_energy_upper;
            ls->energy_lower_min = safe_energy_lower;
        }
    }

    *pc = (s2_pending_constraints_t){0};
}

// reusable macro to store pending constraint values. calls try_send_constraints after each store
// so messages are sent as soon as all 8 values are in.
// active_constraint_slot routes to correct slot in case one commodity has abnormal and normal constraint
#define STORE_PENDING(field) \
    s2_client_t *h = (s2_client_t *)handle; \
    int slot = h->active_constraint_slot >= 0 ? h->active_constraint_slot : find_limit_slot(h, q); \
    if (slot < 0) return ESP_ERR_INVALID_ARG; \
    h->pending_constraints[slot].field##_ready = true; \
    h->pending_constraints[slot].field = value; \
    try_send_constraints(h, slot); \
    return ESP_OK;

esp_err_t s2_client_pebc_provide_energy_valid_until(s2_client_handle_t handle, s2_commodity_quantity_t quantity, time_t deadline) {
    s2_client_t *h = (s2_client_t *)handle;
    int slot = find_limit_slot(h, quantity);
    if (slot < 0) return ESP_ERR_INVALID_ARG;
    h->pending_constraints[slot].energy_valid_until = deadline;
    return ESP_OK;
}

esp_err_t s2_client_pebc_provide_power_upper_limit_max(s2_client_handle_t handle, s2_commodity_quantity_t q, float value) { STORE_PENDING(power_upper_max) }
esp_err_t s2_client_pebc_provide_power_upper_limit_min(s2_client_handle_t handle, s2_commodity_quantity_t q, float value) { STORE_PENDING(power_upper_min) }
esp_err_t s2_client_pebc_provide_power_lower_limit_max(s2_client_handle_t handle, s2_commodity_quantity_t q, float value) { STORE_PENDING(power_lower_max) }
esp_err_t s2_client_pebc_provide_power_lower_limit_min(s2_client_handle_t handle, s2_commodity_quantity_t q, float value) { STORE_PENDING(power_lower_min) }
esp_err_t s2_client_pebc_provide_energy_upper_limit_max(s2_client_handle_t handle, s2_commodity_quantity_t q, float value) { STORE_PENDING(energy_upper_max) }
esp_err_t s2_client_pebc_provide_energy_upper_limit_min(s2_client_handle_t handle, s2_commodity_quantity_t q, float value) { STORE_PENDING(energy_upper_min) }
esp_err_t s2_client_pebc_provide_energy_lower_limit_max(s2_client_handle_t handle, s2_commodity_quantity_t q, float value) { STORE_PENDING(energy_lower_max) }
esp_err_t s2_client_pebc_provide_energy_lower_limit_min(s2_client_handle_t handle, s2_commodity_quantity_t q, float value) { STORE_PENDING(energy_lower_min) }

/**
 * @brief Request power measurements from firmware on each interval.
 */
static void s2_measurement_timer_cb(void *arg) {
    s2_client_t *h = (s2_client_t *)arg;
    for (int i = 0; i < h->rm.commodity_quantities_count; i++)
        h->rm.on_request_measurement(h->rm.commodity_quantities[i]);
}

/**
 * @brief Request fresh power and energy constraints from firmware each tick.
 */
static void s2_constraints_timer_cb(void *arg) {
    s2_client_t *h = (s2_client_t *)arg;
    for (int i = 0; i < h->rm.pebc.default_limits_count; i++) {
        s2_commodity_quantity_t q = h->rm.pebc.default_limits[i].commodity_quantity;
        // is_abnormal is true when this slot is flagged abnormal_condition_only in config,
        // so firmware can return different (higher) limits for the abnormal slot.
        bool abn = h->rm.pebc.default_limits[i].upper_limit_abnormal_only ||
                   h->rm.pebc.default_limits[i].lower_limit_abnormal_only;
        h->active_constraint_slot = i;
        h->rm.pebc.on_request_param(S2_PEBC_PARAM_POWER_UPPER_LIMIT_MAX, q, abn);
        h->rm.pebc.on_request_param(S2_PEBC_PARAM_POWER_UPPER_LIMIT_MIN, q, abn);
        h->rm.pebc.on_request_param(S2_PEBC_PARAM_POWER_LOWER_LIMIT_MAX, q, abn);
        h->rm.pebc.on_request_param(S2_PEBC_PARAM_POWER_LOWER_LIMIT_MIN, q, abn);
        h->rm.pebc.on_request_param(S2_PEBC_PARAM_ENERGY_UPPER_LIMIT_MAX, q, abn);
        h->rm.pebc.on_request_param(S2_PEBC_PARAM_ENERGY_UPPER_LIMIT_MIN, q, abn);
        h->rm.pebc.on_request_param(S2_PEBC_PARAM_ENERGY_LOWER_LIMIT_MAX, q, abn);
        h->rm.pebc.on_request_param(S2_PEBC_PARAM_ENERGY_LOWER_LIMIT_MIN, q, abn);
        h->active_constraint_slot = -1;
    }
}

void s2_pebc_on_session_started(s2_client_t *h) {
    // timer works in microseconds so we multiply ms by 1000
    s2_timer_release(&h->timers.measurement);
    esp_timer_create_args_t args = { .callback = s2_measurement_timer_cb, .arg = h, .name = "s2_measurement" };
    esp_timer_create(&args, &h->timers.measurement);
    esp_timer_start_periodic(h->timers.measurement, (uint64_t)h->rm.measurement_interval_ms * 1000);

    s2_timer_release(&h->timers.constraints);
    args.callback = s2_constraints_timer_cb;
    args.name = "s2_constraints";
    esp_timer_create(&args, &h->timers.constraints);
    esp_timer_start_periodic(h->timers.constraints, (uint64_t)h->rm.pebc.constraint_interval_ms * 1000);

    // fire immediately so CEM does not wait a full interval after session becomes ready.
    s2_constraints_timer_cb(h);
    s2_measurement_timer_cb(h);
}

/**
 * @brief Cancel pending/active instruction timers and clear the id.
 */
static void cancel_instruction(s2_client_t *h) {
    s2_timer_release(&h->instruction.exec_timer);
    s2_timer_release(&h->instruction.duration_timer);
    h->instruction.id[0] = '\0';
}

/**
 * @brief Send a tracked InstructionStatusUpdate for the given instruction id and status.
 */
static void send_instruction_status(s2_client_t *h, const char *instr_id, const char *status) {
    char msg_id[37];
    char *body = s2_build_instruction_status_update(instr_id, status, msg_id);
    if (body) { s2_websocket_send_tracked(h, body, msg_id); free(body); }
}

/**
 * @brief Abort the active instruction (firmware error path).
 */
esp_err_t s2_client_pebc_abort_instruction(s2_client_handle_t handle) {
    s2_client_t *h = (s2_client_t *)handle;
    if (h->instruction.id[0] == '\0') return ESP_ERR_INVALID_STATE;
    char instr_id[37];
    strlcpy(instr_id, h->instruction.id, sizeof(instr_id));
    cancel_instruction(h);
    send_instruction_status(h, instr_id, "ABORTED");
    // resets all commodity quantities to default limits (skip abnormal slots)
    for (int i = 0; i < h->rm.pebc.default_limits_count; i++) {
        if (h->rm.pebc.default_limits[i].upper_limit_abnormal_only && h->rm.pebc.default_limits[i].lower_limit_abnormal_only)
            continue;
        h->rm.pebc.on_instruction(h->rm.pebc.default_limits[i].commodity_quantity,
                                  h->rm.pebc.default_limits[i].power_upper_limit,
                                  h->rm.pebc.default_limits[i].power_lower_limit);
    }
    return ESP_OK;
}

esp_err_t s2_client_pebc_revoke_constraints(s2_client_handle_t handle) {
    s2_client_t *h = (s2_client_t *)handle;
    for (int i = 0; i < h->rm.pebc.default_limits_count; i++) {
        s2_last_sent_t *ls = &h->last_sent[i];
        send_revoke(h, "PEBC.PowerConstraints", ls->power_constraints_id);
        send_revoke(h, "PEBC.EnergyConstraint", ls->energy_constraint_id);
    }
    return ESP_OK;
}

/**
 * @brief Apply limits from the given element index to each commodity quantity.
 */
static void apply_element(s2_client_t *h, uint8_t e) {
    for (int i = 0; i < h->rm.pebc.default_limits_count; i++) {
        if (h->rm.pebc.default_limits[i].upper_limit_abnormal_only && h->rm.pebc.default_limits[i].lower_limit_abnormal_only)
            continue;
        h->rm.pebc.on_instruction(h->rm.pebc.default_limits[i].commodity_quantity,
                                  h->instruction.elements[e].upper_limit[i],
                                  h->instruction.elements[e].lower_limit[i]);
    }
}

/**
 * @brief Fire when the current element duration expires.
 *
 * Chains to the next element or sends SUCCEEDED.
 */
static void s2_instruction_duration_cb(void *arg) {
    s2_client_t *h = (s2_client_t *)arg;
    s2_timer_release(&h->instruction.duration_timer);
    if (h->instruction.id[0] == '\0') return;

    char instr_id[37];
    strlcpy(instr_id, h->instruction.id, sizeof(instr_id));

    h->instruction.current_element++;
    if (h->instruction.current_element < h->instruction.element_count) {
        // still more elements: apply next and start its timer
        uint8_t e = h->instruction.current_element;
        apply_element(h, e);
        if (h->instruction.id[0] == '\0' || strcmp(h->instruction.id, instr_id) != 0) return;
        s2_timer_release(&h->instruction.duration_timer);
        esp_timer_create_args_t args = { .callback = s2_instruction_duration_cb, .arg = h, .name = "s2_instr_dur" };
        esp_timer_create(&args, &h->instruction.duration_timer);
        esp_timer_start_once(h->instruction.duration_timer, (uint64_t)h->instruction.elements[e].duration_ms * 1000);
    } else {
        // last element done: SUCCEEDED + reset device to default limits
        if (h->instruction.id[0] == '\0' || strcmp(h->instruction.id, instr_id) != 0) return;
        send_instruction_status(h, instr_id, "SUCCEEDED");
        for (int i = 0; i < h->rm.pebc.default_limits_count; i++) {
            if (h->rm.pebc.default_limits[i].upper_limit_abnormal_only && h->rm.pebc.default_limits[i].lower_limit_abnormal_only)
                continue;
            h->rm.pebc.on_instruction(h->rm.pebc.default_limits[i].commodity_quantity,
                                      h->rm.pebc.default_limits[i].power_upper_limit,
                                      h->rm.pebc.default_limits[i].power_lower_limit);
        }
        h->instruction.id[0] = '\0';
    }
}

/**
 * @brief Fire at execution_time.
 *
 * Applies current element limits, sends STARTED, and starts the duration timer.
 */
static void s2_instruction_exec_cb(void *arg) {
    s2_client_t *h = (s2_client_t *)arg;
    s2_timer_release(&h->instruction.exec_timer);
    if (h->instruction.id[0] == '\0') return;

    char instr_id[37];
    strlcpy(instr_id, h->instruction.id, sizeof(instr_id));

    uint8_t e = h->instruction.current_element;
    apply_element(h, e);
    if (h->instruction.id[0] == '\0' || strcmp(h->instruction.id, instr_id) != 0) return;

    send_instruction_status(h, instr_id, "STARTED");
    s2_timer_release(&h->instruction.duration_timer);
    esp_timer_create_args_t args = { .callback = s2_instruction_duration_cb, .arg = h, .name = "s2_instr_dur" };
    esp_timer_create(&args, &h->instruction.duration_timer);
    esp_timer_start_once(h->instruction.duration_timer, (uint64_t)h->instruction.elements[e].duration_ms * 1000);
}

/**
 * @brief Init temp envelope elements with default limits.
 */
static void init_tmp_from_defaults(s2_client_t *h, s2_instruction_element_t *tmp) {
    for (int e = 0; e < S2_PEBC_MAX_ENVELOPE_ELEMENTS; e++) {
        tmp[e].duration_ms = 0;
        for (int i = 0; i < h->rm.pebc.default_limits_count; i++) {
            tmp[e].upper_limit[i] = h->rm.pebc.default_limits[i].power_upper_limit;
            tmp[e].lower_limit[i] = h->rm.pebc.default_limits[i].power_lower_limit;
        }
    }
}

typedef struct {
    float ul_min;
    float ul_max;
    float ll_min;
    float ll_max;
} s2_limits_range_t;

static esp_err_t validate_envelope_shape(cJSON *qty_j, cJSON *elems_j, uint8_t *elem_count) {
    if (!cJSON_IsString(qty_j) || !cJSON_IsArray(elems_j)) return ESP_ERR_INVALID_ARG;
    int n = cJSON_GetArraySize(elems_j);
    if (n == 0 || n > S2_PEBC_MAX_ENVELOPE_ELEMENTS) return ESP_ERR_INVALID_ARG;
    if (*elem_count == 0) { *elem_count = (uint8_t)n; return ESP_OK; }
    return *elem_count == (uint8_t)n ? ESP_OK : ESP_ERR_INVALID_ARG;
}

static int find_slot_for_quantity(s2_client_t *h, const char *qty) {
    for (int i = 0; i < h->rm.pebc.default_limits_count; i++) {
        if (strcmp(s2_commodity_quantity_to_string(h->rm.pebc.default_limits[i].commodity_quantity), qty) == 0)
            return i;
    }
    return -1;
}

static s2_limits_range_t compute_limits_range(s2_client_t *h, int slot, bool abn) {
    s2_last_sent_t *ls = &h->last_sent[slot];
    s2_limits_range_t range = {
        .ul_min = ls->power_upper_min,
        .ul_max = ls->power_upper_max,
        .ll_min = ls->power_lower_min,
        .ll_max = ls->power_lower_max,
    };
    if (!abn) return range;
    for (int j = 0; j < h->rm.pebc.default_limits_count; j++) {
        if (j == slot) continue;
        if (h->rm.pebc.default_limits[j].commodity_quantity != h->rm.pebc.default_limits[slot].commodity_quantity) continue;
        s2_last_sent_t *ls2 = &h->last_sent[j];
        if (ls2->power_upper_min < range.ul_min) range.ul_min = ls2->power_upper_min;
        if (ls2->power_upper_max > range.ul_max) range.ul_max = ls2->power_upper_max;
        if (ls2->power_lower_min < range.ll_min) range.ll_min = ls2->power_lower_min;
        if (ls2->power_lower_max > range.ll_max) range.ll_max = ls2->power_lower_max;
    }
    return range;
}

static esp_err_t parse_envelope_element(cJSON *elem, float *ul, float *ll, uint32_t *duration_ms) {
    cJSON *ul_j = cJSON_GetObjectItem(elem, "upper_limit");
    cJSON *ll_j = cJSON_GetObjectItem(elem, "lower_limit");
    cJSON *dur_j = cJSON_GetObjectItem(elem, "duration");
    if (!cJSON_IsNumber(ul_j) || !cJSON_IsNumber(ll_j) || !cJSON_IsNumber(dur_j)) return ESP_ERR_INVALID_ARG;
    // range-check the duration before the cast: a negative or huge value would truncate to nonsense
    if (dur_j->valuedouble < 0 || dur_j->valuedouble > S2_PEBC_MAX_ELEMENT_DURATION_MS) return ESP_ERR_INVALID_ARG;
    *ul = (float)ul_j->valuedouble;
    *ll = (float)ll_j->valuedouble;
    *duration_ms = (uint32_t)dur_j->valuedouble;
    return ESP_OK;
}

static bool instruction_limits_invalid(float ul, float ll, bool abn, bool upper_abn_only, bool lower_abn_only,
                                       const s2_limits_range_t *range) {
    if (ll > ul + LIMIT_EPSILON) return true;
    if (upper_abn_only && !abn) return true;
    if (lower_abn_only && !abn) return true;
    if (ul < range->ul_min - LIMIT_EPSILON || ul > range->ul_max + LIMIT_EPSILON) return true;
    if (ll < range->ll_min - LIMIT_EPSILON || ll > range->ll_max + LIMIT_EPSILON) return true;
    return false;
}

/**
 * @brief Validate one envelope and write accepted elements into tmp.
 *
 * @return ESP_ERR_INVALID_ARG to reject, ESP_OK to continue.
 */
static esp_err_t validate_envelope(s2_client_t *h, cJSON *env, bool abn,
                                   s2_instruction_element_t *tmp, uint8_t *elem_count,
                                   const char *instr_id) {
    cJSON *qty_j   = cJSON_GetObjectItem(env, "commodity_quantity");
    cJSON *elems_j = cJSON_GetObjectItem(env, "power_envelope_elements");
    if (validate_envelope_shape(qty_j, elems_j, elem_count) != ESP_OK) return ESP_ERR_INVALID_ARG;

    int slot = find_slot_for_quantity(h, qty_j->valuestring);
    if (slot < 0) return ESP_ERR_INVALID_ARG;

    bool upper_abn_only = h->rm.pebc.default_limits[slot].upper_limit_abnormal_only;
    bool lower_abn_only = h->rm.pebc.default_limits[slot].lower_limit_abnormal_only;
    s2_limits_range_t range = compute_limits_range(h, slot, abn);

    cJSON *elem;
    int e = 0;
    cJSON_ArrayForEach(elem, elems_j) {
        float ul = 0.0f;
        float ll = 0.0f;
        uint32_t duration_ms = 0;
        if (parse_envelope_element(elem, &ul, &ll, &duration_ms) != ESP_OK) return ESP_ERR_INVALID_ARG;
        if (instruction_limits_invalid(ul, ll, abn, upper_abn_only, lower_abn_only, &range)) {
            ESP_LOGW(TAG, "PEBC.Instruction %s: slot %d elem %d out of range", instr_id, slot, e);
            return ESP_ERR_INVALID_ARG;
        }
        tmp[e].upper_limit[slot] = ul;
        tmp[e].lower_limit[slot] = ll;
        if (!tmp[e].duration_ms) tmp[e].duration_ms = duration_ms; // store duration from first element that has one. All elements must share the same duration
        e++;
    }
    return ESP_OK;
}

/**
 * @brief Schedule or immediately execute the stored instruction.
 */
static esp_err_t schedule_instruction(s2_client_t *h, const char *instr_id) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    int64_t now_ms  = tv.tv_sec * 1000LL + tv.tv_usec / 1000LL;
    int64_t exec_ms = h->instruction.execution_time * 1000LL;
    uint8_t elem_count = h->instruction.element_count;

    // clock never synced: run now, a timer out to the CEM's current year execution_time would never fire
    if (tv.tv_sec < S2_CLOCK_MIN_VALID) {
        ESP_LOGW(TAG, "PEBC.Instruction %s: execution_time %lldh ahead, clock not set, running now",
                 instr_id, (long long)((exec_ms - now_ms) / (60 * 60 * 1000)));
        now_ms = exec_ms;
    }

    if (exec_ms > now_ms) {
        // future: schedule timer to fire at execution_time
        s2_timer_release(&h->instruction.exec_timer);
        esp_timer_create_args_t args = { .callback = s2_instruction_exec_cb, .arg = h, .name = "s2_instr_exec" };
        esp_timer_create(&args, &h->instruction.exec_timer);
        if (esp_timer_start_once(h->instruction.exec_timer, (uint64_t)(exec_ms - now_ms) * 1000ULL) == ESP_OK) {
            return ESP_OK;
        }
        // timer will not arm: run now rather than leave the limits unapplied
        ESP_LOGW(TAG, "PEBC.Instruction %s: could not schedule, running now", instr_id);
        now_ms = exec_ms;
    }

    // past: skip expired instruction elements and trim the remaining duration of the active instruction element
    int64_t t_ms = exec_ms;
    uint8_t e = 0;
    while (e < elem_count) {
        int64_t end_ms = t_ms + (int64_t)h->instruction.elements[e].duration_ms;
        if (end_ms > now_ms) {
            h->instruction.elements[e].duration_ms = (uint32_t)(end_ms - now_ms);
            break;
        }
        t_ms = end_ms;
        e++;
    }
    if (e >= elem_count) {
        send_instruction_status(h, instr_id, "REJECTED");
        return ESP_FAIL;
    }
    h->instruction.current_element = e;
    s2_instruction_exec_cb(h);
    return ESP_OK;
}

/**
 * @brief Process a PEBC.Instruction from the CEM.
 *
 * Validates, stores, executes, then sends InstructionStatusUpdate.
 */
esp_err_t s2_pebc_handle_instruction(s2_client_t *h, cJSON *msg) {
    // grab all fields from incoming JSON
    cJSON *id_j    = cJSON_GetObjectItem(msg, "id");
    cJSON *abn_j   = cJSON_GetObjectItem(msg, "abnormal_condition");
    cJSON *pc_id_j = cJSON_GetObjectItem(msg, "power_constraints_id");
    cJSON *exec_j  = cJSON_GetObjectItem(msg, "execution_time");
    cJSON *envs_j  = cJSON_GetObjectItem(msg, "power_envelopes");
    if (!cJSON_IsString(id_j) || !cJSON_IsBool(abn_j) || !cJSON_IsString(pc_id_j) ||
        !cJSON_IsString(exec_j) || !cJSON_IsArray(envs_j))
        return ESP_ERR_INVALID_ARG;

    const char *instr_id = id_j->valuestring;
    send_instruction_status(h, instr_id, "NEW");

    ESP_LOGD(TAG, "PEBC.Instruction %s: power_constraints_id=%s", instr_id, pc_id_j->valuestring);

    bool abn = cJSON_IsTrue(abn_j);
    // validate all envelopes into a temporary array before touching any live instruction state
    s2_instruction_element_t tmp[S2_PEBC_MAX_ENVELOPE_ELEMENTS] = {0};
    uint8_t elem_count = 0;
    init_tmp_from_defaults(h, tmp);

    cJSON *env;
    cJSON_ArrayForEach(env, envs_j) {
        if (validate_envelope(h, env, abn, tmp, &elem_count, instr_id) != ESP_OK)
            { send_instruction_status(h, instr_id, "REJECTED"); return ESP_OK; }
    }
    if (!elem_count) { send_instruction_status(h, instr_id, "REJECTED"); return ESP_OK; }

    time_t exec_time = s2_parse_iso8601(exec_j->valuestring);
    if (exec_time == 0 || exec_time == (time_t)-1) {
        ESP_LOGW(TAG, "PEBC.Instruction %s: invalid execution_time '%s', rejecting", instr_id, exec_j->valuestring);
        send_instruction_status(h, instr_id, "REJECTED");
        return ESP_OK;
    }

    // replace any existing pending/active instruction, then store the new one
    if (h->instruction.id[0] != '\0') cancel_instruction(h);
    strlcpy(h->instruction.id, instr_id, sizeof(h->instruction.id));
    h->instruction.element_count = elem_count;
    h->instruction.current_element = 0;
    h->instruction.execution_time = exec_time;
    for (uint8_t e = 0; e < elem_count; e++) h->instruction.elements[e] = tmp[e];

    send_instruction_status(h, h->instruction.id, "ACCEPTED");
    schedule_instruction(h, h->instruction.id);
    return ESP_OK;
}

esp_err_t s2_pebc_handle_revoke_object(s2_client_t *h, cJSON *msg) {
    cJSON *type_j = cJSON_GetObjectItem(msg, "object_type");
    cJSON *id_j   = cJSON_GetObjectItem(msg, "object_id");
    if (!cJSON_IsString(type_j) || !cJSON_IsString(id_j)) return ESP_ERR_INVALID_ARG;

    // CEM can revoke an active or pending instruction: cancel timers and reset device to defaults.
    // we can only have 1 active/pending instruction at a time so we always revert to default settings if the instruction is revoked.
    if (strcmp(type_j->valuestring, "PEBC.Instruction") == 0 &&
        h->instruction.id[0] != '\0' &&
        strcmp(h->instruction.id, id_j->valuestring) == 0) {
        char instr_id[37];
        strlcpy(instr_id, h->instruction.id, sizeof(instr_id));
        cancel_instruction(h); // clears instruction and timers
        send_instruction_status(h, instr_id, "REVOKED");
        // set all commodity quantities back to default limits (skip abnormal slots)
        for (int i = 0; i < h->rm.pebc.default_limits_count; i++) {
            if (h->rm.pebc.default_limits[i].upper_limit_abnormal_only && h->rm.pebc.default_limits[i].lower_limit_abnormal_only)
                continue;
            h->rm.pebc.on_instruction(h->rm.pebc.default_limits[i].commodity_quantity,
                                      h->rm.pebc.default_limits[i].power_upper_limit,
                                      h->rm.pebc.default_limits[i].power_lower_limit);
        }
    }
    return ESP_OK;
}
