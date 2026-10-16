// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "cJSON.h"

cJSON* cJSON_CreateObject(void);
cJSON* cJSON_CreateArray(void);
cJSON_bool cJSON_AddItemToArray(cJSON *array, cJSON *item);
cJSON* cJSON_AddStringToObject(cJSON * const object, const char * const name, const char * const string);
cJSON* cJSON_AddNullToObject(cJSON * const object, const char * const name);
cJSON * cJSON_CreateString(const char *string);
cJSON_bool cJSON_AddItemToObject(cJSON *object, const char *string, cJSON *item);
cJSON_bool cJSON_IsString(const cJSON * const item);
cJSON_bool cJSON_IsBool(const cJSON * const item);
cJSON_bool cJSON_IsTrue(const cJSON * const item);
cJSON_bool cJSON_IsNumber(const cJSON * const item);
cJSON_bool cJSON_IsArray(const cJSON * const item);
cJSON_bool cJSON_IsObject(const cJSON * const item);
cJSON_bool cJSON_IsNull(const cJSON * const item);
int cJSON_GetArraySize(const cJSON *array);
cJSON * cJSON_GetArrayItem(const cJSON *array, int index);
cJSON * cJSON_GetObjectItem(const cJSON * const object, const char * const string);
char* cJSON_Print(const cJSON *item);
char* cJSON_PrintUnformatted(const cJSON *item);
cJSON * cJSON_ParseWithLength(const char *value, size_t buffer_length);
cJSON * cJSON_Parse(const char *value);
const char * cJSON_GetErrorPtr(void);
cJSON *cJSON_AddArrayToObject(cJSON * const object, const char * const name);
cJSON *cJSON_AddNumberToObject(cJSON * const object, const char * const name, const double number);
cJSON *cJSON_AddBoolToObject(cJSON * const object, const char * const name, const cJSON_bool boolean);
cJSON *cJSON_AddObjectToObject(cJSON * const object, const char * const name);
void cJSON_Delete(cJSON *item);