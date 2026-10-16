// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

#include "json_stubs.h"
#include "cJSON.h"
#include "MockcJSON_mock.h"
#include <string.h>

#define MOCK_CJSON ((cJSON *)0xdeadbeef)

// _____________________________ Add item to object  _____________________________

static const char *m_add_item_fail_on_key = NULL;
static const char *m_add_item_failed_on_key = NULL;

static cJSON_bool cJSON_AddItemToObject_fail_on_key_stub(cJSON *obj, const char *key, cJSON *value, int num_calls) {
    (void)obj; (void)value; (void)num_calls;
    if (m_add_item_fail_on_key != NULL && strcmp(key, m_add_item_fail_on_key) == 0) {
        m_add_item_failed_on_key = key;
        return 0;
    }

    return 1;
}

void cJSON_AddItemToObject_fail_on_key(const char *key) {
    m_add_item_fail_on_key = key;
    cJSON_AddItemToObject_StopIgnore();
    cJSON_AddItemToObject_Stub(cJSON_AddItemToObject_fail_on_key_stub);
}

bool cJSON_AddItemToObject_failed_on_key(const char *key) {
    if (key == NULL) return m_add_item_failed_on_key == key;
    if (m_add_item_failed_on_key == NULL) return false;
    return strcmp(m_add_item_failed_on_key, key) == 0;
}

// _____________________________ Add string to object  _____________________________

static const char *m_add_string_fail_on_key = NULL;
static const char *m_add_string_failed_on_key = NULL;

static cJSON* cJSON_AddStringToObject_fail_on_key_stub(cJSON *obj, const char *key, const char *value, int num_calls) {
    if (m_add_string_fail_on_key != NULL && strcmp(key, m_add_string_fail_on_key) == 0) {
        m_add_string_failed_on_key = key;
        return NULL;
    }

    return MOCK_CJSON;
}

void cJSON_AddStringToObject_fail_on_key(const char *key) {
    m_add_string_fail_on_key = key;
    cJSON_AddStringToObject_StopIgnore();
    cJSON_AddStringToObject_Stub(cJSON_AddStringToObject_fail_on_key_stub);
}

bool cJSON_AddStringToObject_failed_on_key(const char *key) {
    if (key == NULL) return m_add_string_failed_on_key == key;
    if (m_add_string_failed_on_key == NULL) return false;
    return strcmp(m_add_string_failed_on_key, key) == 0;
}

// _____________________________ Get array item(s) _____________________________

#define MAX_GET_ARRAY_ITEM_MAPPINGS 16

typedef struct {
    int index;
    cJSON *node;
} get_array_item_mapping_t;

static get_array_item_mapping_t m_get_array_item_mappings[MAX_GET_ARRAY_ITEM_MAPPINGS];
static int m_get_array_item_mapping_count = 0;

static int cJSON_GetArraySize_return_for_index_stub(const cJSON *array, int num_calls) {
    (void)array; (void)num_calls;
    return m_get_array_item_mapping_count;
}

static cJSON* cJSON_GetArrayItem_return_for_index_stub(const cJSON *array, int index, int num_calls) {
    (void)array; (void)num_calls;
    for (int i = 0; i < m_get_array_item_mapping_count; i++) {
        if (m_get_array_item_mappings[i].index == index) {
            return m_get_array_item_mappings[i].node;
        }
    }

    return MOCK_CJSON;
}

void cJSON_GetArrayItem_init(void) {
    m_get_array_item_mapping_count = 0;
    cJSON_GetArrayItem_StopIgnore();
    cJSON_GetArraySize_StopIgnore();
    cJSON_GetArrayItem_Stub(cJSON_GetArrayItem_return_for_index_stub);
    cJSON_GetArraySize_Stub(cJSON_GetArraySize_return_for_index_stub);
}

void cJSON_GetArrayItem_return_for_index(int index, cJSON *node) {
    // Replace existing mapping if key was already registered
    for (int i = 0; i < m_get_array_item_mapping_count; i++) {
        if (m_get_array_item_mappings[i].index == index) {
            m_get_array_item_mappings[i].node = node;
            return;
        }
    }

    // Otherwise append a new mapping
    if (m_get_array_item_mapping_count < MAX_GET_ARRAY_ITEM_MAPPINGS) {
        m_get_array_item_mappings[m_get_array_item_mapping_count++] = (get_array_item_mapping_t){
            .index = index,
            .node = node
        };
    }
}

// _____________________________ Get object item _____________________________

#define MAX_GET_ITEM_MAPPINGS 16

typedef struct {
    const char *key;
    cJSON *node;
} get_item_mapping_t;

static get_item_mapping_t m_get_item_mappings[MAX_GET_ITEM_MAPPINGS];
static int m_get_item_mapping_count = 0;

static cJSON* cJSON_GetObjectItem_return_for_key_stub(const cJSON *obj, const char *key, int num_calls) {
    for (int i = 0; i < m_get_item_mapping_count; i++) {
        if (m_get_item_mappings[i].key != NULL && strcmp(m_get_item_mappings[i].key, key) == 0) {
            return m_get_item_mappings[i].node;
        }
    }

    // Fall back to MOCK_CJSON so unregistered keys remain consistent with the
    // catch-all behaviour installed by mock_json_init()
    return MOCK_CJSON;
}

void cJSON_GetObjectItem_init(void) {
    m_get_item_mapping_count = 0;
    cJSON_GetObjectItem_StopIgnore();
    cJSON_GetObjectItem_Stub(cJSON_GetObjectItem_return_for_key_stub);
}

void cJSON_GetObjectItem_return_for_key(const char *key, cJSON *node) {
    // Replace existing mapping if key was already registered
    for (int i = 0; i < m_get_item_mapping_count; i++) {
        if (m_get_item_mappings[i].key != NULL && strcmp(m_get_item_mappings[i].key, key) == 0) {
            m_get_item_mappings[i].node = node;
            return;
        }
    }

    // Otherwise append a new mapping
    if (m_get_item_mapping_count < MAX_GET_ITEM_MAPPINGS) {
        m_get_item_mappings[m_get_item_mapping_count++] = (get_item_mapping_t){
            .key = key,
            .node = node
        };
    }
}

// _____________________________ Type checks _____________________________

#define MAX_IS_STRING_FAIL_NODES 16

static const cJSON *m_is_string_fail_nodes[MAX_IS_STRING_FAIL_NODES];
static int m_is_string_fail_count = 0;

static cJSON_bool cJSON_IsString_fail_for_node_stub(const cJSON *item, int num_calls) {
    // Match real cJSON behaviour for NULL input
    if (item == NULL) return 0;

    // Return false when the node was registered as failing
    for (int i = 0; i < m_is_string_fail_count; i++) {
        if (m_is_string_fail_nodes[i] == item) {
            return 0;
        }
    }

    // Otherwise default to truthy, matching the catch-all from mock_json_init
    return 1;
}

void cJSON_IsString_init(void) {
    m_is_string_fail_count = 0;
    cJSON_IsString_StopIgnore();
    cJSON_IsString_Stub(cJSON_IsString_fail_for_node_stub);
}

void cJSON_IsString_fail_for_node(const cJSON *node) {
    if (m_is_string_fail_count < MAX_IS_STRING_FAIL_NODES) {
        m_is_string_fail_nodes[m_is_string_fail_count++] = node;
    }
}

// _____________________________ Recording _____________________________

#define MAX_RECORDED_CALLS 64

typedef enum {
    RECORD_KIND_STRING,
    RECORD_KIND_ITEM
} record_kind_t;

typedef struct {
    record_kind_t kind;
    const char *key;
    const char *value; // only set for RECORD_KIND_STRING
} json_record_t;

static json_record_t m_records[MAX_RECORDED_CALLS];
static int m_record_count = 0;

static cJSON* cJSON_AddStringToObject_record_stub(cJSON *obj, const char *key, const char *value, int num_calls) {
    if (m_record_count < MAX_RECORDED_CALLS) {
        m_records[m_record_count++] = (json_record_t){
            .kind = RECORD_KIND_STRING,
            .key = key,
            .value = value
        };
    }

    return MOCK_CJSON;
}

static cJSON_bool cJSON_AddItemToObject_record_stub(cJSON *obj, const char *key, cJSON *item, int num_calls) {
    if (m_record_count < MAX_RECORDED_CALLS) {
        m_records[m_record_count++] = (json_record_t){
            .kind = RECORD_KIND_ITEM,
            .key = key,
            .value = NULL
        };
    }

    return 1;
}

void cJSON_record_init(void) {
    m_record_count = 0;
    cJSON_AddStringToObject_StopIgnore();
    cJSON_AddItemToObject_StopIgnore();
    cJSON_AddStringToObject_Stub(cJSON_AddStringToObject_record_stub);
    cJSON_AddItemToObject_Stub(cJSON_AddItemToObject_record_stub);
}

bool cJSON_was_string_added(const char *key, const char *value) {
    for (int i = 0; i < m_record_count; i++) {
        if (m_records[i].kind != RECORD_KIND_STRING) continue;
        if (m_records[i].key == NULL || m_records[i].value == NULL) continue;
        if (strcmp(m_records[i].key, key) == 0 && strcmp(m_records[i].value, value) == 0) {
            return true;
        }
    }
    return false;
}

bool cJSON_was_item_added(const char *key) {
    for (int i = 0; i < m_record_count; i++) {
        if (m_records[i].kind != RECORD_KIND_ITEM) continue;
        if (m_records[i].key == NULL) continue;
        if (strcmp(m_records[i].key, key) == 0) {
            return true;
        }
    }
    return false;
}

// _____________________________ Build helpers _____________________________

static char *cJSON_PrintUnformatted_build_stub(const cJSON *item, int num_calls) {
    (void)item; (void)num_calls;
    return strdup("{}");
}

// Sets up ignores/stubs so that cJSON builder calls in production code succeed.
void cJSON_build_init(void) {
    cJSON_CreateObject_IgnoreAndReturn(MOCK_CJSON);
    cJSON_CreateArray_IgnoreAndReturn(MOCK_CJSON);
    cJSON_CreateString_IgnoreAndReturn(MOCK_CJSON);
    cJSON_AddStringToObject_IgnoreAndReturn(MOCK_CJSON);
    cJSON_AddArrayToObject_IgnoreAndReturn(MOCK_CJSON);
    cJSON_AddItemToArray_IgnoreAndReturn(1);
    cJSON_AddItemToObject_IgnoreAndReturn(1);
    cJSON_AddNumberToObject_IgnoreAndReturn(MOCK_CJSON);
    cJSON_AddBoolToObject_IgnoreAndReturn(MOCK_CJSON);
    cJSON_AddObjectToObject_IgnoreAndReturn(MOCK_CJSON);
    cJSON_PrintUnformatted_Stub(cJSON_PrintUnformatted_build_stub);
    cJSON_ParseWithLength_IgnoreAndReturn(NULL);
    cJSON_Delete_Ignore();
}
