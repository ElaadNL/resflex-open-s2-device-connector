// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

#include "unity.h"
#include "s2_pebc_logic.h"
#include "s2_client_internal.h"
#include "Mockqueue.h"
#include "MockcJSON_mock.h"
#include "Mockesp_timer.h"
#include "json_stubs.h"

// ______________________ s2_pebc_handle_instruction ______________________

// Test missing/invalid arguments in s2_pebc_handle_instruction.
TEST_CASE("Returns invalid arg if required JSON fields are missing", "[s2_pebc_logic - handle_instruction]") {
    s2_client_t h = {
        .rm = {0},
    };
    cJSON msg = {0};

    // All cJSON_GetObjectItem calls return NULL (no fields present)
    cJSON_GetObjectItem_IgnoreAndReturn(NULL);
    // cJSON_IsString returns false - short-circuits before IsBool/IsArray are reached
    cJSON_IsString_IgnoreAndReturn(0);

    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, s2_pebc_handle_instruction(&h, &msg));
}

// Returns OK and sends NEW + REJECTED when power_constraints_id is not in last_sent.
TEST_CASE("Returns OK and rejects instruction with unknown constraints id", "[s2_pebc_logic - handle_instruction]") {
    cJSON fake = {.valuestring = "test-id"};
    s2_client_t h = {0}; // last_sent zeroed = no matching constraints id
    cJSON msg = {0};

    cJSON_GetObjectItem_IgnoreAndReturn(&fake);
    cJSON_IsString_IgnoreAndReturn(1);
    cJSON_IsBool_IgnoreAndReturn(1);
    cJSON_IsArray_IgnoreAndReturn(1);
    cJSON_IsTrue_IgnoreAndReturn(0);
    cJSON_build_init(); // send_instruction_status is called for NEW then REJECTED
    xQueueSemaphoreTake_IgnoreAndReturn(pdTRUE);
    xQueueGenericSend_IgnoreAndReturn(pdTRUE);

    TEST_ASSERT_EQUAL(ESP_OK, s2_pebc_handle_instruction(&h, &msg));
}

// ______________________ s2_client_pebc_abort_instruction ______________________

// Returns invalid state if no active instruction (id is empty).
TEST_CASE("Returns invalid state if no active instruction", "[s2_pebc_logic - abort_instruction]") {
    s2_client_t h = {0}; // instruction.id[0] = '\0'
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, s2_client_pebc_abort_instruction(&h));
}

// Aborts active instruction: sends ABORTED status and calls on_instruction for each non-abnormal slot.
TEST_CASE("Aborts instruction, sends ABORTED, restores defaults", "[s2_pebc_logic - abort_instruction]") {
    static s2_commodity_quantity_t on_instr_qty;
    static float on_instr_upper, on_instr_lower;
    void on_instr(s2_commodity_quantity_t q, float up, float lo) {
        on_instr_qty = q; on_instr_upper = up; on_instr_lower = lo;
    }
    s2_client_t h = {
        .instruction.id = "some-id",
        .rm.pebc = {
            .default_limits_count = 1,
            .default_limits = {{
                .commodity_quantity = S2_COMMODITY_QUANTITY_ELECTRIC_POWER_L1,
                .power_upper_limit = 10.0f,
                .power_lower_limit = -10.0f,
            }},
            .on_instruction = on_instr,
        },
    };
    cJSON_build_init();
    xQueueSemaphoreTake_IgnoreAndReturn(pdTRUE);
    xQueueGenericSend_IgnoreAndReturn(pdTRUE);

    TEST_ASSERT_EQUAL(ESP_OK, s2_client_pebc_abort_instruction(&h));
    TEST_ASSERT_EQUAL('\0', h.instruction.id[0]); // id cleared
    TEST_ASSERT_EQUAL(S2_COMMODITY_QUANTITY_ELECTRIC_POWER_L1, on_instr_qty);
    TEST_ASSERT_EQUAL_FLOAT(10.0f, on_instr_upper);
    TEST_ASSERT_EQUAL_FLOAT(-10.0f, on_instr_lower);
}

// ______________________ s2_client_pebc_provide_* (STORE_PENDING) ______________________

// Returns invalid arg if quantity is not in default_limits.
TEST_CASE("Returns invalid arg if quantity is not in default_limits", "[s2_pebc_logic - provide_power_upper_limit_max]") {
    s2_client_t h = {.active_constraint_slot = -1}; // no active slot override, so lookup by quantity
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, s2_client_pebc_provide_power_upper_limit_max(&h, S2_COMMODITY_QUANTITY_ELECTRIC_POWER_L1, 1.0f));
}

// Stores value in pending_constraints. With only one of 8 values set, no message is sent yet.
TEST_CASE("Stores power_upper_max in pending, no send until all 8 ready", "[s2_pebc_logic - provide_power_upper_limit_max]") {
    s2_client_t h = {
        .rm.pebc.default_limits_count = 1,
        .rm.pebc.default_limits = {{.commodity_quantity = S2_COMMODITY_QUANTITY_ELECTRIC_POWER_L1}},
        .active_constraint_slot = -1,
    };
    TEST_ASSERT_EQUAL(ESP_OK, s2_client_pebc_provide_power_upper_limit_max(&h, S2_COMMODITY_QUANTITY_ELECTRIC_POWER_L1, 5.0f));
    TEST_ASSERT_EQUAL_FLOAT(5.0f, h.pending_constraints[0].power_upper_max);
    TEST_ASSERT_TRUE(h.pending_constraints[0].power_upper_max_ready);
}

// ______________________ s2_pebc_handle_revoke_object ______________________

TEST_CASE("Returns invalid arg if required JSON fields are missing", "[s2_pebc_logic - handle_revoke_object]") {
    s2_client_t h = {0};
    cJSON msg = {0};
    cJSON_GetObjectItem_IgnoreAndReturn(NULL);
    cJSON_IsString_IgnoreAndReturn(0);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, s2_pebc_handle_revoke_object(&h, &msg));
}

// No active instruction with matching id: returns OK, does nothing.
TEST_CASE("Returns OK and does nothing if instruction id does not match", "[s2_pebc_logic - handle_revoke_object]") {
    cJSON type_j = {.valuestring = "PEBC.Instruction"};
    cJSON id_j   = {.valuestring = "other-id"};
    s2_client_t h = {.instruction.id = "active-id"};
    cJSON msg = {0};
    cJSON_GetObjectItem_ExpectAnyArgsAndReturn(&type_j);
    cJSON_GetObjectItem_ExpectAnyArgsAndReturn(&id_j);
    cJSON_IsString_IgnoreAndReturn(1);
    TEST_ASSERT_EQUAL(ESP_OK, s2_pebc_handle_revoke_object(&h, &msg));
    TEST_ASSERT_EQUAL_STRING("active-id", h.instruction.id); // unchanged
}

// Active instruction with matching id: clears it, sends REVOKED, restores defaults.
TEST_CASE("Revokes matching instruction, restores defaults", "[s2_pebc_logic - handle_revoke_object]") {
    static s2_commodity_quantity_t on_instr_qty;
    void on_instr(s2_commodity_quantity_t q, float up, float lo) { on_instr_qty = q; }
    cJSON type_j = {.valuestring = "PEBC.Instruction"};
    cJSON id_j   = {.valuestring = "active-id"};
    s2_client_t h = {
        .instruction.id = "active-id",
        .rm.pebc = {
            .default_limits_count = 1,
            .default_limits = {{.commodity_quantity = S2_COMMODITY_QUANTITY_ELECTRIC_POWER_L1}},
            .on_instruction = on_instr,
        },
    };
    cJSON msg = {0};
    cJSON_GetObjectItem_ExpectAnyArgsAndReturn(&type_j);
    cJSON_GetObjectItem_ExpectAnyArgsAndReturn(&id_j);
    cJSON_IsString_IgnoreAndReturn(1);
    cJSON_build_init();
    xQueueSemaphoreTake_IgnoreAndReturn(pdTRUE);
    xQueueGenericSend_IgnoreAndReturn(pdTRUE);
    TEST_ASSERT_EQUAL(ESP_OK, s2_pebc_handle_revoke_object(&h, &msg));
    TEST_ASSERT_EQUAL('\0', h.instruction.id[0]);
    TEST_ASSERT_EQUAL(S2_COMMODITY_QUANTITY_ELECTRIC_POWER_L1, on_instr_qty);
}

// ______________________ s2_client_pebc_revoke_constraints ______________________

// No constraints sent yet (ids empty): returns OK without sending anything.
TEST_CASE("Returns OK and sends nothing if no constraints were sent", "[s2_pebc_logic - revoke_constraints]") {
    s2_client_t h = {
        .rm.pebc.default_limits_count = 1, // last_sent zeroed = empty ids
    };
    TEST_ASSERT_EQUAL(ESP_OK, s2_client_pebc_revoke_constraints(&h));
}

// Constraints previously sent: sends RevokeObject for each and clears the stored ids.
TEST_CASE("Sends RevokeObject for each active constraint and clears ids", "[s2_pebc_logic - revoke_constraints]") {
    s2_client_t h = {
        .rm.pebc.default_limits_count = 1,
        .last_sent = {{
            .power_constraints_id = "pc-id",
            .energy_constraint_id = "ec-id",
        }},
    };
    cJSON_build_init();
    xQueueSemaphoreTake_IgnoreAndReturn(pdTRUE);
    xQueueGenericSend_IgnoreAndReturn(pdTRUE);
    TEST_ASSERT_EQUAL(ESP_OK, s2_client_pebc_revoke_constraints(&h));
    TEST_ASSERT_EQUAL('\0', h.last_sent[0].power_constraints_id[0]); // cleared after revoke
    TEST_ASSERT_EQUAL('\0', h.last_sent[0].energy_constraint_id[0]);
}

// ______________________ s2_client_pebc_provide_* STORE_PENDING tests ______________________

#define H_WITH_SLOT { \
    .rm.pebc.default_limits_count = 1, \
    .rm.pebc.default_limits = {{.commodity_quantity = S2_COMMODITY_QUANTITY_ELECTRIC_POWER_L1}}, \
    .active_constraint_slot = -1, \
}

TEST_CASE("Stores power_upper_min in pending", "[s2_pebc_logic - provide_power_upper_limit_min]") {
    s2_client_t h = H_WITH_SLOT;
    TEST_ASSERT_EQUAL(ESP_OK, s2_client_pebc_provide_power_upper_limit_min(&h, S2_COMMODITY_QUANTITY_ELECTRIC_POWER_L1, 1.0f));
    TEST_ASSERT_EQUAL_FLOAT(1.0f, h.pending_constraints[0].power_upper_min);
    TEST_ASSERT_TRUE(h.pending_constraints[0].power_upper_min_ready);
}

TEST_CASE("Stores power_lower_max in pending", "[s2_pebc_logic - provide_power_lower_limit_max]") {
    s2_client_t h = H_WITH_SLOT;
    TEST_ASSERT_EQUAL(ESP_OK, s2_client_pebc_provide_power_lower_limit_max(&h, S2_COMMODITY_QUANTITY_ELECTRIC_POWER_L1, 2.0f));
    TEST_ASSERT_EQUAL_FLOAT(2.0f, h.pending_constraints[0].power_lower_max);
    TEST_ASSERT_TRUE(h.pending_constraints[0].power_lower_max_ready);
}

TEST_CASE("Stores power_lower_min in pending", "[s2_pebc_logic - provide_power_lower_limit_min]") {
    s2_client_t h = H_WITH_SLOT;
    TEST_ASSERT_EQUAL(ESP_OK, s2_client_pebc_provide_power_lower_limit_min(&h, S2_COMMODITY_QUANTITY_ELECTRIC_POWER_L1, 3.0f));
    TEST_ASSERT_EQUAL_FLOAT(3.0f, h.pending_constraints[0].power_lower_min);
    TEST_ASSERT_TRUE(h.pending_constraints[0].power_lower_min_ready);
}

TEST_CASE("Stores energy_upper_max in pending", "[s2_pebc_logic - provide_energy_upper_limit_max]") {
    s2_client_t h = H_WITH_SLOT;
    TEST_ASSERT_EQUAL(ESP_OK, s2_client_pebc_provide_energy_upper_limit_max(&h, S2_COMMODITY_QUANTITY_ELECTRIC_POWER_L1, 4.0f));
    TEST_ASSERT_EQUAL_FLOAT(4.0f, h.pending_constraints[0].energy_upper_max);
    TEST_ASSERT_TRUE(h.pending_constraints[0].energy_upper_max_ready);
}

TEST_CASE("Stores energy_upper_min in pending", "[s2_pebc_logic - provide_energy_upper_limit_min]") {
    s2_client_t h = H_WITH_SLOT;
    TEST_ASSERT_EQUAL(ESP_OK, s2_client_pebc_provide_energy_upper_limit_min(&h, S2_COMMODITY_QUANTITY_ELECTRIC_POWER_L1, 5.0f));
    TEST_ASSERT_EQUAL_FLOAT(5.0f, h.pending_constraints[0].energy_upper_min);
    TEST_ASSERT_TRUE(h.pending_constraints[0].energy_upper_min_ready);
}

TEST_CASE("Stores energy_lower_max in pending", "[s2_pebc_logic - provide_energy_lower_limit_max]") {
    s2_client_t h = H_WITH_SLOT;
    TEST_ASSERT_EQUAL(ESP_OK, s2_client_pebc_provide_energy_lower_limit_max(&h, S2_COMMODITY_QUANTITY_ELECTRIC_POWER_L1, 6.0f));
    TEST_ASSERT_EQUAL_FLOAT(6.0f, h.pending_constraints[0].energy_lower_max);
    TEST_ASSERT_TRUE(h.pending_constraints[0].energy_lower_max_ready);
}

TEST_CASE("Stores energy_lower_min in pending", "[s2_pebc_logic - provide_energy_lower_limit_min]") {
    s2_client_t h = H_WITH_SLOT;
    TEST_ASSERT_EQUAL(ESP_OK, s2_client_pebc_provide_energy_lower_limit_min(&h, S2_COMMODITY_QUANTITY_ELECTRIC_POWER_L1, 7.0f));
    TEST_ASSERT_EQUAL_FLOAT(7.0f, h.pending_constraints[0].energy_lower_min);
    TEST_ASSERT_TRUE(h.pending_constraints[0].energy_lower_min_ready);
}

// ______________________ s2_client_pebc_provide_energy_valid_until ______________________

TEST_CASE("Returns invalid arg if quantity is not in default_limits", "[s2_pebc_logic - provide_energy_valid_until]") {
    s2_client_t h = {0};
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, s2_client_pebc_provide_energy_valid_until(&h, S2_COMMODITY_QUANTITY_ELECTRIC_POWER_L1, 12345));
}

TEST_CASE("Stores energy_valid_until deadline in pending slot", "[s2_pebc_logic - provide_energy_valid_until]") {
    s2_client_t h = {
        .rm.pebc.default_limits_count = 1,
        .rm.pebc.default_limits = {{.commodity_quantity = S2_COMMODITY_QUANTITY_ELECTRIC_POWER_L1}},
    };
    TEST_ASSERT_EQUAL(ESP_OK, s2_client_pebc_provide_energy_valid_until(&h, S2_COMMODITY_QUANTITY_ELECTRIC_POWER_L1, 99999));
    TEST_ASSERT_EQUAL(99999, h.pending_constraints[0].energy_valid_until);
}

// ______________________ try_send_constraints (via provide_power_upper_limit_max) ______________________

// Once all 8 values are ready, both power and energy constraint messages are sent and pending is cleared.
TEST_CASE("Sends power and energy constraints when all 8 values are ready", "[s2_pebc_logic - try_send_constraints]") {
    s2_client_t h = {
        .rm.pebc.default_limits_count = 1,
        .rm.pebc.default_limits = {{.commodity_quantity = S2_COMMODITY_QUANTITY_ELECTRIC_POWER_L1}},
        .active_constraint_slot = -1,
        .pending_constraints = {{
            .power_upper_min_ready = true, .power_upper_min = 1.0f,
            .power_lower_max_ready = true, .power_lower_max = -1.0f,
            .power_lower_min_ready = true, .power_lower_min = -2.0f,
            .energy_upper_max_ready = true, .energy_upper_max = 3.0f,
            .energy_upper_min_ready = true, .energy_upper_min = 1.0f,
            .energy_lower_max_ready = true, .energy_lower_max = -1.0f,
            .energy_lower_min_ready = true, .energy_lower_min = -3.0f,
        }},
    };
    cJSON_build_init();
    xQueueSemaphoreTake_IgnoreAndReturn(pdTRUE);
    xQueueGenericSend_IgnoreAndReturn(pdTRUE);
    TEST_ASSERT_EQUAL(ESP_OK, s2_client_pebc_provide_power_upper_limit_max(&h, S2_COMMODITY_QUANTITY_ELECTRIC_POWER_L1, 5.0f));
    TEST_ASSERT_NOT_EQUAL('\0', h.last_sent[0].power_constraints_id[0]); // stored after send
    TEST_ASSERT_NOT_EQUAL('\0', h.last_sent[0].energy_constraint_id[0]);
    TEST_ASSERT_FALSE(h.pending_constraints[0].power_upper_max_ready); // pending cleared
}

// ______________________ s2_pebc_on_session_started ______________________

// Verifies that both periodic timers are created and started, and that callbacks fire
// on_request_measurement fires once per commodity, on_request_param 8 times (one per limit slot).
static int session_measurement_calls;
static int session_param_calls;
static void on_session_meas(s2_commodity_quantity_t q) { session_measurement_calls++; }
static void on_session_param(s2_pebc_param_t p, s2_commodity_quantity_t q, bool abn) { session_param_calls++; }

TEST_CASE("Creates timers and fires callbacks immediately on session start", "[s2_pebc_logic - on_session_started]") {
    session_measurement_calls = 0;
    session_param_calls = 0;

    s2_client_t h = {
        .rm = {
            .measurement_interval_ms = 5000,
            .commodity_quantities_count = 1,
            .commodity_quantities = {S2_COMMODITY_QUANTITY_ELECTRIC_POWER_L1},
            .on_request_measurement = on_session_meas,
            .pebc = {
                .constraint_interval_ms = 10000,
                .default_limits_count = 1,
                .default_limits = {{.commodity_quantity = S2_COMMODITY_QUANTITY_ELECTRIC_POWER_L1}},
                .on_request_param = on_session_param,
            },
        },
    };
    esp_timer_create_IgnoreAndReturn(ESP_OK);
    esp_timer_start_periodic_ExpectAndReturn(NULL, 5000000, ESP_OK); // measurement: 5000ms * 1000
    esp_timer_start_periodic_IgnoreArg_timer();
    esp_timer_start_periodic_ExpectAndReturn(NULL, 10000000, ESP_OK); // constraints: 10000ms * 1000
    esp_timer_start_periodic_IgnoreArg_timer();
    s2_pebc_on_session_started(&h);
    TEST_ASSERT_EQUAL(1, session_measurement_calls);  // fired immediately for 1 commodity
    TEST_ASSERT_EQUAL(8, session_param_calls);         // 8 fired immediately for one
}

// A repeated session start must release the timers from the previous one instead of leaking them.
TEST_CASE("Releases existing timers when a session starts again", "[s2_pebc_logic - on_session_started]") {
    s2_client_t h = {
        .rm = {
            .measurement_interval_ms = 5000,
            .commodity_quantities_count = 1,
            .commodity_quantities = {S2_COMMODITY_QUANTITY_ELECTRIC_POWER_L1},
            .on_request_measurement = on_session_meas,
            .pebc = {
                .constraint_interval_ms = 10000,
                .default_limits_count = 1,
                .default_limits = {{.commodity_quantity = S2_COMMODITY_QUANTITY_ELECTRIC_POWER_L1}},
                .on_request_param = on_session_param,
            },
        },
    };
    esp_timer_create_IgnoreAndReturn(ESP_OK);
    esp_timer_start_periodic_IgnoreAndReturn(ESP_OK);

    s2_pebc_on_session_started(&h);
    TEST_ASSERT_EQUAL(0, Mockesp_timer_delete_calls());  // nothing to release on the first start

    s2_pebc_on_session_started(&h);
    TEST_ASSERT_EQUAL(2, Mockesp_timer_delete_calls());  // measurement + constraints released
}

// ______________________ apply_element / exec_cb / duration_cb ______________________

static esp_timer_cb_t g_cap_cb[2];
static void *g_cap_arg[2];
static int g_cap_n;

static esp_err_t cap_timer(const esp_timer_create_args_t *a, esp_timer_handle_t *out, int n) {
    if (g_cap_n < 2) { g_cap_cb[g_cap_n] = a->callback; g_cap_arg[g_cap_n] = a->arg; g_cap_n++; }
    return ESP_OK;
}

// Runs handle_instruction with a future exec time and a valid 1-element L1 envelope (ul=ll=0).
// Uses esp_timer_create_Stub(cap_timer) - g_cap_cb[0] = exec_cb after the call.
static void capture_exec_cb(s2_client_t *h) {
    static cJSON id_j   = {.valuestring = "instr-id"}, abn_j = {0};
    static cJSON pc_j   = {.valuestring = "pc-id"};
    static cJSON exec_j = {.valuestring = "2099-01-01T00:00:00"};
    static cJSON qty_j  = {.valuestring = "ELECTRIC.POWER.L1"};
    static cJSON ul_j   = {0}, ll_j = {0}, dur_j = {.valuedouble = 1000};
    static cJSON elem_j = {0}, elems_j = {.child = &elem_j};
    static cJSON env_j  = {0}, envs_j  = {.child = &env_j};
    static cJSON msg    = {0};

    g_cap_n = 0;
    cJSON_GetObjectItem_ExpectAnyArgsAndReturn(&id_j);
    cJSON_GetObjectItem_ExpectAnyArgsAndReturn(&abn_j);
    cJSON_GetObjectItem_ExpectAnyArgsAndReturn(&pc_j);
    cJSON_GetObjectItem_ExpectAnyArgsAndReturn(&exec_j);
    cJSON_GetObjectItem_ExpectAnyArgsAndReturn(&envs_j);
    cJSON_GetObjectItem_ExpectAnyArgsAndReturn(&qty_j);
    cJSON_GetObjectItem_ExpectAnyArgsAndReturn(&elems_j);
    cJSON_GetObjectItem_ExpectAnyArgsAndReturn(&ul_j);
    cJSON_GetObjectItem_ExpectAnyArgsAndReturn(&ll_j);
    cJSON_GetObjectItem_ExpectAnyArgsAndReturn(&dur_j);
    cJSON_IsString_IgnoreAndReturn(1);
    cJSON_IsBool_IgnoreAndReturn(1);
    cJSON_IsArray_IgnoreAndReturn(1);
    cJSON_IsTrue_IgnoreAndReturn(0);
    cJSON_GetArraySize_IgnoreAndReturn(1);
    cJSON_IsNumber_IgnoreAndReturn(1);
    cJSON_build_init();
    xQueueSemaphoreTake_IgnoreAndReturn(pdTRUE);
    xQueueGenericSend_IgnoreAndReturn(pdTRUE);
    esp_timer_create_Stub(cap_timer);
    esp_timer_start_once_IgnoreAndReturn(ESP_OK);
    s2_pebc_handle_instruction(h, &msg);
}

TEST_CASE("Calls on_instruction with element limits when exec fires", "[s2_pebc_logic - apply_element]") {
    static s2_commodity_quantity_t qty;
    void on_instr(s2_commodity_quantity_t q, float up, float lo) { qty = q; }
    s2_client_t h = {
        .rm.pebc.default_limits_count = 1,
        .rm.pebc.default_limits = {{.commodity_quantity = S2_COMMODITY_QUANTITY_ELECTRIC_POWER_L1}},
        .rm.pebc.on_instruction = on_instr,
        .last_sent = {{.power_constraints_id = "pc-id"}},
    };
    capture_exec_cb(&h);
    g_cap_cb[0](g_cap_arg[0]); // exec_cb -> apply_element -> on_instruction
    TEST_ASSERT_EQUAL(S2_COMMODITY_QUANTITY_ELECTRIC_POWER_L1, qty);
}

TEST_CASE("Clears exec timer, sends STARTED and arms duration timer", "[s2_pebc_logic - exec_cb]") {
    void on_instr(s2_commodity_quantity_t q, float up, float lo) {}
    s2_client_t h = {
        .rm.pebc.default_limits_count = 1,
        .rm.pebc.default_limits = {{.commodity_quantity = S2_COMMODITY_QUANTITY_ELECTRIC_POWER_L1}},
        .rm.pebc.on_instruction = on_instr,
        .last_sent = {{.power_constraints_id = "pc-id"}},
    };
    capture_exec_cb(&h); // g_cap_cb[0] = exec_cb, g_cap_n = 1
    g_cap_cb[0](g_cap_arg[0]);
    TEST_ASSERT_NULL(h.instruction.exec_timer); // cleared at start of exec_cb
    TEST_ASSERT_EQUAL(2, g_cap_n);              // duration timer was created
}

TEST_CASE("Sends SUCCEEDED and clears id when last element expires", "[s2_pebc_logic - duration_cb]") {
    void on_instr(s2_commodity_quantity_t q, float up, float lo) {}
    s2_client_t h = {
        .rm.pebc.default_limits_count = 1,
        .rm.pebc.default_limits = {{.commodity_quantity = S2_COMMODITY_QUANTITY_ELECTRIC_POWER_L1}},
        .rm.pebc.on_instruction = on_instr,
        .last_sent = {{.power_constraints_id = "pc-id"}},
    };
    capture_exec_cb(&h);
    g_cap_cb[0](g_cap_arg[0]); // exec_cb -> g_cap_cb[1] = duration_cb
    g_cap_cb[1](g_cap_arg[1]); // element_count=1, current_element->1 -> SUCCEEDED
    TEST_ASSERT_EQUAL('\0', h.instruction.id[0]);
}

// ______________________ validate_envelope_shape ______________________

TEST_CASE("Rejects instruction when envelope has no elements", "[s2_pebc_logic - validate_envelope_shape]") {
    cJSON id_j  = {.valuestring = "instr-id"}, abn_j = {0};
    cJSON pc_j  = {.valuestring = "pc-id"}, exec_j = {.valuestring = "2099-01-01T00:00:00"};
    cJSON qty_j = {.valuestring = "ELECTRIC.POWER.L1"}, elems_j = {0}; // no child
    cJSON env_j = {0}, envs_j = {.child = &env_j}, msg = {0};
    s2_client_t h = {
        .rm.pebc.default_limits_count = 1,
        .rm.pebc.default_limits = {{.commodity_quantity = S2_COMMODITY_QUANTITY_ELECTRIC_POWER_L1}},
        .last_sent = {{.power_constraints_id = "pc-id"}},
    };
    cJSON_GetObjectItem_ExpectAnyArgsAndReturn(&id_j);
    cJSON_GetObjectItem_ExpectAnyArgsAndReturn(&abn_j);
    cJSON_GetObjectItem_ExpectAnyArgsAndReturn(&pc_j);
    cJSON_GetObjectItem_ExpectAnyArgsAndReturn(&exec_j);
    cJSON_GetObjectItem_ExpectAnyArgsAndReturn(&envs_j);
    cJSON_GetObjectItem_ExpectAnyArgsAndReturn(&qty_j);
    cJSON_GetObjectItem_ExpectAnyArgsAndReturn(&elems_j);
    cJSON_IsString_IgnoreAndReturn(1);
    cJSON_IsBool_IgnoreAndReturn(1);
    cJSON_IsArray_IgnoreAndReturn(1);
    cJSON_IsTrue_IgnoreAndReturn(0);
    cJSON_GetArraySize_IgnoreAndReturn(0); // shape check fails: 0 elements
    cJSON_build_init();
    xQueueSemaphoreTake_IgnoreAndReturn(pdTRUE);
    xQueueGenericSend_IgnoreAndReturn(pdTRUE);
    TEST_ASSERT_EQUAL(ESP_OK, s2_pebc_handle_instruction(&h, &msg));
    TEST_ASSERT_EQUAL('\0', h.instruction.id[0]); // rejected, id never stored
}

// ______________________ init_tmp_from_defaults ______________________

TEST_CASE("Slot not covered by envelope keeps default limits", "[s2_pebc_logic - init_tmp_from_defaults]") {
    cJSON id_j   = {.valuestring = "instr-id"}, abn_j = {0};
    cJSON pc_j   = {.valuestring = "pc-id"}, exec_j = {.valuestring = "2099-01-01T00:00:00"};
    cJSON qty_j  = {.valuestring = "ELECTRIC.POWER.L1"}; // only L1 covered
    cJSON ul_j   = {0}, ll_j = {0}, dur_j = {.valuedouble = 1000};
    cJSON elem_j = {0}, elems_j = {.child = &elem_j};
    cJSON env_j  = {0}, envs_j = {.child = &env_j}, msg = {0};
    s2_client_t h = {
        .rm.pebc.default_limits_count = 2,
        .rm.pebc.default_limits = {
            {.commodity_quantity = S2_COMMODITY_QUANTITY_ELECTRIC_POWER_L1},
            {.commodity_quantity = S2_COMMODITY_QUANTITY_ELECTRIC_POWER_L2,
             .power_upper_limit = 5.0f, .power_lower_limit = -5.0f},
        },
        .last_sent = {{.power_constraints_id = "pc-id"}},
    };
    cJSON_GetObjectItem_ExpectAnyArgsAndReturn(&id_j);
    cJSON_GetObjectItem_ExpectAnyArgsAndReturn(&abn_j);
    cJSON_GetObjectItem_ExpectAnyArgsAndReturn(&pc_j);
    cJSON_GetObjectItem_ExpectAnyArgsAndReturn(&exec_j);
    cJSON_GetObjectItem_ExpectAnyArgsAndReturn(&envs_j);
    cJSON_GetObjectItem_ExpectAnyArgsAndReturn(&qty_j);
    cJSON_GetObjectItem_ExpectAnyArgsAndReturn(&elems_j);
    cJSON_GetObjectItem_ExpectAnyArgsAndReturn(&ul_j);
    cJSON_GetObjectItem_ExpectAnyArgsAndReturn(&ll_j);
    cJSON_GetObjectItem_ExpectAnyArgsAndReturn(&dur_j);
    cJSON_IsString_IgnoreAndReturn(1);
    cJSON_IsBool_IgnoreAndReturn(1);
    cJSON_IsArray_IgnoreAndReturn(1);
    cJSON_IsTrue_IgnoreAndReturn(0);
    cJSON_GetArraySize_IgnoreAndReturn(1);
    cJSON_IsNumber_IgnoreAndReturn(1);
    cJSON_build_init();
    xQueueSemaphoreTake_IgnoreAndReturn(pdTRUE);
    xQueueGenericSend_IgnoreAndReturn(pdTRUE);
    esp_timer_create_IgnoreAndReturn(ESP_OK);
    esp_timer_start_once_IgnoreAndReturn(ESP_OK);
    TEST_ASSERT_EQUAL(ESP_OK, s2_pebc_handle_instruction(&h, &msg));
    // slot 1 (L2) not in envelope - default limits from init_tmp_from_defaults kept
    TEST_ASSERT_EQUAL_FLOAT(5.0f,  h.instruction.elements[0].upper_limit[1]);
    TEST_ASSERT_EQUAL_FLOAT(-5.0f, h.instruction.elements[0].lower_limit[1]);
}
