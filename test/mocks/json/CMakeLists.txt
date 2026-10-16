# SPDX-FileCopyrightText: ElaadNL
#
# SPDX-License-Identifier: Apache-2.0

message(STATUS "building JSON MOCKS")

idf_component_get_property(original_component_dir json COMPONENT_OVERRIDEN_DIR)

set(cexception_src "$ENV{IDF_PATH}/components/cmock/CMock/vendor/c_exception/lib")

set(include_dirs
    "include"
    "${original_component_dir}/cJSON"
    "${cexception_src}"
)

idf_component_mock(
    INCLUDE_DIRS ${include_dirs}
    MOCK_HEADER_FILES ${CMAKE_CURRENT_SOURCE_DIR}/include/cJSON_mock.h
)

target_sources(${COMPONENT_LIB} PRIVATE "${cexception_src}/CException.c")
