// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

#include "unity.h"
#include <stdbool.h>
#include "Mockqueue.h"
#include "Mocktask.h"
#include "Mockidf_additions.h"
#include "Mockesp_http_client_mock.h"
#include "MockcJSON_mock.h"
#include "Mockesp_timer.h"
#include "Mockevent_groups.h"
#include "esp_websocket_client_stubs.h"
#include "s2_client_initiate_stubs.h"

#include "esp_log.h"

void setUp() {
    // Initialize mocks
    Mockesp_http_client_mock_Init();
    Mockidf_additions_Init();
    Mocktask_Init();
    Mockqueue_Init();
    MockcJSON_mock_Init();
    Mockesp_timer_Init();
    Mockevent_groups_Init();
    websocket_client_stubs_reset();
    s2_websocket_connect_stub_reset();
    s2_session_stubs_reset();
}

void tearDown() {
    // Clear mocked state before each test
    Mockesp_http_client_mock_Verify();
    Mockidf_additions_Verify();
    Mocktask_Verify();
    Mockqueue_Verify();
    MockcJSON_mock_Verify();
    Mockesp_timer_Verify();
    Mockevent_groups_Verify();

    Mockesp_http_client_mock_Destroy();
    Mockidf_additions_Destroy();
    Mocktask_Destroy();
    Mockqueue_Destroy();
    MockcJSON_mock_Destroy();
    Mockesp_timer_Destroy();
    Mockevent_groups_Destroy();
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    unity_run_all_tests();
    // unity_run_menu();
    UNITY_END();
}