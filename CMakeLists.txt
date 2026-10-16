# SPDX-FileCopyrightText: ElaadNL
#
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.16)

# S2 component name
# Can be overridden from CI or the command line with -DS2_COMPONENT_NAME=...
if(NOT DEFINED S2_COMPONENT_NAME)
    if(DEFINED CMAKE_PROJECT_NAME)
        set(S2_COMPONENT_NAME "${CMAKE_PROJECT_NAME}" CACHE STRING "S2 component name (override with -DS2_COMPONENT_NAME=...)")
    else()
        get_filename_component(_s2_name "${CMAKE_CURRENT_SOURCE_DIR}" NAME)
        set(S2_COMPONENT_NAME "${_s2_name}" CACHE STRING "S2 component name (override with -DS2_COMPONENT_NAME=...)")
    endif()
endif()

# ESP-specific dependencies (not available on linux target)
if(NOT IDF_TARGET STREQUAL "linux")
    set(s2_esp_requires "nvs_flash" "mdns" "mbedtls")
else()
    set(s2_esp_requires "")
endif()

idf_component_register(
    SRCS
        "src/s2_client.c"
        "src/s2_client_http.c"
        "src/s2_client_ws.c"
        "src/log_forwarder.c"
        "src/s2_client_pairing.c"
        "src/s2_common.c"
        "src/s2_messages.c"
        "src/s2_pebc_logic.c"
        "src/s2_crypto.c"
        "src/s2_pairing_challenge.c"
        "src/s2_dns_sd.c"
        "src/s2_long_poll.c"
    INCLUDE_DIRS
        "include"
        "src"
    PRIV_INCLUDE_DIRS
        "src"
    REQUIRES
        "esp_http_client"
        "esp_timer"
        "cjson"
        "esp_websocket_client"
        ${s2_esp_requires}
)

target_compile_definitions(${COMPONENT_LIB} PRIVATE _POSIX_C_SOURCE=200809L)

if(IDF_TARGET STREQUAL "linux")
    target_compile_options(${COMPONENT_LIB} PRIVATE --coverage -O0)
endif()
