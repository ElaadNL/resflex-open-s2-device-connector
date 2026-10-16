// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

// Test stubs for s2_client_websocket_connect
#pragma once
#include "s2_client.h"

void s2_websocket_connect_stub_set_return(esp_err_t v);
void s2_websocket_connect_stub_reset(void);

// Session calls the reconnect logic makes. Left alone they run the real code.
void s2_initiate_session_stub_fail_times(int times, esp_err_t then);
void s2_confirm_access_token_stub_set_return(esp_err_t err);
void s2_session_stubs_reset(void);
