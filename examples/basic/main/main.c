// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_sntp.h"
#include "env_config.h"
#include "s2_client.h"
#include "s2_client_internal.h"
#include "s2_client_http.h"
#include "s2_client_pairing.h"
#include "s2_crypto.h"
#include "s2_pairing_challenge.h"
#include "log_forwarder.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "rom/ets_sys.h"
#include <psa/crypto.h>

#include "s2_dns_sd.h"
#include "s2_long_poll.h"
#include "mdns.h"
#include <stdarg.h>

/* Wi-Fi connection state */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1
#define MAX_RETRY          5

// max CEMs to collect during DNS-SD discovery
#define MAX_CEMS 4

static const char *TAG = "example";

// toggles used for testing purposes.
// enable to revoke first set of constraints after REVOKE_DELAY_MS for testing
#define TEST_REVOKE_AFTER_CONSTRAINTS false
// enable to abort second received instruction for  testing
#define TEST_ABORT_ON_SECOND_INSTRUCTION false

// demo mode: exactly one must be 1
// RESPONDER_NODE_DEMO - RM has no HTTP server, only long polling is possible
// INITIATOR_NODE_DEMO - RM starts pairing as the HTTPS client
#define RESPONDER_NODE_DEMO 1
#define INITIATOR_NODE_DEMO 0
#if (RESPONDER_NODE_DEMO + INITIATOR_NODE_DEMO) != 1
#error "enable exactly one of RESPONDER_NODE_DEMO / INITIATOR_NODE_DEMO"
#endif

// wait until revoking constraints
#define REVOKE_DELAY_MS 8000

/* Global handle used by callbacks to provide values back to the library */
static s2_client_handle_t s2_handle = NULL;

/* Forward native ESP logs into the log_forwarder so they are sent to CEM when enabled.
 * We wrap the vprintf hook so the original log output still occurs.
 */
static int (*original_vprintf)(const char *fmt, va_list ap) = NULL;

static int forwarding_vprintf(const char *fmt, va_list ap) {
    char buf[256];
    va_list ap2;
    va_copy(ap2, ap);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap2);
    va_end(ap2);
    if (n > 0) {
        if ((size_t)n >= sizeof(buf)) buf[sizeof(buf)-1] = '\0';
        /* Trim trailing newline for cleaner forwarded messages */
        if (n > 0 && buf[n-1] == '\n') buf[n-1] = '\0';

        /* Determine abbreviated log level from the vprintf prefix: 'I','W','E' */
        char lvl = buf[0];
        bool is_s2_client = strstr(buf, "s2_client_websocket:") != NULL;

        /* Filter noisy protocol traces and internal s2 library INFO logs to avoid
         * filling the forwarder queue and creating echo loops. We still forward
         * WARN/ERROR messages from s2_client_websocket.
         */
        if (is_s2_client && lvl == 'I') {
            // drop routine info-level s2_client_websocket traces
        } else {
            const char *src = "ESP";
            /* Forward WARN/ERROR as INFO-level to the forwarder but preserve source */
            log_forwarder_enqueue(lvl == 'W' ? "WARN" : (lvl == 'E' ? "ERROR" : "INFO"), src, buf);
        }
    }
    if (original_vprintf) {
        return original_vprintf(fmt, ap);
    }
    return vprintf(fmt, ap);
}

#if TEST_REVOKE_AFTER_CONSTRAINTS
static void revoke_after_constraints_task(void *arg) {
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(REVOKE_DELAY_MS));
    esp_err_t err = s2_client_pebc_revoke_constraints(s2_handle);
    if (err == ESP_OK)
        ESP_LOGI(TAG, "TEST: sent RevokeObject for constraints to CEM");
    else
        ESP_LOGW(TAG, "TEST: revoke constraints returned err=0x%x", (unsigned int)err);
    vTaskDelete(NULL);
}
#endif

/* Simulated device state: upper power limit per commodity quantity, indexed by s2_commodity_quantity_t.
 * The library calls on_instruction once per quantity present in the CEM's instruction,
 * so we need one slot per quantity to track them independently. */
static float active_upper_limit_w[10] = {
    [S2_COMMODITY_QUANTITY_ELECTRIC_POWER_L1] = 3680.0f,
    [S2_COMMODITY_QUANTITY_ELECTRIC_POWER_L2] = 3680.0f,
    [S2_COMMODITY_QUANTITY_ELECTRIC_POWER_L3] = 3680.0f,
};

/* Event group processing Wi-Fi events */
static EventGroupHandle_t wifi_event_group;
static int retry_count = 0;

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (retry_count < MAX_RETRY) {
            esp_wifi_connect();
            retry_count++;
            ESP_LOGI(TAG, "Retrying connection... (%d/%d)", retry_count, MAX_RETRY);
        } else {
            xEventGroupSetBits(wifi_event_group, WIFI_FAIL_BIT);
            ESP_LOGE(TAG, "Connection failed after %d retries", MAX_RETRY);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        retry_count = 0;
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

void wifi_init(void) {
    wifi_event_group = xEventGroupCreate();

    // Create default STA interface
    esp_netif_create_default_wifi_sta();

    // Initialize Wi-Fi driver with default configuration
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // Configure Wi-Fi with WPA2-PSK
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASSWORD,
        }
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));

    /* Register event handlers for Wi-Fi and IP events */
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    // Start Wi-Fi network
    ESP_ERROR_CHECK(esp_wifi_start());

    // Wait till we are connected
    ESP_LOGI(TAG, "Connecting to \"%s\"...", WIFI_SSID);

    EventBits_t bits = xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, pdFALSE, pdFALSE, portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Connected successfully.");
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGE(TAG, "Failed to connect.");
    }
}

/* Sync wall-clock time via NTP. The S2 library uses time() for valid_from timestamps. */
static void sync_time(void) {
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_init();
    /* one lost response would leave the clock at 1970, so ask again every few seconds */
    time_t now = 0;
    for (int i = 0; i < 30; i++) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        if (time(&now) >= 1000000000L) {
            ESP_LOGI(TAG, "NTP sync OK: %lld", (long long)now);
            return;
        }
        if (i % 5 == 4) (void)esp_sntp_restart();
    }
    ESP_LOGW(TAG, "NTP sync timed out - timestamps will be incorrect");
}

// called when an instruction becomes active (CEM limits) or expires (back to config defaults).
// real impl: write limits to hardware here. if that fails, call s2_client_pebc_abort_instruction().
//
// This callback fires in two situations:
//   1. Instruction STARTED  -> upper_limit/lower_limit are the CEM's instructed values.
//   2. Instruction REVOKED -> upper_limit/lower_limit are your config default_limits.
//      Use this to reset hardware back to its normal operating range.
//
// NOTES:   called once PER QUANTITY present in the instruction. Always use `quantity` value to apply
//          limits to the correct channel.
//          abnormal_condition is handled inside the library. If the CEM sends an instruction with
//          abnormal_condition = true, this callback fires using the constraints that have abnormal_condition_only = true
//          (the library validates them against abnormal_condition_only constraint limits in config).
//          You do not need to distinguish abnormal vs normal here just apply whatever limits arrive.
//          Library handles all validation of abnormal/normal and if instruction is within constraints.
static void on_instruction(s2_commodity_quantity_t quantity, float upper_limit, float lower_limit) {
    ESP_LOGI(TAG, "on_instruction: quantity=%d upper=%.1fW lower=%.1fW", (int)quantity, upper_limit, lower_limit);
    active_upper_limit_w[quantity] = upper_limit;
    ESP_LOGI(TAG, "on_instruction: quantity %d now capped at %.1fW", (int)quantity, active_upper_limit_w[quantity]);

    #if TEST_ABORT_ON_SECOND_INSTRUCTION
        static bool abort_test_fired = false;
        static int instruction_count = 0;
        // abort second instructed (non-default) L1 callback to test ABORTED flow.
        if (quantity == S2_COMMODITY_QUANTITY_ELECTRIC_POWER_L1 && upper_limit < 3679.99f) {
            instruction_count++;
            ESP_LOGI(TAG, "TEST: instruction count=%d", instruction_count);
            if (!abort_test_fired && instruction_count == 2) {
                abort_test_fired = true;
                esp_err_t err = s2_client_pebc_abort_instruction(s2_handle);
                if (err == ESP_OK)
                    ESP_LOGI(TAG, "TEST: sent ABORTED InstructionStatusUpdate on second instruction");
                else
                    ESP_LOGW(TAG, "TEST: abort instruction returned err=0x%x", (unsigned int)err);
            }
        }
    #endif
}

// called by library when it needs a power measurement. read value from hardware and provide it back.
// hardcoded here for a 16A single-phase EV charger (3680W max). real impl: read from modbus etc.
static void on_request_measurement(s2_commodity_quantity_t quantity) {
    // simulate device listening to instruction
    float cap = active_upper_limit_w[quantity];
    float draw = 1000.0f < cap ? 1000.0f : cap;
    s2_client_provide_measurement(s2_handle, quantity, draw);
}

// called by library when it needs a power forecast. only called if provides_forecast = true.
static void on_request_forecast(s2_commodity_quantity_t quantity) {
    s2_client_provide_forecast(s2_handle, quantity, 2000.0f);
}

// called by library before sending each PEBC constraint message.
// read constraint values from hardware and provide them back.
// hardcoded here for a 16A single-phase EV charger. real impl: read from modbus etc.
static void on_request_param(s2_pebc_param_t param, s2_commodity_quantity_t quantity, bool is_abnormal) {
    switch (param) {
        // power constraint range: max/min = highest/lowest value CEM may instruct for each limit.
        // set is_abnormal = true when this call is for an abnormal_condition_only constraint slot.
        // return different limits for that slot so the CEM knows the extra range available in emergencies (abnormal conditions).
        case S2_PEBC_PARAM_POWER_UPPER_LIMIT_MAX:
            s2_client_pebc_provide_power_upper_limit_max(s2_handle, quantity, is_abnormal ? 4000.0f : 3680.0f);
            break;
        case S2_PEBC_PARAM_POWER_UPPER_LIMIT_MIN:
            s2_client_pebc_provide_power_upper_limit_min(s2_handle, quantity, 0.0f);
            break;
        case S2_PEBC_PARAM_POWER_LOWER_LIMIT_MAX:
            s2_client_pebc_provide_power_lower_limit_max(s2_handle, quantity, 0.0f);    // cant export
            break;
        case S2_PEBC_PARAM_POWER_LOWER_LIMIT_MIN:
            s2_client_pebc_provide_power_lower_limit_min(s2_handle, quantity, 0.0f);
            break;
        // energy constraint range: average power bounds over the energy window (for smart scheduling)
        case S2_PEBC_PARAM_ENERGY_UPPER_LIMIT_MAX:
            s2_client_pebc_provide_energy_upper_limit_max(s2_handle, quantity, 3680.0f);
            // Optional: override the default valid_until for this energy constraint (default: now + 24h).
            // Call this in the same on_request_param as the energy limit provides.
            time_t deadline = time(NULL) + 8 * 3600; // valid for 8 hours from now
            s2_client_pebc_provide_energy_valid_until(s2_handle, quantity, deadline);
            break;
        case S2_PEBC_PARAM_ENERGY_UPPER_LIMIT_MIN:
            s2_client_pebc_provide_energy_upper_limit_min(s2_handle, quantity, 0.0f);
            break;
        case S2_PEBC_PARAM_ENERGY_LOWER_LIMIT_MAX:
            s2_client_pebc_provide_energy_lower_limit_max(s2_handle, quantity, 0.0f);
            break;
        case S2_PEBC_PARAM_ENERGY_LOWER_LIMIT_MIN:
            s2_client_pebc_provide_energy_lower_limit_min(s2_handle, quantity, 0.0f);
            break;
    }
}

static void on_session_ready(void) {
    ESP_LOGI(TAG, "PEBC session ready - timers started, on_request_param will be called periodically for measurements, constraints and forecasts");

#if TEST_REVOKE_AFTER_CONSTRAINTS
    static bool revoke_test_fired = false;
    if (!revoke_test_fired) {
        revoke_test_fired = true;
        BaseType_t ok = xTaskCreate(revoke_after_constraints_task, "s2_revoke_test", 4096, NULL, 5, NULL);
        if (ok != pdPASS)
            ESP_LOGW(TAG, "TEST: failed to start revoke test task");
    }
#endif
}

void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName) {
    // Write directly to UART, no buffering, no locks, no stack usage
    ets_printf("Stack overflow in task: %s\n", pcTaskName);
    abort();
}

// Run the S2 Connect pairing flow (requestPairing -> WebSocket) using the
// pre-provisioned PAIRING_ACCESS_TOKEN. Shared by RM-initiated and long-poll paths.
static void pair_and_connect(s2_client_handle_t handle) {
    s2_client_request_pairing_response_t req_pairing_res = {0};
    s2_client_connection_details_response_t conn_details_res = {0};

    const char *pairing_token = PAIRING_ACCESS_TOKEN;
    if (pairing_token[0] == '\0') {
        ESP_LOGE(TAG, "No pairing token configured - set PAIRING_ACCESS_TOKEN in .env");
        return;
    }

    // re-fetch the CEM cert: the HMAC is bound to it, and a CEM restart may have changed it
    if (s2_client_fetch_server_certificate(handle) != ESP_OK) {
        ESP_LOGW(TAG, "fetch_server_cert failed, pairing may fail HMAC verification");
    }

    // Generate random 32-byte client challenge
    uint8_t client_challenge[32];
    s2_pairing_generate_challenge(client_challenge);
    char client_challenge_b64[45];
    s2_base64_encode_32(client_challenge, client_challenge_b64);

    // POST requestPairing
    esp_err_t err = s2_client_request_pairing(handle, client_challenge_b64, true, false, &req_pairing_res);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed request to /requestPairing!");
        return;
    }
    ESP_LOGI(TAG, "requestPairing OK: attemptId=%s", req_pairing_res.pairing_attempt_id);

    // Verify clientHmacChallengeResponse (proves the CEM knows the token)
    if (!s2_pairing_verify_response(pairing_token, handle->tls_cert_fingerprint, NULL,
                                    client_challenge, 32,
                                    req_pairing_res.client_hmac_challenge_response)) {
        ESP_LOGE(TAG, "HMAC verify failed!");
        s2_client_finalize_pairing(handle, req_pairing_res.pairing_attempt_id, false);
        return;
    }

    // HMAC verified - pin the cert fingerprint
    s2_client_confirm_certificate_pin(handle);

    // Compute serverHmacChallengeResponse
    uint8_t server_challenge[32];
    s2_base64_decode_32(req_pairing_res.server_hmac_challenge, server_challenge);
    char server_response_b64[45];
    s2_pairing_compute_response(pairing_token, handle->tls_cert_fingerprint, NULL,
                                 server_challenge, 32,
                                 server_response_b64, sizeof(server_response_b64));

    // POST requestConnectionDetails
    err = s2_client_request_connection_details(handle, req_pairing_res.pairing_attempt_id, server_response_b64, &conn_details_res);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed request to /requestConnectionDetails!");
        return;
    }
    ESP_LOGI(TAG, "requestConnectionDetails OK: url=%s", conn_details_res.initiate_session_url);

    // POST finalizePairing
    err = s2_client_finalize_pairing(handle, req_pairing_res.pairing_attempt_id, true);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed request to /finalizePairing!");
        return;
    }

    // POST connection/initiateSession
    s2_client_initiate_session_response_t init_resp = {0};
    err = s2_client_initiate_session(handle, req_pairing_res.server_node.id, conn_details_res.access_token, &init_resp);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "initiateSession failed, using access token directly");
    }

    // POST connection/confirmAccessToken - gets websocketToken (commToken)
    s2_client_confirm_access_token_response_t confirm_resp = {0};
    const char *confirm_token = init_resp.access_token[0] ? init_resp.access_token : conn_details_res.access_token;
    err = s2_client_confirm_access_token_extended(handle, confirm_token, &confirm_resp);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "confirmAccessToken failed");
    }

    // Connect WebSocket
    const char *final_websocket_token = confirm_resp.websocket_token[0] ? confirm_resp.websocket_token : confirm_token;
    const char *final_websocket_url  = confirm_resp.websocket_url[0]  ? confirm_resp.websocket_url  : conn_details_res.initiate_session_url;
    ESP_LOGI(TAG, "WS connect: url=%s token=%s", final_websocket_url, final_websocket_token);
    err = s2_client_websocket_connect(handle, final_websocket_url, final_websocket_token, final_websocket_token);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to connect to websocket server");
    }
}

void app_main(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "Erasing NVS flash due to version change or full pages...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }
    ESP_ERROR_CHECK(ret);

    // PSA Crypto must be initialized before any mbedtls operation
    psa_crypto_init();

    // Initialize TCP/IP stack
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Initialize Wi-Fi connection
    wifi_init();

    // Sync wall-clock time so S2 constraint messages have valid timestamps
    sync_time();

    // mDNS init for DNS-SD CEM discovery
    mdns_init();

    // All are mandatory unless stated as optional. Optional fields can be omitted to apply defaults or setting to 0 will also apply defaults for numeric fields.
    s2_client_config_t config = {
        .node = {
            .id = "5535d679-2a74-4c3a-858e-c134b23ade0d",
            .brand = "test",
            .deployment = S2_DEPLOYMENT_LAN,
            .logo_uri = "test",
            .model = "test",
            .role = S2_ROLE_RM,
            .supported_versions = {"0.0.2-beta"},
            .type = "test",
            .user_defined_name = "test"
        },
        .server_config = {
            .base_url = CEM_S2_INTEGRATION_URL,
            .enable_tls = true,
            .skip_cert_cn_check = true,
        },
        .rm_config = {
            .control_type = S2_CONTROL_TYPE_PEBC,
            .name = "Example EV Charger",       // optional
            .manufacturer = "Elaad",            // optional
            .model = "EVC",     // optional
            .serial_number = "123",             // optional
            .firmware_version = "v1.0",         // optional
            .roles = {
                { .commodity = S2_COMMODITY_ELECTRICITY, .role_type = S2_ROLE_TYPE_ENERGY_CONSUMER }
            },
            .roles_count = 1,
            .provides_forecast = true,
            .instruction_processing_delay_ms = 0,   // optional, 0 = default (500ms)
            // Unique commodity quantities this device measures. Used for provides_power_measurement_types in RMD.
            // List each distinct quantity once (unlike default_limits which may have duplicate quantities for abnormal slots).
            .commodity_quantities = {
                S2_COMMODITY_QUANTITY_ELECTRIC_POWER_L1,
                S2_COMMODITY_QUANTITY_ELECTRIC_POWER_L2,
                S2_COMMODITY_QUANTITY_ELECTRIC_POWER_L3,
            },
            .commodity_quantities_count = 3,
            .measurement_interval_ms = 100000,           // optional, 0 = default (60s)
            .forecast_interval_ms = 300000,          // optional, 0 = default (15 min) - only used if provides_forecast = true
            .on_request_measurement = on_request_measurement,
            .on_request_forecast = on_request_forecast,
            .on_session_ready = on_session_ready,
            .pebc = {
                .constraint_interval_ms = 300000,        // optional, 0 = default (5 min)
                .energy_constraint_window_ms = 0,       // optional, 0 = default (now + 24h)
                .power_constraint_window_ms = 0,        // optional, 0 = omit valid_until (stays valid until replaced/revoked)
                .power_constraint_defer = false,        // optional, false = VANISH (safe default), true = DEFER
                .default_limits = {
                    {
                        .commodity_quantity = S2_COMMODITY_QUANTITY_ELECTRIC_POWER_L1,
                        .power_upper_limit = 3680.0f,   // 16A @ 230V single phase. 16x230 = 3680
                        .power_lower_limit = 0.0f,
                        .energy_upper_limit = 0.0f,
                        .energy_lower_limit = 0.0f,
                        .upper_limit_abnormal_only = false,
                        .lower_limit_abnormal_only = false,
                    },
                    // 3-phase example: add one entry per phase, on_request_param is called once per entry.
                    {
                       .commodity_quantity = S2_COMMODITY_QUANTITY_ELECTRIC_POWER_L2,
                       .power_upper_limit = 3680.0f,
                       .power_lower_limit = 0.0f,
                       .energy_upper_limit = 0.0f,
                       .energy_lower_limit = 0.0f,
                       .upper_limit_abnormal_only = false,
                       .lower_limit_abnormal_only = false,
                    },
                    {
                       .commodity_quantity = S2_COMMODITY_QUANTITY_ELECTRIC_POWER_L3,
                       .power_upper_limit = 3680.0f,
                       .power_lower_limit = 0.0f,
                       .energy_upper_limit = 0.0f,
                       .energy_lower_limit = 0.0f,
                       .upper_limit_abnormal_only = false,
                       .lower_limit_abnormal_only = false,
                    },
                    // If you want to give abnormal limits (only applied when the CEM sends an instruction with
                    // abnormal_condition = true), add a separate entry for the same quantity with the abnormal flags set:
                    {
                        .commodity_quantity = S2_COMMODITY_QUANTITY_ELECTRIC_POWER_L1,
                        .power_upper_limit = 4000.0f,
                        .power_lower_limit = 0.0f,
                        .energy_upper_limit = 0.0f,
                        .energy_lower_limit = 0.0f,
                        .upper_limit_abnormal_only = true,
                        .lower_limit_abnormal_only = true,
                    },
                    // Symmetric 3-phase (single value for all phases):
                    // {
                    //     .commodity_quantity = S2_COMMODITY_QUANTITY_ELECTRIC_POWER_3_PHASE_SYMMETRIC,
                    //     .power_upper_limit = 3680.0f,
                    //     .power_lower_limit = 0.0f,
                    //     .energy_upper_limit = 0.0f,
                    //     .energy_lower_limit = 0.0f,
                    //     .upper_limit_abnormal_only = true,
                    //     .lower_limit_abnormal_only = true,
                    // },
                },
                .default_limits_count = 4, // update this count to match the number of limits given above.
                .on_instruction = on_instruction,
                .on_request_param = on_request_param,
            },
        }
    };
    s2_handle = s2_client_init(&config);
    s2_client_handle_t s2_client_handle = s2_handle;

    if (s2_client_handle == NULL) {
        ESP_LOGE(TAG, "Initialization failed!");
        return;
    }
    ESP_LOGI(TAG, "Initialization success!");

    original_vprintf = esp_log_set_vprintf(forwarding_vprintf);

    // try DNS-SD to discover CEMs; override base_url with the first one found
    ESP_LOGI(TAG, "Attempting DNS-SD discovery...");
    char *cem_urls[MAX_CEMS] = {0};
    int cem_count = s2_dns_sd_find_cems(5000, cem_urls, MAX_CEMS);
    if (cem_count > 0) {
        for (int i = 0; i < cem_count; i++) {
            ESP_LOGI(TAG, "DNS-SD: found CEM %d/%d: %s", i + 1, cem_count, cem_urls[i]);
        }
        // No UI in this example, so pick the first CEM. A device with a UI
        // should present the full list and let the user choose.
        char *dns_url = cem_urls[0];
        // strip trailing /pairing or /pairing/ from DNS-SD pairingUrl
        size_t len = strlen(dns_url);
        if (len > 8 && strcmp(dns_url + len - 8, "/pairing") == 0)
            dns_url[len - 8] = '\0';
        else if (len > 9 && strcmp(dns_url + len - 9, "/pairing/") == 0)
            dns_url[len - 9] = '\0';
        strlcpy(s2_handle->server.base_url, dns_url, sizeof(s2_handle->server.base_url));
        // also update the Host header value extracted from the new base_url
        s2_client_http_extract_host(s2_handle->server.base_url,
                                     s2_handle->server.host,
                                     sizeof(s2_handle->server.host));
        ESP_LOGI(TAG, "DNS-SD: using discovered CEM at %s", s2_handle->server.base_url);
        for (int i = 0; i < cem_count; i++) {
            free(cem_urls[i]);
        }
    } else {
        ESP_LOGW(TAG, "DNS-SD failed, falling back to base_url=%s", s2_handle->server.base_url);
    }

    esp_err_t err;

    // version negotiation: GET /pairing/ to verify v1 is supported
    err = s2_client_verify_pairing_version(s2_handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "version negotiation failed, CEM may not support v1");
    }

    // fetch TLS cert fingerprint at runtime (TOFU)
    err = s2_client_fetch_server_certificate(s2_client_handle);
    ESP_LOGD(TAG, "fetch_server_cert: 0x%x", err);

#if RESPONDER_NODE_DEMO
    // Responder-node demo: the RM is an HTTPS client only (no HTTP server), so
    // only long polling is possible. Poll /waitForPairing and react to CEM actions.
    ESP_LOGI(TAG, "RESPONDER_NODE_DEMO: entering waitForPairing loop");
    {
        bool include_descriptions = false;
        bool no_valid_token = false;
        for (;;) {
            s2_long_poll_request_options_t opts = {
                .include_descriptions = include_descriptions,
                .no_valid_token = no_valid_token,
            };
            include_descriptions = false;
            no_valid_token = false;

            s2_long_poll_result_t result;
            esp_err_t lp_err = s2_long_poll_wait_extended(s2_client_handle,
                                                     s2_handle->node.id,
                                                     &opts, &result);
            if (lp_err != ESP_OK) {
                ESP_LOGW(TAG, "waitForPairing returned 0x%x", (unsigned int)lp_err);
                vTaskDelay(pdMS_TO_TICKS(1000));
                continue;
            }

            switch (result.action) {
                case S2_LP_SEND_NODE_DESC:
                    ESP_LOGI(TAG, "LP: sendNodeDescription -> include descriptions next poll");
                    include_descriptions = true;
                    break;
                case S2_LP_PREPARE_PAIRING:
                    ESP_LOGI(TAG, "LP: preparePairing -> show pairing code");
                    break;
                case S2_LP_CANCEL_PREPARE:
                    ESP_LOGI(TAG, "LP: cancelPreparePairing -> hide pairing code");
                    break;
                case S2_LP_REQUEST_PAIRING:
                    if (PAIRING_ACCESS_TOKEN[0] == '\0') {
                        no_valid_token = true;  // no token: report it next poll
                        break;
                    }
                    ESP_LOGI(TAG, "LP: requestPairing -> initiate pairing");
                    pair_and_connect(s2_client_handle);
                    // keep polling: the CEM may need to pair this node again, and a failed
                    // pairing above has to be retryable without a reboot
                    break;
                case S2_LP_NO_ACTION:
                case S2_LP_ERROR:
                default:
                    break;
            }
        }
    }
#endif

#if INITIATOR_NODE_DEMO
    /* ---- S2 Connect pairing flow (token from PAIRING_ACCESS_TOKEN) ---- */
    pair_and_connect(s2_client_handle);

    // log forwarding test: enqueue a log for the forwarder to batch and send
    time_t now = time(NULL);
    char msg[128];
    int n = snprintf(msg, sizeof(msg), "example log from device @%lld", (long long)now);
    if (n > 0) {
        log_forwarder_enqueue("INFO", "RM", msg);
        ESP_LOGI(TAG, "Enqueued test log for forwarder");
    }

    // UNHAPPY PATH: device goes offline / unplugged
    // If the device disconnects before the CEM has sent any instruction,
    // call revoke_constraints so the CEM knows the previously sent PowerConstraints
    // and EnergyConstraints are no longer valid and stops using them for scheduling.
    // Sends a RevokeObject for every constraint object that was last successfully transmitted:
    // err = s2_client_pebc_revoke_constraints(s2_client_handle);
    // if (err == ESP_OK)
    //     ESP_LOGI(TAG, "Constraints revoked - CEM will discard pending envelopes");

    // UNHAPPY PATH: hardware fault while an instruction is active
    // If the device encounters a fault that prevents it from following the active instruction, call abort_instruction.
    // Sends an ABORTED InstructionStatusUpdate to the CEM, resets all commodity quantities
    // back to their config default_limits, and cancels any running instruction element timer.
    // Returns ESP_ERR_INVALID_STATE when no instruction is currently active:
    // err = s2_client_pebc_abort_instruction(s2_client_handle);
    // if (err == ESP_OK)
    //     ESP_LOGI(TAG, "Instruction aborted - device reset to default limits");


    // Send unpair request
    // err = s2_client_unpair(s2_client_handle, init_connection_res->token_pair.access_token);
    // if (err == ESP_OK) {
    //     ESP_LOGI(TAG, "Successful request to /unpair!");
    // }
    // else {
    //     ESP_LOGI(TAG, "Failed request to /unpair!");
    //     goto cleanup;
    // }

    return;
#endif
}