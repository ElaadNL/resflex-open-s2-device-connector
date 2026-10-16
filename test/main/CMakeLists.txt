# SPDX-FileCopyrightText: ElaadNL
#
# SPDX-License-Identifier: Apache-2.0

idf_component_register(
    SRCS
        "test_main.c"
        "test_s2_client.c"
        "test_s2_client_pairing.c"
        "test_s2_client_ws.c"
        "test_s2_client_http.c"
        "test_s2_common.c"
        "test_s2_pebc_logic.c"
        "test_log_forwarder.c"
        "test_s2_client_ws_reconnect.c"
        "test_s2_long_poll.c"
        "test_s2_crypto.c"
        "test_s2_pairing_challenge.c"
        "test_s2_dns_sd.c"
        "../stubs/esp_http_client_stubs.c"
        "../stubs/esp_websocket_client_stubs.c"
        "../stubs/json_stubs.c"
        "../stubs/freertos_stubs.c"
        "../stubs/s2_client_initiate_stubs.c"
        "../stubs/s2_client_no_verify_stub.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/s2_client.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/s2_client_http.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/s2_client_ws.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/log_forwarder.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/s2_client_pairing.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/s2_common.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/s2_messages.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/s2_pebc_logic.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/s2_long_poll.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/s2_crypto.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/s2_pairing_challenge.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/s2_dns_sd.c"
        
    INCLUDE_DIRS
        "."
        "../stubs"
        "../mocks/esp_timer"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../include"
    PRIV_REQUIRES
        cmock
        cjson
        esp_http_client
        esp_websocket_client
        json
        mdns
        nvs_flash
        unity
        ${S2_COMPONENT_NAME}
        ${S2_COMPONENT_NAME}
    WHOLE_ARCHIVE
)

if(IDF_TARGET STREQUAL "linux")
    target_compile_options(${COMPONENT_LIB} PRIVATE --coverage -O0)
endif()
