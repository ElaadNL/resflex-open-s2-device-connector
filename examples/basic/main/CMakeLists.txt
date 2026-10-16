# SPDX-FileCopyrightText: ElaadNL
#
# SPDX-License-Identifier: Apache-2.0

set(EXAMPLE_ENV_FILE "${CMAKE_CURRENT_LIST_DIR}/../.env")
set(EXAMPLE_ENV_TEMPLATE "${CMAKE_CURRENT_LIST_DIR}/../.env.example")
set(EXAMPLE_ENV_SOURCE "${EXAMPLE_ENV_FILE}")

if(NOT EXISTS "${EXAMPLE_ENV_FILE}")
    if(EXISTS "${EXAMPLE_ENV_TEMPLATE}")
        message(WARNING
            "${EXAMPLE_ENV_FILE} not found. Falling back to ${EXAMPLE_ENV_TEMPLATE} for build-time defaults."
        )
        set(EXAMPLE_ENV_SOURCE "${EXAMPLE_ENV_TEMPLATE}")
    else()
        message(WARNING
            "No .env or .env.example found. Using internal placeholder defaults for build-only."
        )
        set(EXAMPLE_ENV_SOURCE "")
    endif()
endif()

set(EXAMPLE_WIFI_SSID "")
set(EXAMPLE_WIFI_PASSWORD "")
set(EXAMPLE_CEM_S2_INTEGRATION_URL "")
set(EXAMPLE_PAIRING_ACCESS_TOKEN "")

if(EXAMPLE_ENV_SOURCE STREQUAL "")
    set(EXAMPLE_ENV_CONTENT "")
else()
    file(READ "${EXAMPLE_ENV_SOURCE}" EXAMPLE_ENV_CONTENT)
    string(REPLACE "\r\n" "\n" EXAMPLE_ENV_CONTENT "${EXAMPLE_ENV_CONTENT}")
    string(REPLACE "\r" "\n" EXAMPLE_ENV_CONTENT "${EXAMPLE_ENV_CONTENT}")
endif()

function(_get_env_value KEY OUT_VAR)
    string(REGEX MATCH "(^|\n)[ \t]*${KEY}[ \t]*=[ \t]*([^\n]*)" _match "${EXAMPLE_ENV_CONTENT}")
    if(_match)
        set(_val "${CMAKE_MATCH_2}")
        string(REGEX REPLACE "[ \t]*#.*$" "" _val "${_val}")
        string(STRIP "${_val}" _val)
        if(_val MATCHES "^\"(.*)\"$")
            set(_val "${CMAKE_MATCH_1}")
        endif()
    else()
        set(_val "")
    endif()
    set(${OUT_VAR} "${_val}" PARENT_SCOPE)
endfunction()

function(_sanitize_c_string IN OUT_VAR)
    set(_v "${IN}")
    string(REPLACE "\\" "\\\\" _v "${_v}")
    string(REPLACE "\"" "\\\"" _v "${_v}")
    set(${OUT_VAR} "${_v}" PARENT_SCOPE)
endfunction()

_get_env_value("WIFI_SSID" EXAMPLE_WIFI_SSID)
_get_env_value("WIFI_PASSWORD" EXAMPLE_WIFI_PASSWORD)
_get_env_value("CEM_S2_INTEGRATION_URL" EXAMPLE_CEM_S2_INTEGRATION_URL)
_get_env_value("PAIRING_ACCESS_TOKEN" EXAMPLE_PAIRING_ACCESS_TOKEN)

if(EXAMPLE_WIFI_SSID STREQUAL "")
    set(EXAMPLE_WIFI_SSID "example-ssid")
    message(WARNING "WIFI_SSID is empty in ${EXAMPLE_ENV_SOURCE}; using placeholder for compile-time build.")
endif()

if(EXAMPLE_WIFI_PASSWORD STREQUAL "")
    set(EXAMPLE_WIFI_PASSWORD "example-password")
    message(WARNING "WIFI_PASSWORD is empty in ${EXAMPLE_ENV_SOURCE}; using placeholder for compile-time build.")
endif()

if(EXAMPLE_CEM_S2_INTEGRATION_URL STREQUAL "")
    set(EXAMPLE_CEM_S2_INTEGRATION_URL "https://homeassistant.local:8123/api/s2_pebc")
    message(WARNING "CEM_S2_INTEGRATION_URL is empty in ${EXAMPLE_ENV_SOURCE}; using placeholder for compile-time build.")
endif()

if(EXAMPLE_PAIRING_ACCESS_TOKEN STREQUAL "")
    set(EXAMPLE_PAIRING_ACCESS_TOKEN "")
    message(WARNING "PAIRING_ACCESS_TOKEN is empty in ${EXAMPLE_ENV_SOURCE}; pairing request will fail at runtime until set.")
endif()

_sanitize_c_string("${EXAMPLE_WIFI_SSID}" EXAMPLE_WIFI_SSID)
_sanitize_c_string("${EXAMPLE_WIFI_PASSWORD}" EXAMPLE_WIFI_PASSWORD)
_sanitize_c_string("${EXAMPLE_CEM_S2_INTEGRATION_URL}" EXAMPLE_CEM_S2_INTEGRATION_URL)
_sanitize_c_string("${EXAMPLE_PAIRING_ACCESS_TOKEN}" EXAMPLE_PAIRING_ACCESS_TOKEN)

configure_file(
    "${CMAKE_CURRENT_LIST_DIR}/env_config.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/env_config.h"
    @ONLY
)

get_filename_component(_s2_root "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
get_filename_component(S2_COMPONENT_NAME "${_s2_root}" NAME)

# Recursively collect all C source files
idf_component_register(
    SRCS
        "main.c"
    INCLUDE_DIRS
        "."
        "${CMAKE_CURRENT_BINARY_DIR}"
    PRIV_REQUIRES
        "esp_wifi"
        "wpa_supplicant"
        "nvs_flash"
        "espressif__cjson"
        "${S2_COMPONENT_NAME}"
)
