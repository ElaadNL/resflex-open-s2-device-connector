// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "cJSON.h"
#include <stdbool.h>

// _____________________________ Failure monitoring _____________________________

/**
 * @brief Install stub for cJSON_AddItemToObject to fail on specific key
 *
 * @param key Key to fail the cJSON_AddItemToObject call on
 *
 * Must be called before the code under test runs. After the code under test has run,
 * the helpers below can be used to assert on which key the function call has failed
 *
 * Resets the recorded key on every invocation
 */
void cJSON_AddItemToObject_fail_on_key(const char *key);

/**
 * @brief Returns true when cJSON_AddItemToObject failed on the given key
 */
bool cJSON_AddItemToObject_failed_on_key(const char *key);

/**
 * @brief Install stub for cJSON_AddStringToObject to fail on specific key
 *
 * @param key Key to fail the cJSON_AddStringToObject call on
 *
 * Must be called before the code under test runs. After the code under test has run,
 * the helpers below can be used to assert on which key the function call has failed
 *
 * Resets the recorded key on every invocation
 */
void cJSON_AddStringToObject_fail_on_key(const char *key);

/**
 * @brief Returns true when cJSON_AddStringToObject failed on the given key
 */
bool cJSON_AddStringToObject_failed_on_key(const char *key);

// _____________________________ Get array item(s) _____________________________

/**
 * @brief Install stub for cJSON_GetArrayItem and cJSON_GetArray_size and
 * reset registered mappings
 *
 * Must be called before each test that uses cJSON_GetArrayItem_return_for_index
 * to prevent mappings from leaking between tests
 */
void cJSON_GetArrayItem_init(void);

/**
 * @brief Register a cJSON node to be returned when cJSON_GetArrayItem is
 *        called with the given key
 *
 * Must be preceded by cJSON_GetArrayItem_init(). Multiple indexes can be
 * registered by calling this function repeatedly. Calls for indexes that have
 * not been registered fall back to returning MOCK_CJSON, so catch-all
 * expectations from mock_json_init() keep working
 *
 * Note: the stub matches on index only, regardless of which parent object
 * cJSON_GetArrayItem was called on
 *
 * @param index Index the stub will match on. Lifetime must outlive the test
 * @param node  cJSON node returned for matching indexes. Lifetime must
 *              outlive the test
 */
void cJSON_GetArrayItem_return_for_index(int index, cJSON *node);

// _____________________________ Get object item _____________________________

/**
 * @brief Install stub for cJSON_GetObjectItem and reset registered mappings
 *
 * Must be called before each test that uses cJSON_GetObjectItem_return_for_key
 * to prevent mappings from leaking between tests
 */
void cJSON_GetObjectItem_init(void);

/**
 * @brief Register a cJSON node to be returned when cJSON_GetObjectItem is
 *        called with the given key
 *
 * Must be preceded by cJSON_GetObjectItem_init(). Multiple keys can be
 * registered by calling this function repeatedly. Calls for keys that have
 * not been registered fall back to returning MOCK_CJSON, so catch-all
 * expectations from mock_json_init() keep working
 *
 * Note: the stub matches on key only, regardless of which parent object
 * cJSON_GetObjectItem was called on
 *
 * @param key   Key the stub will match on (string compared, not pointer
 *              compared). Lifetime must outlive the test
 * @param node  cJSON node returned for matching keys. Pass NULL to simulate
 *              a missing key. Lifetime must outlive the test
 */
void cJSON_GetObjectItem_return_for_key(const char *key, cJSON *node);

// _____________________________ Type checks _____________________________

/**
 * @brief Install stub for cJSON_IsString and reset registered failing nodes
 *
 * Must be called before each test that uses cJSON_IsString_fail_for_node
 * to prevent registrations from leaking between tests
 */
void cJSON_IsString_init(void);

/**
 * @brief Register a cJSON node for which cJSON_IsString should return false
 *
 * Must be preceded by cJSON_IsString_init(). Multiple nodes can be registered
 * by calling this function repeatedly. cJSON_IsString returns true (1) for
 * any node not registered as failing, keeping consistency with the catch-all
 * behaviour installed by mock_json_init()
 *
 * @param node  cJSON node pointer for which IsString should return false.
 *              Compared by pointer identity, not by contents
 */
void cJSON_IsString_fail_for_node(const cJSON *node);

// _____________________________ Recording _____________________________

/**
 * @brief Install recording stubs for cJSON_AddStringToObject and cJSON_AddItemToObject
 *
 * Must be called before the code under test runs. After the code under test has run,
 * the helpers below can be used to assert which keys/values have been added to the
 * (mocked) JSON object.
 *
 * Resets the recorded call list on every invocation
 */
void cJSON_record_init(void);

/**
 * @brief Returns true when cJSON_AddStringToObject was called with the given key/value
 */
bool cJSON_was_string_added(const char *key, const char *value);

/**
 * @brief Returns true when cJSON_AddItemToObject was called with the given key
 */
bool cJSON_was_item_added(const char *key);

// _____________________________ Build helpers _____________________________

/**
 * @brief Install ignores/stubs so that cJSON builder calls in production code succeed.
 *
 * Must be called before the code under test runs.
 */
void cJSON_build_init(void);
