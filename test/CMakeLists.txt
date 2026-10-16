# SPDX-FileCopyrightText: ElaadNL
#
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

option(ENABLE_COVERAGE "Build tests with coverage instrumentation" OFF)

set(COMPONENTS main)
set(EXTRA_COMPONENT_DIRS "..")
list(APPEND EXTRA_COMPONENT_DIRS "$ENV{IDF_PATH}/tools/mocks/freertos/")
list(APPEND EXTRA_COMPONENT_DIRS "$ENV{IDF_PATH}/tools/mocks/esp_event/")
list(APPEND EXTRA_COMPONENT_DIRS "mocks/esp_http_client/")
list(APPEND EXTRA_COMPONENT_DIRS "mocks/json/")
list(APPEND EXTRA_COMPONENT_DIRS "mocks/cjson/")
list(APPEND EXTRA_COMPONENT_DIRS "mocks/esp_timer/")
list(APPEND EXTRA_COMPONENT_DIRS "mocks/mdns/")
list(APPEND EXTRA_COMPONENT_DIRS "mocks/nvs_flash/")

if(IDF_TARGET STREQUAL "linux")
    if(ENABLE_COVERAGE)
        message(STATUS "Enabling coverage instrumentation for tests")
        # Instrument compile and link for gcov/gcovr
        add_compile_options(-fprofile-arcs -ftest-coverage)
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} --coverage -Wl,-z,execstack -Wl,--wrap=esp_websocket_client_send_text -Wl,--wrap=esp_websocket_client_destroy -Wl,--wrap=esp_websocket_client_stop -Wl,--wrap=esp_websocket_client_init -Wl,--wrap=esp_websocket_register_events -Wl,--wrap=esp_websocket_client_start -Wl,--wrap=s2_client_websocket_connect -Wl,--wrap=s2_client_initiate_session -Wl,--wrap=s2_client_confirm_access_token_extended")
    else()
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-z,execstack -Wl,--wrap=esp_websocket_client_send_text -Wl,--wrap=esp_websocket_client_destroy -Wl,--wrap=esp_websocket_client_stop -Wl,--wrap=esp_websocket_client_init -Wl,--wrap=esp_websocket_register_events -Wl,--wrap=esp_websocket_client_start -Wl,--wrap=s2_client_websocket_connect -Wl,--wrap=s2_client_initiate_session -Wl,--wrap=s2_client_confirm_access_token_extended")
    endif()
endif()

project(s2_tests)