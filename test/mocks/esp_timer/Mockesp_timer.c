// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

// Lightweight implementation of the vendored esp_timer mock used in unit tests.

#include "Mockesp_timer.h"
#include <stdlib.h>

static esp_err_t next_create_return = 0;
static int create_once = 0;
static esp_err_t (*create_stub)(const esp_timer_create_args_t *a, esp_timer_handle_t *out, int n) = NULL;

static esp_err_t next_start_once_return = 0;
static int start_once_once = 0;

static esp_err_t next_start_periodic_return = 0;
static int start_periodic_once = 0;

static esp_err_t next_stop_return = 0;
static int stop_once = 0;
static int delete_calls = 0;

void Mockesp_timer_Init(void) {
    next_create_return = 0;
    create_once = 0;
    create_stub = NULL;

    next_start_once_return = 0;
    start_once_once = 0;

    next_start_periodic_return = 0;
    start_periodic_once = 0;

    next_stop_return = 0;
    stop_once = 0;
    delete_calls = 0;
}
void Mockesp_timer_Verify(void) { /* no-op */ }
void Mockesp_timer_Destroy(void) {
    /* Reset all state to defaults to avoid leakage between tests */
    next_create_return = 0;
    create_once = 0;
    create_stub = NULL;

    next_start_once_return = 0;
    start_once_once = 0;

    next_start_periodic_return = 0;
    start_periodic_once = 0;

    next_stop_return = 0;
    stop_once = 0;
    delete_calls = 0;
}

void esp_timer_create_IgnoreAndReturn(esp_err_t to_return) { next_create_return = to_return; create_once = 0; }
void esp_timer_start_once_IgnoreAndReturn(esp_err_t to_return) { next_start_once_return = to_return; start_once_once = 0; }
void esp_timer_start_periodic_IgnoreAndReturn(esp_err_t to_return) { next_start_periodic_return = to_return; start_periodic_once = 0; }
void esp_timer_stop_IgnoreAndReturn(esp_err_t to_return) { next_stop_return = to_return; stop_once = 0; }

void esp_timer_create_ExpectAnyArgsAndReturn(esp_err_t to_return) { next_create_return = to_return; create_once = 1; }
void esp_timer_create_ExpectAndReturn(const void *args, esp_timer_handle_t handle, esp_err_t to_return) { (void)args; if (handle) * (void **)&handle = handle; next_create_return = to_return; create_once = 1; }
void esp_timer_create_Stub(esp_err_t (*stub)(const esp_timer_create_args_t *a, esp_timer_handle_t *out, int n)) { create_stub = stub; }

void esp_timer_start_periodic_ExpectAndReturn(esp_timer_handle_t timer, uint64_t period_us, esp_err_t to_return) { (void)timer; (void)period_us; next_start_periodic_return = to_return; start_periodic_once = 1; }
void esp_timer_start_periodic_IgnoreArg_timer(void) { /* no-op for mock */ }
void esp_timer_start_once_ExpectAnyArgsAndReturn(esp_err_t to_return) { next_start_once_return = to_return; start_once_once = 1; }
void esp_timer_start_periodic_ExpectAnyArgsAndReturn(esp_err_t to_return) { next_start_periodic_return = to_return; start_periodic_once = 1; }

esp_err_t esp_timer_create(const void *args, esp_timer_handle_t *handle) {
    if (create_stub) {
        return create_stub((const esp_timer_create_args_t *)args, handle, 0);
    }
    (void)args;
    if (handle) *handle = (esp_timer_handle_t)malloc(1);
    esp_err_t r = next_create_return;
    if (create_once) next_create_return = 0;
    return r;
}

esp_err_t esp_timer_start_once(esp_timer_handle_t timer, uint64_t timeout_us) {
    (void)timer; (void)timeout_us;
    esp_err_t r = next_start_once_return;
    if (start_once_once) next_start_once_return = 0;
    return r;
}

esp_err_t esp_timer_start_periodic(esp_timer_handle_t timer, uint64_t period_us) {
    (void)timer; (void)period_us;
    esp_err_t r = next_start_periodic_return;
    if (start_periodic_once) next_start_periodic_return = 0;
    return r;
}

esp_err_t esp_timer_stop(esp_timer_handle_t timer) {
    (void)timer;
    esp_err_t r = next_stop_return;
    if (stop_once) next_stop_return = 0;
    return r;
}

esp_err_t esp_timer_delete(esp_timer_handle_t timer) {
    (void)timer;
    delete_calls++;
    return 0;
}

int Mockesp_timer_delete_calls(void) { return delete_calls; }

uint64_t esp_timer_get_time(void) {
    return 0ULL;
}
