// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

#include "unity.h"
#include "s2_client.h"
#include "s2_client_http.h"
#include <stdbool.h>
#include "esp_http_client_stubs.h"
#include "esp_websocket_client_stubs.h"

#include "fixtures.h"

#define SERVER_URL "https://homeassistant.local:8123/api/s2_pebc"

// ______________________ s2_client_init ______________________

/** Minimal dummy callbacks required by s2_client_init validation */
static void dummy_on_session_ready(void) {}
static void dummy_on_request_measurement(s2_commodity_quantity_t q) {}
static void dummy_on_instruction(s2_commodity_quantity_t q, float upper, float lower) {}
static void dummy_on_request_param(s2_pebc_param_t p, s2_commodity_quantity_t q, bool abnormal) {}

/** Default S2 client configuration that can be passed to the init function */
static s2_client_config_t get_default_config(void) {
    s2_client_config_t cfg = {
        .node = {
            .id = "abff635e-cd69-4350-b10f-a990b7920ba0",
            .brand = "test",
            .deployment = S2_DEPLOYMENT_LAN,
            .logo_uri = "test",
            .model = "test",
            .role = S2_ROLE_RM,
            .supported_versions = {"0.0.1-beta"},
            .type = "test",
            .user_defined_name = "test"
        },
        .server_config = {
            .base_url = SERVER_URL,
            .interface = "eth0"
        },
        .rm_config = {
            .control_type = S2_CONTROL_TYPE_PEBC,
            .roles = {{.commodity = S2_COMMODITY_ELECTRICITY, .role_type = S2_ROLE_TYPE_ENERGY_CONSUMER}},
            .roles_count = 1,
            .commodity_quantities = {S2_COMMODITY_QUANTITY_ELECTRIC_POWER_L1},
            .commodity_quantities_count = 1,
            .on_request_measurement = dummy_on_request_measurement,
            .on_session_ready = dummy_on_session_ready,
            .pebc = {
                .default_limits = {{.commodity_quantity = S2_COMMODITY_QUANTITY_ELECTRIC_POWER_L1}},
                .default_limits_count = 1,
                .on_instruction = dummy_on_instruction,
                .on_request_param = dummy_on_request_param,
            },
        }
    };
    return cfg;
}

TEST_CASE("Returns NULL if no config is passed", "[S2 client - init]") {
    TEST_ASSERT_NULL(s2_client_init(NULL));
}

TEST_CASE("Returns NULL if node id is missing", "[S2 client - init]") {
    s2_client_config_t cfg = get_default_config();
    cfg.node.id[0] = '\0';
    TEST_ASSERT_NULL(s2_client_init(&cfg));
}

TEST_CASE("Returns NULL if node brand is missing", "[S2 client - init]") {
    s2_client_config_t cfg = get_default_config();
    cfg.node.brand[0] = '\0';
    TEST_ASSERT_NULL(s2_client_init(&cfg));
}

TEST_CASE("Returns NULL if node type is missing", "[S2 client - init]") {
    s2_client_config_t cfg = get_default_config();
    cfg.node.type[0] = '\0';
    TEST_ASSERT_NULL(s2_client_init(&cfg));
}

TEST_CASE("Returns NULL if node model is missing", "[S2 client - init]") {
    s2_client_config_t cfg = get_default_config();
    cfg.node.model[0] = '\0';
    TEST_ASSERT_NULL(s2_client_init(&cfg));
}

TEST_CASE("Returns NULL if no supported versions are defined", "[S2 client - init]") {
    s2_client_config_t cfg = get_default_config();
    for (int i = 0; i < S2_NODE_MAX_SUPPORTED_VERSIONS; i++) {
        cfg.node.supported_versions[i][0] = '\0';
    }
    TEST_ASSERT_NULL(s2_client_init(&cfg));
}

TEST_CASE("Returns NULL if server base URL is missing", "[S2 client - init]") {
    s2_client_config_t cfg = get_default_config();
    cfg.server_config.base_url[0] = '\0';
    TEST_ASSERT_NULL(s2_client_init(&cfg));
}

TEST_CASE("Returns valid handle when TLS enabled without cert (TOFU)", "[S2 client - init]") {
    s2_client_config_t cfg = get_default_config();
    cfg.server_config.enable_tls = true;
    cfg.server_config.cert_pem = NULL;
    cfg.server_config.cert_pem_len = 0;

    mock_queue_init(CREATE_AND_IGNORE);
    mock_task_init(CREATE_AND_IGNORE);
    MockHttpClient http_client; mock_http_client_init(&http_client, CREATE_AND_IGNORE);

    TEST_ASSERT_NOT_NULL(s2_client_init(&cfg));
}

TEST_CASE("Returns valid handle when queue and task creation succeed", "[S2 client - init]") {
    s2_client_config_t cfg = get_default_config();

    mock_queue_init(CREATE_AND_IGNORE);
    mock_task_init(CREATE_AND_IGNORE);
    MockHttpClient http_client; mock_http_client_init(&http_client, CREATE_AND_IGNORE);

    TEST_ASSERT_NOT_NULL(s2_client_init(&cfg));
}

TEST_CASE("Returns NULL if request queue creation fails", "[S2 client - init]") {
    s2_client_config_t cfg = get_default_config();

    mock_queue_init(CREATE_AND_FAIL);
    mock_task_init(CREATE_AND_IGNORE);
    MockHttpClient http_client; mock_http_client_init(&http_client, CREATE_AND_IGNORE);
    esp_http_client_cleanup_IgnoreAndReturn(ESP_OK);
    
    TEST_ASSERT_NULL(s2_client_init(&cfg));
}

TEST_CASE("Returns NULL if HTTP task creation fails", "[S2 client - init]") {
    s2_client_config_t cfg = get_default_config();

    mock_queue_init(CREATE_AND_IGNORE);
    mock_task_init(CREATE_AND_FAIL);
    MockHttpClient http_client; mock_http_client_init(&http_client, CREATE_AND_IGNORE);
    esp_http_client_cleanup_IgnoreAndReturn(ESP_OK);
    
    TEST_ASSERT_NULL(s2_client_init(&cfg));
}

TEST_CASE("Returns NULL if HTTP client init fails", "[S2 client - init]") {
    s2_client_config_t cfg = get_default_config();

    // Ignore queue & task
    mock_queue_init(CREATE_AND_IGNORE);
    mock_task_init(CREATE_AND_IGNORE);
    MockHttpClient http_client; mock_http_client_init(&http_client, CREATE_AND_FAIL);

    TEST_ASSERT_NULL(s2_client_init(&cfg));
}

TEST_CASE("Interface is correctly configured in HTTP client when passed", "[S2 client - init]") {
    s2_client_config_t cfg = get_default_config();

    // Ignore queue & task
    mock_queue_init(CREATE_AND_IGNORE);
    mock_task_init(CREATE_AND_IGNORE);

    // Record HTTP client init call to check if interface is set
    http_client_record_init();

    // Initialize S2 client
    s2_client_init(&cfg);

    // Verify interface is set
    TEST_ASSERT_TRUE(http_client_was_iface_set(cfg.server_config.interface));
}

TEST_CASE("Interface is skipped when not passed", "[S2 client - init]") {
    s2_client_config_t cfg = get_default_config();
    cfg.server_config.interface[0] = '\0';

    // Ignore queue & task
    mock_queue_init(CREATE_AND_IGNORE);
    mock_task_init(CREATE_AND_IGNORE);

    // Record HTTP client init call to check if interface is set
    http_client_record_init();

    // Initialize S2 client
    s2_client_init(&cfg);

    // Verify interface is NOT set
    TEST_ASSERT_TRUE(http_client_was_iface_set(NULL));
}

TEST_CASE("Certificates are configured when TLS is enabled", "[S2 client - init]") {
    s2_client_config_t cfg = get_default_config();
    cfg.server_config.enable_tls = true;
    cfg.server_config.cert_pem = "CERTIFICATE";
    cfg.server_config.cert_pem_len = 12;

    // Ignore queue & task
    mock_queue_init(CREATE_AND_IGNORE);
    mock_task_init(CREATE_AND_IGNORE);

    // Record HTTP client init call to check if certificate is set
    http_client_record_init();

    // Initialize S2 client
    s2_client_init(&cfg);

    // Verify certificate is set
    TEST_ASSERT_TRUE(http_client_was_cert_set(cfg.server_config.cert_pem));
}

TEST_CASE("Copies maximum length strings correctly", "[S2 client - init]") {
    s2_client_config_t cfg = get_default_config();
    
    strcpy(cfg.node.id, "123e4567-e89b-12d3-a456-426614174000");
    
    // Fill brand with 63 'B's
    memset(cfg.node.brand, 'B', 63);
    cfg.node.brand[63] = '\0';

    // Mock dependencies
    mock_queue_init(CREATE_AND_IGNORE);
    mock_task_init(CREATE_AND_IGNORE);
    MockHttpClient http_client; mock_http_client_init(&http_client, CREATE_AND_IGNORE);

    s2_client_handle_t handle = s2_client_init(&cfg);
    
    TEST_ASSERT_NOT_NULL(handle);
    TEST_ASSERT_EQUAL_STRING(cfg.node.id, handle->node.id);
    TEST_ASSERT_EQUAL_STRING(cfg.node.brand, handle->node.brand);
}

TEST_CASE("Handles supported version string length boundaries", "[S2 client - init]") {
    s2_client_config_t cfg = get_default_config();
    
    // Case A: A "Short" version (Normal behavior)
    strcpy(cfg.node.supported_versions[0], "1.0");
    
    // Case B: A "Maximum" version (31 characters + '\0')
    // This tests the "if (strlen < sizeof)" logic
    char max_ver[32];
    memset(max_ver, 'V', 31);
    max_ver[31] = '\0';
    strcpy(cfg.node.supported_versions[1], max_ver);

    mock_queue_init(CREATE_AND_IGNORE);
    mock_task_init(CREATE_AND_IGNORE);
    MockHttpClient http_client; mock_http_client_init(&http_client, CREATE_AND_IGNORE);

    s2_client_handle_t handle = s2_client_init(&cfg);

    TEST_ASSERT_NOT_NULL(handle);
    TEST_ASSERT_EQUAL_STRING("1.0", handle->node.supported_versions[0]);
    TEST_ASSERT_EQUAL_STRING(max_ver, handle->node.supported_versions[1]);
}

TEST_CASE("Handles non-null terminated strings gracefully", "[S2 client - init]") {
    s2_client_config_t cfg = get_default_config();
    
    // Simulate a "dirty" brand string where the terminator was missed
    // We fill 64 chars with 'X'. There is no room for a '\0'.
    // node_config_valid rejects this (strnlen check), so init returns NULL.
    memset(cfg.node.brand, 'X', 64); 
    
    // No mock setup needed - validation rejects before any resource creation

    // The validation should reject this as the string has no null terminator
    TEST_ASSERT_NULL(s2_client_init(&cfg));
}

TEST_CASE("States are initialized with their defaults", "[S2 client - init]") {
    s2_client_config_t cfg = get_default_config();

    // Ignore mocks
    mock_queue_init(CREATE_AND_IGNORE);
    mock_task_init(CREATE_AND_IGNORE);
    MockHttpClient http_client; mock_http_client_init(&http_client, CREATE_AND_IGNORE);

    s2_client_handle_t handle = s2_client_init(&cfg);

    TEST_ASSERT_EQUAL(S2_CLIENT_WEBSOCKET_CONN_STATE_DISCONNECTED, handle->websocket_conn_state);
}

TEST_CASE("Creates required handles (e.g. task, queue etc.)", "[S2 client - init]") {
    s2_client_config_t cfg = get_default_config();

    // Test if queue, task and HTTP client have been created
    mock_queue_init(CREATE_AND_SUCCEED);
    mock_task_init(CREATE_AND_SUCCEED);
    MockHttpClient http_client; mock_http_client_init(&http_client, CREATE_AND_SUCCEED);

    // Make sure it returns a handle, not NULL
    TEST_ASSERT_NOT_NULL(s2_client_init(&cfg));
}

TEST_CASE("Copies configuration into created handle", "[S2 client - init]") {
    s2_client_config_t cfg = get_default_config();

    // Ignore mocks
    mock_queue_init(CREATE_AND_IGNORE);
    mock_task_init(CREATE_AND_IGNORE);
    MockHttpClient http_client; mock_http_client_init(&http_client, CREATE_AND_IGNORE);

    s2_client_handle_t handle = s2_client_init(&cfg);

    TEST_ASSERT_EQUAL_STRING(cfg.node.id, handle->node.id);
    TEST_ASSERT_EQUAL_STRING(cfg.node.brand, handle->node.brand);
    TEST_ASSERT_EQUAL_STRING(cfg.node.logo_uri, handle->node.logo_uri);
    TEST_ASSERT_EQUAL_STRING(cfg.node.type, handle->node.type);
    TEST_ASSERT_EQUAL_STRING(cfg.node.model, handle->node.model);
    TEST_ASSERT_EQUAL_STRING(cfg.node.user_defined_name, handle->node.user_defined_name);
    TEST_ASSERT_EQUAL(cfg.node.role, handle->node.role);
    TEST_ASSERT_EQUAL(cfg.node.deployment, handle->node.deployment);
    TEST_ASSERT_EQUAL_STRING(cfg.node.supported_versions[0], handle->node.supported_versions[0]);
    TEST_ASSERT_EQUAL_STRING(cfg.server_config.base_url, handle->server.base_url);
}

// ______________________ s2_client_init_http ______________________

TEST_CASE("Returns false if client creation failed", "[S2 client - HTTP init]") {
    // Create client handle for test
    s2_client_config_t cfg = get_default_config();
    mock_queue_init(CREATE_AND_IGNORE);
    mock_task_init(CREATE_AND_IGNORE);
    MockHttpClient http_client; mock_http_client_init(&http_client, CREATE_AND_IGNORE);
    s2_client_handle_t handle = s2_client_init(&cfg);

    // HTTP client init should fail
    esp_http_client_init_StopIgnore();
    esp_http_client_init_ExpectAnyArgsAndReturn(NULL);
    TEST_ASSERT_FALSE(s2_client_init_http(handle, &cfg));
}

TEST_CASE("Returns false if request queue creation failed", "[S2 client - HTTP init]") {
    // Create client handle for test
    s2_client_config_t cfg = get_default_config();
    mock_queue_init(CREATE_AND_IGNORE);
    mock_task_init(CREATE_AND_IGNORE);
    MockHttpClient http_client; mock_http_client_init(&http_client, CREATE_AND_IGNORE);
    s2_client_handle_t handle = s2_client_init(&cfg);

    // Request queue creation should fail
    xQueueGenericCreate_StopIgnore();
    xQueueGenericCreate_ExpectAnyArgsAndReturn(NULL);
    esp_http_client_cleanup_ExpectAnyArgsAndReturn(ESP_OK);
    TEST_ASSERT_FALSE(s2_client_init_http(handle, &cfg));
}

TEST_CASE("Returns false if task creation creation failed", "[S2 client - HTTP init]") {
    // Create client handle for test
    s2_client_config_t cfg = get_default_config();
    mock_queue_init(CREATE_AND_IGNORE);
    mock_task_init(CREATE_AND_IGNORE);
    MockHttpClient http_client; mock_http_client_init(&http_client, CREATE_AND_IGNORE);
    s2_client_handle_t handle = s2_client_init(&cfg);

    // HTTP task creation should fail
    xTaskCreatePinnedToCore_StopIgnore();
    xTaskCreatePinnedToCore_ExpectAnyArgsAndReturn(pdFALSE);
    esp_http_client_cleanup_ExpectAnyArgsAndReturn(ESP_OK);
    TEST_ASSERT_FALSE(s2_client_init_http(handle, &cfg));
}

// ______________________ s2_client_destroy ______________________

TEST_CASE("Frees all resources created during client initialization", "[S2 client - destroy]") {
    // Create a client handle
    s2_client_config_t cfg = get_default_config();

    // Ignore mocks
    mock_queue_init(CREATE_AND_IGNORE);
    mock_task_init(CREATE_AND_IGNORE);
    MockHttpClient http_client; mock_http_client_init(&http_client, CREATE_AND_IGNORE);

    s2_client_handle_t handle = s2_client_init(&cfg);

    // Make sure all resources are freed (WebSocket resources not created yet)
    esp_http_client_cleanup_ExpectAnyArgsAndReturn(ESP_OK);
    vQueueDelete_ExpectAnyArgs();

    s2_client_destroy(handle);
}