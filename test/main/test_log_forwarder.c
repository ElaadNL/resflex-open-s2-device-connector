// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

#include "unity.h"
#include "log_forwarder.h"
#include "s2_client_internal.h"
#include "Mockesp_timer.h"
#include "fixtures.h"

#define TEST_LOGF_QUEUE_SIZE 16

TEST_CASE("log_forwarder basic coverage", "[log_forwarder]")
{
    s2_client_t h = {0};

    esp_timer_create_IgnoreAndReturn(ESP_OK);
    esp_timer_start_once_IgnoreAndReturn(ESP_OK);

    cJSON_build_init();
    mock_queue_init(CREATE_AND_SUCCEED);

    xQueueGenericCreate_IgnoreAndReturn((QueueHandle_t)0xdeadbeef);

    log_forwarder_init(&h);

    log_forwarder_enqueue(NULL, NULL, NULL);
    log_forwarder_enqueue("INFO", "RM", "message");

    log_forwarder_flush_now_for_test();

    TEST_PASS();
}

TEST_CASE("log_forwarder enqueue and flush", "[log_forwarder]")
{
    s2_client_t h = {0};

    esp_timer_create_IgnoreAndReturn(ESP_OK);
    esp_timer_start_once_IgnoreAndReturn(ESP_OK);

    cJSON_build_init();
    mock_queue_init(CREATE_AND_SUCCEED);
    xQueueGenericCreate_IgnoreAndReturn((QueueHandle_t)0xdeadbeef);

    log_forwarder_init(&h);
    for (int i = 0; i < 3; ++i) {
        log_forwarder_enqueue("INFO", "SRC", "msg");
    }

    /* Force immediate flush and ensure no mock failures occur. */
    log_forwarder_flush_now_for_test();

    log_forwarder_flush_now_for_test();

    TEST_PASS();
}

TEST_CASE("log_forwarder overflow and batch", "[log_forwarder]")
{
    s2_client_t h = {0};

    esp_timer_create_IgnoreAndReturn(ESP_OK);
    esp_timer_start_once_IgnoreAndReturn(ESP_OK);

    cJSON_build_init();
    mock_queue_init(CREATE_AND_SUCCEED);
    xQueueGenericCreate_IgnoreAndReturn((QueueHandle_t)0xdeadbeef);

    log_forwarder_init(&h);

    /* Fill beyond the queue size to exercise drop-oldest path */
    for (int i = 0; i < (TEST_LOGF_QUEUE_SIZE + 4); ++i) {
        log_forwarder_enqueue("WARN", "SRC", "overflow");
    }

    /* Three flushes are enough to drain the 16-entry queue because the
     * implementation sends at most 6 messages per batch. */
    log_forwarder_flush_now_for_test();
    log_forwarder_flush_now_for_test();
    log_forwarder_flush_now_for_test();

    TEST_PASS();
}

TEST_CASE("log_forwarder flush null root", "[log_forwarder]")
{
    s2_client_t h = {0};

    esp_timer_create_IgnoreAndReturn(ESP_OK);
    esp_timer_start_once_IgnoreAndReturn(ESP_OK);

    cJSON_build_init();
    mock_queue_init(CREATE_AND_SUCCEED);
    xQueueGenericCreate_IgnoreAndReturn((QueueHandle_t)0xdeadbeef);

    log_forwarder_init(&h);
    log_forwarder_enqueue("INFO", "SRC", "msg");

    // Make cJSON_CreateObject return NULL to hit the !root path
    cJSON_CreateObject_StopIgnore();
    cJSON_CreateObject_IgnoreAndReturn(NULL);

    log_forwarder_flush_now_for_test();

    TEST_PASS();
}

TEST_CASE("log_forwarder flush null body", "[log_forwarder]")
{
    s2_client_t h = {0};

    esp_timer_create_IgnoreAndReturn(ESP_OK);
    esp_timer_start_once_IgnoreAndReturn(ESP_OK);

    cJSON_build_init();
    mock_queue_init(CREATE_AND_SUCCEED);
    xQueueGenericCreate_IgnoreAndReturn((QueueHandle_t)0xdeadbeef);

    log_forwarder_init(&h);
    log_forwarder_enqueue("INFO", "SRC", "msg");

    // Make cJSON_PrintUnformatted return NULL to hit the !body path
    cJSON_PrintUnformatted_StopIgnore();
    cJSON_PrintUnformatted_IgnoreAndReturn(NULL);

    log_forwarder_flush_now_for_test();

    TEST_PASS();
}

TEST_CASE("log_forwarder enqueue drops reception status", "[log_forwarder]")
{
    // Call enqueue with a ReceptionStatus message; it should be dropped */
    log_forwarder_enqueue("INFO", "RM", "Received ReceptionStatus from CEM");

    TEST_PASS();
}

TEST_CASE("log_forwarder enqueue lazy mutex fails", "[log_forwarder]")
{
    // Reset state so the lazy mutex creation path is exercised */
    log_forwarder_reset_for_test();

    // Make mutex creation fail
    xQueueCreateMutex_StopIgnore();
    xQueueCreateMutex_IgnoreAndReturn(NULL);

    log_forwarder_enqueue("INFO", "RM", "early log before init");

    TEST_PASS();
}

TEST_CASE("log_forwarder enqueue semaphore take fails", "[log_forwarder]")
{
    s2_client_t h = {0};

    esp_timer_create_IgnoreAndReturn(ESP_OK);
    esp_timer_start_once_IgnoreAndReturn(ESP_OK);

    cJSON_build_init();
    mock_queue_init(CREATE_AND_SUCCEED);
    xQueueGenericCreate_IgnoreAndReturn((QueueHandle_t)0xdeadbeef);

    log_forwarder_init(&h);

    // Make semaphore take fail so the enqueue bails out early
    xQueueSemaphoreTake_StopIgnore();
    xQueueSemaphoreTake_IgnoreAndReturn(pdFALSE);

    log_forwarder_enqueue("WARN", "SRC", "should not be queued");

    TEST_PASS();
}