// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

// Vendored lightweight mock for esp_timer for unit tests.
#pragma once

#include <stdint.h>

typedef int esp_err_t;
typedef void* esp_timer_handle_t;

/* minimal callback and create-args types used by tests */
typedef void (*esp_timer_cb_t)(void *);
typedef struct {
	esp_timer_cb_t callback;
	void *arg;
	const char *name;
} esp_timer_create_args_t;

/* Simple CMock-like control functions used by tests */
void Mockesp_timer_Init(void);
void Mockesp_timer_Verify(void);
void Mockesp_timer_Destroy(void);

/* Number of esp_timer_delete() calls since the last Init/Destroy */
int Mockesp_timer_delete_calls(void);

/* CMock-style helpers (tests call these macros/functions) */
void esp_timer_create_IgnoreAndReturn(esp_err_t to_return);
void esp_timer_start_once_IgnoreAndReturn(esp_err_t to_return);
void esp_timer_start_periodic_IgnoreAndReturn(esp_err_t to_return);
void esp_timer_stop_IgnoreAndReturn(esp_err_t to_return);

/* More CMock-like helpers used in tests */
void esp_timer_create_ExpectAnyArgsAndReturn(esp_err_t to_return);
void esp_timer_create_ExpectAndReturn(const void *args, esp_timer_handle_t handle, esp_err_t to_return);
void esp_timer_create_Stub(esp_err_t (*stub)(const esp_timer_create_args_t *a, esp_timer_handle_t *out, int n));

void esp_timer_start_periodic_ExpectAndReturn(esp_timer_handle_t timer, uint64_t period_us, esp_err_t to_return);
void esp_timer_start_periodic_IgnoreArg_timer(void);

void esp_timer_create_ExpectAnyArgsAndReturn(esp_err_t to_return);
void esp_timer_start_once_ExpectAnyArgsAndReturn(esp_err_t to_return);
void esp_timer_start_periodic_ExpectAnyArgsAndReturn(esp_err_t to_return);

/* Minimal esp_timer API that the production code calls. Implementations
 * are provided in Mockesp_timer.c for tests. In production builds the real
 * esp-timer component provides these symbols.
 */
esp_err_t esp_timer_create(const void *args, esp_timer_handle_t *handle);
esp_err_t esp_timer_start_once(esp_timer_handle_t timer, uint64_t timeout_us);
esp_err_t esp_timer_start_periodic(esp_timer_handle_t timer, uint64_t period_us);
esp_err_t esp_timer_stop(esp_timer_handle_t timer);
esp_err_t esp_timer_delete(esp_timer_handle_t timer);
uint64_t esp_timer_get_time(void);
