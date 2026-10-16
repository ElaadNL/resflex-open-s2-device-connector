// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <esp_http_client.h>
#include <stdbool.h>
#include <time.h>
#include "s2_types.h"
#include "esp_err.h"
#include <net/if.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char base_url[128];                                 /*!< Base URL of the S2 server node */
    bool enable_tls;                                    /*!< Indicates if TLS (HTTPS / WSS) should be used */
    const char *cert_pem;                               /*!< PEM encoded certificate(s) for TLS validation. NULL if not using TLS */
    size_t cert_pem_len;                                /*!< Length of the cert_pem buffer (length of certificate + null terminator) */
    bool skip_cert_cn_check;                            /*!< Skip TLS certificate common name check. Set true for LAN self-signed certs accessed by IP */
    char interface[IF_NAMESIZE];                        /*!< Name of the interface for data to go through */
} s2_client_server_config_t;

/** Maximum number of access tokens the RM can store for reconnection attempts. */
#define S2_MAX_ACCESS_TOKENS 5

/** Length of a single access token string (32 chars + null). */
#define S2_ACCESS_TOKEN_LEN 33

/** Size of a SHA256 TLS certificate fingerprint in bytes. */
#define S2_TLS_FINGERPRINT_SIZE 32

/**
 * @brief NVS keys for persisting S2 Connect configuration and state.
 *
 * These keys are used with the ESP-IDF NVS (Non-Volatile Storage) library
 * to persist pairing/connection state across reboots.
 */
#define S2_NVS_NAMESPACE           "s2_client"        /*!< NVS namespace for S2 client data */
#define S2_NVS_KEY_ACCESS_TOKENS   "access_tokens"    /*!< JSON array of access tokens */
#define S2_NVS_KEY_TLS_FINGERPRINT "tls_fingerprint"  /*!< SHA256 TLS cert fingerprint (32 bytes) */
#define S2_NVS_KEY_DEPLOYMENT      "deployment"       /*!< Deployment type: "LAN" or "WAN" */
#define S2_NVS_KEY_DNS_SD          "dns_sd"           /*!< DNS-SD enabled flag (uint8) */

/** S2 Connect pairing API major version supported by this RM */
#define S2_CONNECT_PAIRING_API_VERSION "v1"

/** Maximum number of different commodity quantities. There are 10 different quantity types in S2 spec. 
 *  A device will probably not use more than 3/4 at the same time but 10 is a safe upper since we know it
 *  cannot be more
*/
#define S2_MAX_COMMODITY_QUANTITIES 10

/** Default polling intervals when not set in config */
#define S2_DEFAULT_MEASUREMENT_INTERVAL_MS   60000  /* 1 minute */
#define S2_DEFAULT_CONSTRAINT_INTERVAL_MS   300000  /* 5 minutes */
#define S2_DEFAULT_FORECAST_INTERVAL_MS     900000  /* 15 minutes */

/** Default instruction processing delay when not set in config. 
 *  average time the device needs to process and execute an instruction.
*/
#define S2_DEFAULT_INSTRUCTION_PROCESSING_DELAY_MS 500

/** Maximum number of roles (commodity + role type pairs) a device can declare 
 *  Theoretical max is 3 because electric devices can have 3 phases.
 *  S2 spec does not state a max but s2_python library uses 3 as a max so we can follow that.
 *  See: https://github.com/flexiblepower/s2-python/blob/9ca1409d3a9773b955fe1d1f3c6d5b1b59a71213/src/s2python/generated/gen_s2.py#L1165
*/
#define S2_RM_MAX_ROLES 3

/**
 * @brief Default power and energy limits for a single commodity quantity
 *
 * Used as a fallback when no PEBC.Instruction is active or when an error has occurred.
 * At least one entry is required in s2_pebc_config_t.
 *
 * Units per field depend on the selected commodity_quantity.
 * 
 * IMPORTANT: See s2_commodity_quantity_t in s2_types.h for the unit of each commodity. 
 *            Make sure you set the limits according to the correct unit.
 */
typedef struct {
    s2_commodity_quantity_t commodity_quantity; /*!< Which commodity quantity these limits apply to */
    float power_upper_limit;                    /*!< Default upper limit */
    float power_lower_limit;                    /*!< Default lower limit */
    float energy_upper_limit;                   /*!< Default energy upper limit */
    float energy_lower_limit;                   /*!< Default energy lower limit */
    bool upper_limit_abnormal_only;             /*!< Set true if upper power limit range is for abnormal conditions only */
    bool lower_limit_abnormal_only;             /*!< Set true if lower power limit range is for abnormal conditions only */
} s2_pebc_default_limits_t;

/**
 * @brief Identifies which PEBC constraint value the library is requesting via on_request_param.
 * 
 * See below for more information on each: 
 *  - Power Limits: https://docs.s2standard.org/model-reference/PEBC/PEBC.PowerConstraints/
 *  - Energy Limits: https://docs.s2standard.org/model-reference/PEBC/PEBC.EnergyConstraint/
 */
typedef enum {
    S2_PEBC_PARAM_POWER_UPPER_LIMIT_MAX,  /*!< Highest value CEM may instruct for the upper power limit */
    S2_PEBC_PARAM_POWER_UPPER_LIMIT_MIN,  /*!< Lowest value  CEM may instruct for the upper power limit */
    S2_PEBC_PARAM_POWER_LOWER_LIMIT_MAX,  /*!< Highest value CEM may instruct for the lower power limit */
    S2_PEBC_PARAM_POWER_LOWER_LIMIT_MIN,  /*!< Lowest value CEM may instruct for the lower power limit */
    S2_PEBC_PARAM_ENERGY_UPPER_LIMIT_MAX, /*!< Highest upper average power device can handle */
    S2_PEBC_PARAM_ENERGY_UPPER_LIMIT_MIN, /*!< Lowest upper average power device can handle */
    S2_PEBC_PARAM_ENERGY_LOWER_LIMIT_MAX, /*!< Highest lower average power device can handle */
    S2_PEBC_PARAM_ENERGY_LOWER_LIMIT_MIN, /*!< Lowest lower average power device can handle */
} s2_pebc_param_t;

/**
 * @brief Library calls this when it needs a PEBC constraint value from firmware.
 *
 * Firmware should read the relevant value (e.g via Modbus) and then call the corresponding
 * s2_client_pebc_provide_*() function to hand it back to the library.
 *
 * @param param        Which constraint value is being requested.
 * @param quantity     Which commodity quantity it applies to.
 * @param is_abnormal  True if this request is for a limit slot marked with upper_limit_abnormal_only
 *                     or lower_limit_abnormal_only in config. Use this to return different (e.g. higher)
 *                     limits for the abnormal slot vs the normal slot of the same commodity quantity.
 */
typedef void (*s2_pebc_on_request_param_t)(s2_pebc_param_t param, s2_commodity_quantity_t quantity, bool is_abnormal);

/**
 * @brief Callback invoked when a PEBC instruction becomes active or expires.
 *
 * Called in two situations:
 *   1. A new instruction becomes active (STARTED): called with the CEM's instruction limits.
 *   2. The active instruction expires or is replaced or revoked: called with the default limits from config
 *      so the device can go back to running with default limits in place.
 *
 * If a new instruction arrives while one is already active, the active instruction is cancelled.
 * This only applies if their execution times and durations mean that their active periods overlap. 
 * Simply put: new instruction always takes precedence.
 * This callback is invoked once with the new limits.
 * There are no intermediate calls with defaults if there is no time gap between instructions (overlap).
 *
 * The library owns the instruction timer. Duration is not passed to the callback.
 *
 * @param commodity_quantity  The commodity quantity the limits apply to.
 * @param upper_limit         Power upper limit in the unit of the commodity quantity (see s2_commodity_quantity_t).
 * @param lower_limit         Power lower limit in the unit of the commodity quantity.
 */
typedef void (*s2_pebc_on_instruction_t)(s2_commodity_quantity_t commodity_quantity, float upper_limit, float lower_limit);

/**
 * @brief PEBC resource manager configuration. Contains only PEBC-specific fields.
 *
 * Set as the pebc member of s2_rm_config_t when control_type is S2_CONTROL_TYPE_PEBC.
 */
typedef struct {
    /** Default limits per commodity quantity, used when no instruction is active. At least 1 required. */
    s2_pebc_default_limits_t default_limits[S2_MAX_COMMODITY_QUANTITIES];
    uint8_t default_limits_count; /*!< Number of valid entries in default_limits (at least 1) */

    /** Polling interval for sending constraints. Set 0 to use S2_DEFAULT_CONSTRAINT_INTERVAL_MS (5 min). */
    uint32_t constraint_interval_ms;
    /** Window duration for energy constraints (valid_until = now + window). Set 0 to use default (24h). */
    uint32_t energy_constraint_window_ms;
    /** Window duration for power constraints (valid_until = now + window). Set 0 to omit valid_until (stays valid until replaced/revoked). */
    uint32_t power_constraint_window_ms;
    /** consequence_type for PEBC.PowerConstraints. false = VANISH (default), true = DEFER (smarter energy management). */
    bool power_constraint_defer;

    /**
     * Called when a PEBC instruction becomes active or the active instruction ends. Mandatory.
     * Library does not accept a config without this set.
     * We do this to ensure the device either has a (valid) instruction or is running on default settings at all times.
     */
    s2_pebc_on_instruction_t on_instruction;

    /** Library calls this to request a constraint value from firmware. Mandatory.
     *  Firmware will then read the value (e.g. via Modbus) and call the corresponding
     *  s2_client_pebc_provide_*() function to hand the value back.
     */
    s2_pebc_on_request_param_t on_request_param;
} s2_pebc_config_t;

/**
 * @brief Called by library when it needs a power measurement from firmware.
 *
 * Firmware should read the value and call s2_client_provide_measurement().
 *
 * @param quantity  Which commodity quantity to measure.
 */
typedef void (*s2_on_request_measurement_t)(s2_commodity_quantity_t quantity);

/**
 * @brief Called by library when it needs a power forecast from firmware.
 *
 * Only called if provides_forecast = true. Firmware should call s2_client_provide_forecast().
 *
 * @param quantity  Which commodity quantity to forecast.
 */
typedef void (*s2_on_request_forecast_t)(s2_commodity_quantity_t quantity);

/**
 * @brief S2 control type. Determines which union member is active in s2_rm_config_t.
 */
typedef enum {
    S2_CONTROL_TYPE_PEBC, /*!< Power Envelope Based Control */
    S2_CONTROL_TYPE_PPBC, /*!< Power Profile Based Control */
    S2_CONTROL_TYPE_OMBC, /*!< Operation Mode Based Control */
    S2_CONTROL_TYPE_FRBC, /*!< Fill Rate Based Control */
    S2_CONTROL_TYPE_DDBC, /*!< Demand Driven Based Control */
} s2_control_type_t;

/**
 * @brief Resource manager configuration.
 *
 * Contains generic RM identity, roles, intervals, and callbacks, plus a control-type-specific
 * configuration union. Set control_type to determine which union member is active.
 *
 * Mandatory/optional fields correspond to the S2 spec ResourceManagerDetails message.
 * See: https://docs.s2standard.org/model-reference/Common/ResourceManagerDetails/
 */
typedef struct {
    s2_control_type_t control_type; /*!< Which control type this RM uses. Determines active union member. */

    /** Device identity fields. Sent in ResourceManagerDetails. All optional. */
    char name[64];
    char manufacturer[64];
    char model[64];
    char serial_number[64];
    char firmware_version[32];

    /** Roles describing what commodities this device consumes, produces or stores. Mandatory, at least 1 role. */
    s2_role_t roles[S2_RM_MAX_ROLES];
    uint8_t roles_count; /*!< Number of valid entries in roles (at least 1) */

    /** Whether this device can provide PowerForecasts. Set true to enable on_request_forecast calls. */
    bool provides_forecast;

    /** Average time in ms the device needs to process an instruction.
     *  Set 0 to use S2_DEFAULT_INSTRUCTION_PROCESSING_DELAY_MS. */
    uint32_t instruction_processing_delay_ms;

    /** Commodity quantities this RM can measure. Populates provides_power_measurement_types in RMD.
     *  List unique quantities only (unlike default_limits which may duplicate for abnormal slots). */
    s2_commodity_quantity_t commodity_quantities[S2_MAX_COMMODITY_QUANTITIES];
    uint8_t commodity_quantities_count; /*!< Number of valid entries (at least 1) */

    /** Polling interval for reading power measurements. Set 0 to use S2_DEFAULT_MEASUREMENT_INTERVAL_MS (1 min). */
    uint32_t measurement_interval_ms;
    /** Polling interval for sending power forecasts. Set 0 to use S2_DEFAULT_FORECAST_INTERVAL_MS (15 min).
     *  Only used when provides_forecast = true. */
    uint32_t forecast_interval_ms;

    /** Called when library needs a power measurement. Mandatory. */
    s2_on_request_measurement_t on_request_measurement;
    /** Called when library needs a forecast. Only called when provides_forecast = true. */
    s2_on_request_forecast_t on_request_forecast;
    /**
     * Called once when the S2 session is fully established (SelectControlType received from CEM).
     * After this, the library will start requesting measurements and sending constraints/forecasts to the CEM.
     * Mandatory.
     */
    void (*on_session_ready)(void);

    /** Control-type-specific configuration. Set the member matching control_type. */
    union {
        s2_pebc_config_t pebc; /*!< PEBC configuration. Active when control_type == S2_CONTROL_TYPE_PEBC. */
    };
} s2_rm_config_t;

/**
 * @brief S2 client configuration
 */
typedef struct {
    s2_node_info_t node;                     /*!< Client node information */
    s2_client_server_config_t server_config; /*!< S2 server configuration */
    s2_rm_config_t rm_config;                /*!< Resource manager configuration */

    /** ---- S2 Connect fields ---- */

    /** SHA256 fingerprint of the CEM's TLS leaf certificate (32 bytes).
     *  Set for LAN deployments with self-signed certs. All-zero = not set. */
    uint8_t tls_cert_fingerprint[S2_TLS_FINGERPRINT_SIZE];

    /** Enable DNS-SD discovery to find the CEM on the LAN. */
    bool dns_sd_enabled;

    /** Access token list for reconnection. Tokens[0] is the newest.
     *  Persist to NVS to survive reboots. */
    char access_tokens[S2_MAX_ACCESS_TOKENS][S2_ACCESS_TOKEN_LEN];
    uint8_t access_token_count;
} s2_client_config_t;

/** S2 client handle available after initialization */
typedef struct s2_client *s2_client_handle_t;

/** Firmware calls this to hand a measurement value back after an on_request_measurement call. */
esp_err_t s2_client_provide_measurement(s2_client_handle_t handle, s2_commodity_quantity_t quantity, float value);

/**
 * Firmware calls these to hand constraint values back after an on_request_param call.
 * Returns ESP_ERR_INVALID_STATE if called without a pending request.
 */
esp_err_t s2_client_pebc_provide_power_upper_limit_max(s2_client_handle_t handle, s2_commodity_quantity_t quantity, float value);
esp_err_t s2_client_pebc_provide_power_upper_limit_min(s2_client_handle_t handle, s2_commodity_quantity_t quantity, float value);
esp_err_t s2_client_pebc_provide_power_lower_limit_max(s2_client_handle_t handle, s2_commodity_quantity_t quantity, float value);
esp_err_t s2_client_pebc_provide_power_lower_limit_min(s2_client_handle_t handle, s2_commodity_quantity_t quantity, float value);
esp_err_t s2_client_pebc_provide_energy_upper_limit_max(s2_client_handle_t handle, s2_commodity_quantity_t quantity, float value);
esp_err_t s2_client_pebc_provide_energy_upper_limit_min(s2_client_handle_t handle, s2_commodity_quantity_t quantity, float value);
esp_err_t s2_client_pebc_provide_energy_lower_limit_max(s2_client_handle_t handle, s2_commodity_quantity_t quantity, float value);
esp_err_t s2_client_pebc_provide_energy_lower_limit_min(s2_client_handle_t handle, s2_commodity_quantity_t quantity, float value);
/** Override the energy constraint valid_until for a quantity. deadline is a unix timestamp (time_t).
 *  If not called, the default is now + 24h. */
esp_err_t s2_client_pebc_provide_energy_valid_until(s2_client_handle_t handle, s2_commodity_quantity_t quantity, time_t deadline);
/** Hand a forecast value back after an on_request_forecast call. */
esp_err_t s2_client_provide_forecast(s2_client_handle_t handle, s2_commodity_quantity_t quantity, float value);

/** Call this if firmware encounters an issue mid-instruction (hardware fault for example). Sends ABORTED InstructionStatusUpdate to CEM, resets device to default limits, cancels timers.
 *  Returns ESP_ERR_INVALID_STATE if no active instruction. */
esp_err_t s2_client_pebc_abort_instruction(s2_client_handle_t handle);

/** Revoke all previously sent PowerConstraints and EnergyConstraints for all quantity slots.
 *  Call this when the device goes offline, disconnects, or can no longer honour any constraints
 *  (unplugged, hardware fault before CEM has issued an instruction).
 *  Sends a RevokeObject for every object that was last successfully sent.
 *  Clears stored object IDs so the constraints are not revoked again automatically. */
esp_err_t s2_client_pebc_revoke_constraints(s2_client_handle_t handle);

/**
 * @brief   Initializes the S2 client.
 *          This function should be called before other functions as it returns a handle
 *          that is used as input for other functions of the interface
 * 
 * @param   config S2 client configuration, see `s2_client_config_t`
 * 
 * @return
 *      - `s2_client_handle_t`
 * 
 *      - NULL if any errors
 */
s2_client_handle_t s2_client_init(const s2_client_config_t *config);

/**
 * @brief   Compute and store the SHA256 fingerprint of the server's TLS leaf certificate.
 *
 * Call during LAN pairing after the first HTTPS request succeeds (version check).
 * The fingerprint is used in the HMAC challenge-response and pinned for all future
 * connections to this server.
 *
 * @param   client_handle   S2 client handle
 * @param   der_cert        Pointer to DER-encoded leaf certificate
 * @param   cert_len        Length of certificate in bytes
 */
void s2_client_pin_server_certificate(s2_client_handle_t client_handle,
                               const uint8_t *der_cert, size_t cert_len);

/**
 * @brief   Add an access token to the persisted list (newest first).
 *
 * Tokens survive reboots via NVS. Used after pairing succeeds to store the
 * new access token. On reconnect the RM tries tokens[0] first, falling back
 * to older tokens on 401.
 *
 * @param   client_handle   S2 client handle
 * @param   token           Access token string (null-terminated)
 */
void s2_client_add_access_token(s2_client_handle_t client_handle,
                                const char *token);

/**
 * @brief initiateSession response (POST /connection/v1/initiateSession).
 */ 
typedef struct {
    char access_token[128];
    char selected_s2_version[32];
} s2_client_initiate_session_response_t;

/**
 * @brief confirmAccessToken response (POST /connection/v1/confirmAccessToken).
 */
typedef struct {
    char websocket_token[128];
    char websocket_url[256];
} s2_client_confirm_access_token_response_t;

/**
 * @brief   POST /connection/v1/initiateSession - get a new pending access token.
 */
esp_err_t s2_client_initiate_session(s2_client_handle_t client_handle,
                                      const char *server_node_id,
                                      const char *access_token,
                                      s2_client_initiate_session_response_t *response);

/**
 * @brief   POST /connection/v1/confirmAccessToken - activate pending token, get WebSocket commToken.
 */
esp_err_t s2_client_confirm_access_token_extended(s2_client_handle_t client_handle,
                                              const char *access_token,
                                              s2_client_confirm_access_token_response_t *response);

/**
 * @brief   Confirm the access token with the CEM session init.
 *
 * POST /connection/v1/confirmAccessToken with the token as Bearer.
 * On 200: server activated the token, old tokens are pruned from NVS.
 * On failure: both old and new tokens are kept (try sequentially).
 *
 * @param   client_handle   S2 client handle
 * @param   access_token    The access token to confirm
 * @return  ESP_OK if confirmed, ESP_FAIL otherwise
 */
esp_err_t s2_client_confirm_access_token(s2_client_handle_t client_handle,
                                          const char *access_token);

/**
 * @brief   Sends a POST request to the /preparePairing endpoint of the REST API running on
 *          the S2 server node. It is up to the server to decide what to do with this signal
 * 
 * @param   client_handle S2 client handle obtained from initialization
 * 
 * @return
 *      - ESP_OK on success
 * 
 *      - ESP_ERR_INVALID_ARG on invalid function arguments
 * 
 *      - ESP_FAIL on failure
 */
esp_err_t s2_client_prepare_pairing(s2_client_handle_t client_handle,
                                     const char *server_node_id);

/**
 * @brief   Sends a POST request to the /cancelPreparePairing endpoint of the REST API running on
 *          the S2 server node. Can be used to cancel a preparePairing request It is up to the
 *          server to decide what to do with this signal
 * 
 * @param   client_handle S2 client handle obtained from initialization
 * 
 * @return
 *      - ESP_OK on success
 * 
 *      - ESP_ERR_INVALID_ARG on invalid function arguments
 * 
 *      - ESP_FAIL on failure
 */
esp_err_t s2_client_cancel_prepare_pairing(s2_client_handle_t client_handle,
                                            const char *server_node_id);

/**
 * @brief Confirm the TLS certificate fingerprint after successful HMAC verification.
 *        Enables cert pinning for all subsequent TLS connections.
 *
 * @param   client_handle S2 client handle
 */
void s2_client_confirm_certificate_pin(s2_client_handle_t client_handle);

/**
 * @brief requestPairing response (POST /pairing/v1/requestPairing).
 */
typedef struct {
    char pairing_attempt_id[48];         /*!< Pairing attempt identifier from server */
    s2_node_info_t server_node;          /*!< S2 server node description */
    char client_hmac_challenge_response[45];  /*!< Base64 HMAC response (server proves it knows the token) */
    char server_hmac_challenge[45];      /*!< Base64 server challenge (RM must HMAC with token and return) */
} s2_client_request_pairing_response_t;

/**
 * @brief   Sends a POST request to the /requestPairing endpoint
 *
 * Sends clientNodeDescription + clientHmacChallenge. The token is never sent directly.
 *
 * @param   client_handle   S2 client handle obtained from initialization
 * @param   client_challenge_b64  Base64-encoded 32-byte random client challenge
 * @param   is_lan           true for LAN deployment
 * @param   force_pairing    true to force pairing even when S2 message versions don't overlap
 * @param   response         Response of the request will be stored in this field
 * 
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_ARG on invalid function arguments
 *      - ESP_FAIL on failure
 */
esp_err_t s2_client_request_pairing(s2_client_handle_t client_handle,
                                     const char *client_challenge_b64,
                                     bool is_lan,
                                     bool force_pairing,
                                     s2_client_request_pairing_response_t *response);

/**
 * @brief requestConnectionDetails response (POST /pairing/v1/requestConnectionDetails).
 */
typedef struct {
    char initiate_session_url[256];      /*!< WebSocket URL for session initiation */
    char access_token[128];              /*!< Access token for WebSocket auth (Bearer) */
} s2_client_connection_details_response_t;

/**
 * @brief   Sends a POST request to the /requestConnectionDetails endpoint
 *
 * Sends serverHmacChallengeResponse (HMAC of server challenge with pairing token).
 * Returns initiateSessionUrl + accessToken for WebSocket connection.
 *
 * @param   client_handle               S2 client handle
 * @param   pairing_attempt_id          pairingAttemptId from requestPairing response
 * @param   server_challenge_response_b64  Base64 HMAC response computed from serverHmacChallenge
 * @param   response                    Response stored here
 * 
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_ARG on invalid function arguments
 *      - ESP_FAIL on failure
 */
esp_err_t s2_client_request_connection_details(s2_client_handle_t client_handle,
                                                const char *pairing_attempt_id,
                                                const char *server_challenge_response_b64,
                                                s2_client_connection_details_response_t *response);

/**
 * @brief   Sends a POST request to the /finalizePairing endpoint
 *
 * Confirms pairing success or failure to the CEM.
 * pairing_attempt_id is sent as Authorization: Bearer header.
 * On HMAC validation failure, call with success=false
 *
 * @param   client_handle       S2 client handle
 * @param   pairing_attempt_id  pairingAttemptId from requestPairing response
 * @param   success             true if pairing succeeded, false on failure
 */
esp_err_t s2_client_finalize_pairing(s2_client_handle_t client_handle,
                                      const char *pairing_attempt_id,
                                      bool success);

/**
 * @brief   Establishes a WebSocket connection to the CEM.
 *
 * Pass the accessToken from requestConnectionDetails as the bearer token.
 * The url should be the initiateSessionUrl from requestConnectionDetails.
 *
 * @param   client_handle       S2 client handle obtained from initialization
 * @param   url                 WebSocket URL (initiateSessionUrl)
 * @param   bearer_token        accessToken. Sent as Authorization: Bearer header.
 * @param   access_token        Retained for potential re-authentication on auth failures.
 * 
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_ARG on invalid function arguments
 *      - ESP_ERR_NO_MEM on memory allocation failure
 *      - ESP_FAIL on failure
 */
esp_err_t s2_client_websocket_connect(s2_client_handle_t client_handle, const char *url, const char *bearer_token, const char *access_token);

/**
 * @brief   Helper function to send a message to the S2 server via the established WebSocket
 *          connection
 * 
 * @param   client_handle   S2 client handle obtained from initialization
 * @param   message         Body that will be included in the message
 * @param   timeout         Timeout in ms to apply for sending a message
 * 
 * @return
 *      - ESP_OK on success
 * 
 *      - ESP_ERR_INVALID_ARG on invalid function arguments
 * 
 *      - ESP_ERR_INVALID_STATE when WebSocket connection is not yet initialized
 * 
 *      - ESP_ERR_TIMEOUT on send timeout
 * 
 *      - ESP_FAIL on failure
 */
esp_err_t s2_client_websocket_send_message(s2_client_handle_t client_handle, const char *message, uint32_t timeout_ms);

/**
 * @brief   Sends a POST request to the /unpair endpoint of the REST API running on the
 *          S2 server node. Can be used to unpair the S2 client from the server
 * 
 * @param   client_handle   S2 client handle obtained from initialization
 * @param   access_token    Access token used to authorize the client
 * @param   server_node_id  Node ID of the server to unpair from
 * 
 * @return
 *      - ESP_OK on success
 * 
 *      - ESP_ERR_INVALID_ARG on invalid function arguments
 * 
 *      - ESP_FAIL on failure
 */
esp_err_t s2_client_unpair(s2_client_handle_t client_handle, const char *access_token,
                            const char *server_node_id);

/**
 * @brief   Destroys the S2 client and releases all its allocated resources.
 * 
 * @param   client_handle S2 client handle obtained from initialization
 */
void s2_client_destroy(s2_client_handle_t client_handle);

/* -- Test-only internal helpers (visible only during unit tests) ----------------- */
#ifdef UNITY_INCLUDE_CONFIG_H
/* Initialize HTTP internals for a client (used by unit tests). Returns true on success. */
bool s2_client_init_http(s2_client_handle_t client, const s2_client_config_t *config);
#endif

#ifdef __cplusplus
}
#endif
