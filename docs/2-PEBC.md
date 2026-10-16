<!--
SPDX-FileCopyrightText: ElaadNL

SPDX-License-Identifier: Apache-2.0
-->

# PEBC

This document contains information regarding the S2 firmware library that implements the Power Envelope Based Control (PEBC) control type according to the [S2 standard](https://docs.s2standard.org/). It covers everything a firmware developer will need to configure the library and wire callbacks so that a fully functional S2 Resource Manager (RM) can be configured to communicate with a Customer Energy Manager (CEM). This page walks through the process of setting up a S2 session, full PEBC flow with constraints, measurements, forecasts, and instruction handling.

## Session lifecycle

After [pairing](https://docs.s2standard.org/docs/communication-layer/discovery-pairing-authentication/) (see the corresponding **1-Pairing** documentation), the device opens a WebSocket connection to the CEM. The CEM sends a [`Handshake`](https://docs.s2standard.org/model-reference/Common/Handshake/) listing its supported S2 versions; the library responds with its own `Handshake`. The CEM then sends a [`HandshakeResponse`](https://docs.s2standard.org/model-reference/Common/HandshakeResponse/) with the agreed version. Once that's done, the library sends [`ResourceManagerDetails`](https://docs.s2standard.org/model-reference/Common/ResourceManagerDetails/) and waits for [`SelectControlType`](https://docs.s2standard.org/model-reference/Common/SelectControlType/). The CEM picks PEBC, the library calls your `on_session_ready` callback, and the periodic timers start.

```txt
[Pairing done] -> WebSocket open
    -> CEM sends Handshake (supported versions)
        -> library sends Handshake back (its supported versions)
            -> CEM sends HandshakeResponse (selected version)
                -> library sends ResourceManagerDetails
                    -> CEM sends SelectControlType (PEBC)
                        -> library calls on_session_ready()
                            -> measurement timer starts
                            -> constraint timer starts (fires immediately too, CEM gets data right away)
```

`on_session_ready` is your signal that the session is up and the library is running. You don't need to kick off anything manually. The first measurement and constraint requests come in right after.

```c
static void on_session_ready(void) {
    // session is live. on_request_measurement and on_request_param will start firing.
    ESP_LOGI(TAG, "S2 session ready");
}
```

If the CEM sends a control type other than PEBC, the library rejects it and the session does not start since, currently, the library only supports PEBC.

If the WebSocket drops, the library reconnects on its own. It calls `initiateSession` and `confirmAccessToken` again for a fresh communication token
(they are single use and expire within 30 seconds), then reconnects and calls `on_session_ready` again. Retries use a capped, jittered backoff and keep going
while the CEM is unreachable. If the CEM refuses the stored access token, the library stops retrying and logs that the device has to pair again.

## Configuration

Everything goes into one `s2_client_config_t` passed to `s2_client_init()`. It has three nested parts: node identity, server connection, and the RM config. The RM config has a PEBC-specific sub-struct.

```c
s2_client_config_t config = {
    .node = { ... },          // who this device is on the S2 network
    .server_config = { ... }, // where the CEM is
    .rm_config = { ... },     // what this device does and how
};
s2_handle = s2_client_init(&config);
```

`s2_client_init` returns `NULL` on failure. Always check it.

### Node

```c
.node = {
    .id = "5535d679-2a74-4c3a-858e-c134b23ade0d",  // UUID, must be unique per device
    .role = S2_ROLE_RM,
    .deployment = S2_DEPLOYMENT_LAN,
    .supported_versions = {"0.0.2-beta"},
    .brand = "Acme",           // optional
    .model = "HW-1000",        // optional
    .user_defined_name = "My Device",  // optional
}
```

The `id` is the node's S2 identity keep it stable across reboots (store in NVS).

### Server config

```c
.server_config = {
    .base_url = "http://192.168.1.10:8123",
    // TLS:
    // .enable_tls = true,
    // .cert_pem = my_cert_pem,
    // .cert_pem_len = sizeof(my_cert_pem),
    // .interface = "eth0",   // bind to a specific network interface, optional
}
```

### RM config

```c
.rm_config = {
    .control_type = S2_CONTROL_TYPE_PEBC,

    // Device metadata sent in ResourceManagerDetails. All optional
    .name = "Example EV Charger",
    .manufacturer = "Elaad",
    .model = "EVC",
    .serial_number = "123",
    .firmware_version = "v1.0",

    // Roles: what commodities this device consumes/produces/stores. At least 1.
    .roles = {
        { .commodity = S2_COMMODITY_ELECTRICITY, .role_type = S2_ROLE_TYPE_ENERGY_CONSUMER }
    },
    .roles_count = 1,

    // Commodity quantities this device can measure. List each once, no duplicates.
    .commodity_quantities = {
        S2_COMMODITY_QUANTITY_ELECTRIC_POWER_L1,
    },
    .commodity_quantities_count = 1,

    .measurement_interval_ms = 0,   // 0 = default (60s)
    .provides_forecast = false,

    .on_request_measurement = on_request_measurement,  // mandatory
    .on_session_ready = on_session_ready,               // mandatory
    .on_request_forecast = on_request_forecast,        // only needed if provides_forecast = true

    .pebc = { ... },
}
```

### PEBC config

```c
.pebc = {
    // Default limits applied when no instruction is active. Mandatory, at least 1 entry.
    .default_limits = {
        {
            .commodity_quantity = S2_COMMODITY_QUANTITY_ELECTRIC_POWER_L1,
            .power_upper_limit = 3680.0f,
            .power_lower_limit = 0.0f,
            .energy_upper_limit = 0.0f,
            .energy_lower_limit = 0.0f,
            .upper_limit_abnormal_only = false,
            .lower_limit_abnormal_only = false,
        },
    },
    .default_limits_count = 1,

    .constraint_interval_ms = 0,        // 0 = default (5 min)
    .energy_constraint_window_ms = 0,   // 0 = default (now + 24h)
    .power_constraint_window_ms = 0,    // 0 = no valid_until (stays valid until replaced/revoked)
    .power_constraint_defer = false,    // false = VANISH (safe default)

    .on_instruction = on_instruction,   // mandatory
    .on_request_param = on_request_param, // mandatory
}
```

`default_limits` defines what the device falls back to when no instruction is active or after one expires (succeeds). The library calls `on_instruction` with these values whenever that happens, so your hardware always has something valid to apply. More on `default_limits` in the Abnormal condition slots section.

## The request/provide pattern

The library never blocks waiting for hardware values. Instead it asks for them via a callback, and you hand the value back with a matching `provide` call.

```txt
library calls on_request_measurement()
    -> you read the sensor / Modbus register
        -> you call s2_client_provide_measurement()
```

Same pattern for constraints:

```txt
library calls on_request_param(S2_PEBC_PARAM_POWER_UPPER_LIMIT_MAX, ...)
    -> you read the value from hardware
        -> you call s2_client_pebc_provide_power_upper_limit_max()
```

The `provide` call must happen in response to the request. Calling it outside of that context returns `ESP_ERR_INVALID_STATE`. You don't need to be async about it; calling `provide` synchronously inside the callback is fine and is the normal way to do it.

## Measurements

Every `measurement_interval_ms` the library calls `on_request_measurement` once per commodity quantity in your config. Read the value from hardware and provide it back.

```c
static void on_request_measurement(s2_commodity_quantity_t quantity) {
    float watts = read_power_from_hardware(quantity); // Modbus, ADC, whatever
    s2_client_provide_measurement(s2_handle, quantity, watts);
}
```

The library sends a `PowerMeasurement` to the CEM after each provide call. The `quantity` parameter tells you which phase or commodity to read. Always use it when you have multiple quantities.

`measurement_interval_ms` defaults to 60 seconds if you set it to 0. The first measurement request fires immediately when the session starts, so the CEM gets data straight away without waiting for the first interval to tick.

## PEBC constraints

Every `constraint_interval_ms` the library calls `on_request_param` to collect the operating limits it needs to send to the CEM. It calls it 8 times per `default_limits` slot so once per `s2_pebc_param_t` value. You call the matching `provide_*` function for each.

```c
static void on_request_param(s2_pebc_param_t param, s2_commodity_quantity_t quantity, bool is_abnormal) {
    switch (param) {
        // Power constraints: what range the CEM may instruct for each power limit
        case S2_PEBC_PARAM_POWER_UPPER_LIMIT_MAX:
            s2_client_pebc_provide_power_upper_limit_max(s2_handle, quantity, 3680.0f);
            break;
        case S2_PEBC_PARAM_POWER_UPPER_LIMIT_MIN:
            s2_client_pebc_provide_power_upper_limit_min(s2_handle, quantity, 0.0f);
            break;
        case S2_PEBC_PARAM_POWER_LOWER_LIMIT_MAX:
            s2_client_pebc_provide_power_lower_limit_max(s2_handle, quantity, 0.0f);
            break;
        case S2_PEBC_PARAM_POWER_LOWER_LIMIT_MIN:
            s2_client_pebc_provide_power_lower_limit_min(s2_handle, quantity, 0.0f);
            break;
        // Energy constraints: average power bounds over the energy window
        case S2_PEBC_PARAM_ENERGY_UPPER_LIMIT_MAX:
            s2_client_pebc_provide_energy_upper_limit_max(s2_handle, quantity, 3680.0f);
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
```

The library waits until all 8 values for a slot are in, then builds and sends [`PEBC.PowerConstraints`](https://docs.s2standard.org/model-reference/PEBC/PEBC.PowerConstraints/) and [`PEBC.EnergyConstraint`](https://docs.s2standard.org/model-reference/PEBC/PEBC.EnergyConstraint/) to the CEM. If the values haven't changed since the last send, it skips it so no duplicate messages.

The `is_abnormal` flag is explained in the Abnormal condition slots section.

### Energy valid_until

By default the library sets the energy constraint's `valid_until` to now + 24h. Override it by calling `s2_client_pebc_provide_energy_valid_until()` in the same `on_request_param` callback as your energy provides:

```c
case S2_PEBC_PARAM_ENERGY_UPPER_LIMIT_MAX:
    s2_client_pebc_provide_energy_upper_limit_max(s2_handle, quantity, 3680.0f);
    s2_client_pebc_provide_energy_valid_until(s2_handle, quantity, time(NULL) + 8 * 3600);
    break;
```

### Intervals and windows

| Field | Default | Effect |
|---|---|---|
| `constraint_interval_ms` | 5 min | How often constraints are re-requested from firmware |
| `energy_constraint_window_ms` | 24h | `valid_until = now + window` on each `PEBC.EnergyConstraint` |
| `power_constraint_window_ms` | none | If set, adds `valid_until` to `PEBC.PowerConstraints`; otherwise omitted (stays valid until replaced or revoked) |
| `power_constraint_defer` | `false` (VANISH) | `true` = DEFER so CEM can reschedule the device instead of dropping the instruction. See [here](https://docs.s2standard.org/model-reference/PEBC/PEBC.PowerEnvelopeConsequenceType/) for more information |

## Abnormal condition slots

Normally the CEM instructs within the limits you declare in `default_limits`. Abnormal slots let you advertise a wider range the CEM may only use during emergencies (grid overload, etc.).

Add a second entry for the same quantity with the abnormal flags set:

```c
// Normal slot
{ .commodity_quantity = S2_COMMODITY_QUANTITY_ELECTRIC_POWER_L1, .power_upper_limit = 3680.0f,
  .upper_limit_abnormal_only = false, .lower_limit_abnormal_only = false, ... },
// Abnormal slot. Only used when CEM sends abnormal_condition = true
{ .commodity_quantity = S2_COMMODITY_QUANTITY_ELECTRIC_POWER_L1, .power_upper_limit = 4000.0f,
  .upper_limit_abnormal_only = true, .lower_limit_abnormal_only = true, ... },
```

`on_request_param` is called once per slot. Use `is_abnormal` to return the right value:

```c
case S2_PEBC_PARAM_POWER_UPPER_LIMIT_MAX:
    s2_client_pebc_provide_power_upper_limit_max(s2_handle, quantity,
        is_abnormal ? 4000.0f : 3680.0f);
    break;
```

The library validates incoming instructions against the correct slot automatically. `on_instruction` just gets the limits - you don't need to know if it was an abnormal instruction.

## Instructions

A [`PEBC.Instruction`](https://docs.s2standard.org/model-reference/PEBC/PEBC.Instruction/) is the CEM telling the device to stay within specific power limits for a given duration. It can contain multiple limit elements, each with its own limits and duration.

When one arrives, the library validates it against your declared constraints, sends `InstructionStatusUpdate` responses automatically, and schedules execution. When it's time, `on_instruction` fires once per commodity quantity with the CEM's limits:

```c
static void on_instruction(s2_commodity_quantity_t quantity, float upper_limit, float lower_limit) {
    // apply limits to hardware here
    // if that fails (hardware fault): call s2_client_pebc_abort_instruction()
}
```

`on_instruction` also fires when an instruction ends. The `default_limits` values from config are then set, so the device goes back to normal. You don't need to track when instructions expire. The library handles the timer.

### Lifecycle

```txt
CEM sends PEBC.Instruction
  -> library sends NEW + ACCEPTED
    -> at execution_time: on_instruction() fires -> library sends STARTED
      -> duration elapses: on_instruction() fires with defaults -> SUCCEEDED
      -> or CEM revokes: on_instruction() fires with defaults -> REVOKED
      -> or firmware calls abort_instruction(): ABORTED
```

If the clock was never synced (the RM reads 1970 until NTP sets it) the library
treats the instruction as due now and runs it immediately, so CEM limits are
never left unapplied. `execution_time` is otherwise followed exactly, however
far ahead it is.

### Aborting

If applying the instruction fails on the hardware side, call:

```c
s2_client_pebc_abort_instruction(s2_handle);
```

This sends `ABORTED` to the CEM, cancels the timers, and resets the device to default limits via `on_instruction`.

## Forecasts

Optional. Set `provides_forecast = true` in `rm_config` to enable it. The library then calls `on_request_forecast` every `forecast_interval_ms` (default 15 min) per commodity quantity. Provide the value back:

```c
static void on_request_forecast(s2_commodity_quantity_t quantity) {
    s2_client_provide_forecast(s2_handle, quantity, 2000.0f);
}
```

If `provides_forecast = false`, `on_request_forecast` is never called and it doesn't need to be set then.

## Revoking constraints

Call this when the device goes offline, loses power, or can no longer honour any constraints:

```c
s2_client_pebc_revoke_constraints(s2_handle);
```

This sends a [`RevokeObject`](https://docs.s2standard.org/model-reference/Common/RevokeObject/) for every `PEBC.PowerConstraints` and `PEBC.EnergyConstraint` that was last successfully sent. After that, the CEM knows the device's constraints are no longer valid and won't send instructions based on them.

If there's nothing to revoke (nothing was sent yet, or everything was already revoked), it does nothing.
