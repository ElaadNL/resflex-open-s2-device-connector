// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Mockqueue.h"
#include "Mocktask.h"
#include "Mockidf_additions.h"
#include "Mockesp_http_client_mock.h"
#include "MockcJSON_mock.h"
#include "json_stubs.h"
#include <string.h>
#include <stdlib.h>

/* =========================================================================
 * CreateAnd – controls the behaviour of a single mock fixture
 * =========================================================================
 *
 *  CREATE_AND_FAIL    – the creation call returns NULL / pdFALSE so that
 *                       error-handling paths in the code under test are hit
 *
 *  CREATE_AND_SUCCEED – the creation call returns a valid handle and an
 *                       exact matching delete call is expected exactly once
 *
 *  CREATE_AND_IGNORE  – both the creation call and the delete call are
 *                       ignored; useful when the fixture is only needed as
 *                       background plumbing that is not the focus of the test
 */
typedef enum {
    CREATE_AND_FAIL,
    CREATE_AND_SUCCEED,
    CREATE_AND_IGNORE
} CreateAnd;

/* =========================================================================
 * FreeRTOS queue mock
 * =========================================================================*/
 
/**
 * @brief Arm queue-creation CMock expectations according to @p flags
 *
 * Must be called before the code under test runs
 *
 * @param flags Desired mock behaviour (fail / succeed / ignore)
 */
static inline void mock_queue_init(CreateAnd flags) {
    static const QueueHandle_t queue = (QueueHandle_t)0xdeadbeef;
 
    switch (flags) {
        case CREATE_AND_FAIL:
            xQueueGenericCreate_ExpectAnyArgsAndReturn(NULL);
            xQueueCreateMutex_IgnoreAndReturn(NULL);
            xQueueCreateMutexStatic_IgnoreAndReturn(NULL);
            xQueueSemaphoreTake_IgnoreAndReturn(pdFALSE);
            xQueueGenericSend_IgnoreAndReturn(pdFALSE);
            break;
    
        case CREATE_AND_IGNORE:
            xQueueGenericCreate_IgnoreAndReturn(queue);
            xQueueCreateMutex_IgnoreAndReturn(queue);
            xQueueCreateMutexStatic_IgnoreAndReturn(queue);
            vQueueDelete_Ignore();
        break;

        case CREATE_AND_SUCCEED:
        default:
            xQueueGenericCreate_ExpectAnyArgsAndReturn(queue);
            xQueueCreateMutex_IgnoreAndReturn(queue);
            xQueueCreateMutexStatic_IgnoreAndReturn(queue);
            xQueueSemaphoreTake_IgnoreAndReturn(pdTRUE);
            xQueueGenericSend_IgnoreAndReturn(pdTRUE);
            break;
    }
}

/* =========================================================================
 * FreeRTOS task mock
 * =========================================================================*/

/**
 * @brief Arm task-creation CMock expectations according to @p flags
 *
 * Must be called before the code under test runs
 *
 * @param flags Desired mock behaviour (fail / succeed / ignore)
 */
static inline void mock_task_init(CreateAnd flags) {
    static TaskHandle_t task = (TaskHandle_t)1;

    switch (flags) {
        case CREATE_AND_FAIL:
            xTaskCreatePinnedToCore_ExpectAnyArgsAndReturn(pdFALSE);
            break;
    
        case CREATE_AND_IGNORE:
            xTaskCreatePinnedToCore_IgnoreAndReturn(pdTRUE);
            xTaskCreatePinnedToCore_ReturnThruPtr_pxCreatedTask(&task);
            vTaskDelete_Ignore();
            break;

        case CREATE_AND_SUCCEED:
        default:
            xTaskCreatePinnedToCore_ExpectAnyArgsAndReturn(pdTRUE);
            xTaskCreatePinnedToCore_ReturnThruPtr_pxCreatedTask(&task);
            break;
    }
}

/* =========================================================================
 * ESP HTTP client
 * =========================================================================*/

 typedef struct {
    esp_http_client_handle_t client; /**< Fake handle returned by init. */
} MockHttpClient;

/**
 * @brief Arm esp_http_client_init() expectations according to @p flags
 *
 * Must be called before the code under test runs
 *
 * @param http  MockHttpClient instance whose handle field will be populated
 * @param flags Desired mock behaviour
 */
static inline void mock_http_client_init(MockHttpClient *http, CreateAnd flags) {
    http->client = (esp_http_client_handle_t)0xdeadbeef;
 
    switch (flags) {
        case CREATE_AND_FAIL:
            esp_http_client_init_ExpectAnyArgsAndReturn(NULL);
            break;
    
        case CREATE_AND_IGNORE:
            esp_http_client_init_IgnoreAndReturn(http->client);
            break;

        case CREATE_AND_SUCCEED:
        default:
            esp_http_client_init_ExpectAnyArgsAndReturn(http->client);
            break;
    }
}

/* =========================================================================
 * JSON
 * =========================================================================*/

#define MOCK_CJSON ((cJSON *)0xdeadbeef)

/**
 * @brief Initialise a stack-allocated cJSON node as a string value
 *
 * Use this when the code under test will dereference ->valuestring on a
 * value returned from a mocked cJSON_GetObjectItem (or similar) call.
 * Returning the catch-all MOCK_CJSON sentinel for those calls would cause
 * the code under test to read from an invalid address (0xdeadbeef) and
 * crash/hang the test
 *
 * @param node      cJSON struct to initialise. Its lifetime must outlive
 *                  any usage by the code under test
 * @param value     String value to expose via the node's valuestring field.
 *                  Must remain valid for the duration of the test
 */
static inline void mock_cjson_string(cJSON *node, const char *value) {
    *node = (cJSON){
        .type = cJSON_String,
        .valuestring = (char *)value
    };
}

/**
 * @brief Initialise a stack-allocated cJSON node as an object value
 *
 * Use this when the code under test extracts an object with cJSON_GetObjectItem
 * and uses it for checks like cJSON_IsObject (or similar) call.
 * Returning the catch-all MOCK_CJSON sentinel for those calls would cause
 * the code under test to read from an invalid address (0xdeadbeef) and
 * crash/hang the test
 *
 * @param node      cJSON struct to initialise. Its lifetime must outlive
 *                  any usage by the code under test
 * @param child     First child node of the object. May be NULL for an
 *                  empty object. Must remain valid for the duration of
 *                  the test
 */
static inline void mock_cjson_object(cJSON *node, cJSON *child) {
    *node = (cJSON){
        .type  = cJSON_Object,
        .child = child
    };
}

/**
 * @brief Initialise JSON mocks by ignoring ALL JSON calls by default
 *
 * Must be called before code under test runs. If you want to check if a
 * specific JSON function is called, you should call func_StopIgnore() on
 * it first
 */
static inline void mock_json_init() {
    // Ignore object calls
    cJSON_CreateObject_IgnoreAndReturn(MOCK_CJSON);
    cJSON_AddStringToObject_IgnoreAndReturn(MOCK_CJSON);
    cJSON_AddNullToObject_IgnoreAndReturn(MOCK_CJSON);
    cJSON_AddBoolToObject_IgnoreAndReturn(MOCK_CJSON);
    cJSON_AddItemToObject_IgnoreAndReturn(1);
    cJSON_IsObject_IgnoreAndReturn(1);
    cJSON_GetObjectItem_IgnoreAndReturn(MOCK_CJSON);

    // Ignore array calls
    cJSON_CreateArray_IgnoreAndReturn(MOCK_CJSON);
    cJSON_AddItemToArray_IgnoreAndReturn(1);
    cJSON_IsArray_IgnoreAndReturn(1);
    cJSON_GetArraySize_IgnoreAndReturn(1);
    cJSON_GetArrayItem_IgnoreAndReturn(MOCK_CJSON);

    // Ignore other calls
    cJSON_CreateString_IgnoreAndReturn(MOCK_CJSON);
    cJSON_IsString_IgnoreAndReturn(1);
    cJSON_IsNull_IgnoreAndReturn(0);
    cJSON_PrintUnformatted_IgnoreAndReturn(strdup("{}"));
    cJSON_ParseWithLength_IgnoreAndReturn(MOCK_CJSON);
    cJSON_Delete_Ignore();

    // Initialize all stubs
    cJSON_GetArrayItem_init();
    cJSON_GetObjectItem_init();
    cJSON_record_init();
    cJSON_IsString_init();
}
