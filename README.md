<!--
SPDX-FileCopyrightText: ElaadNL

SPDX-License-Identifier: Apache-2.0
-->

# S2 Device Firmware Library

A reference implementation for an S2 Resource Manager (RM).
It is implemented as a ESP-IDF library which can be integrated to create S2-compliant devices.
This library is based on the [S2 Connect v1.0](https://docs.s2standard.org/s2-connect/1.0.0/discovery-pairing-authentication/) specification for pairing and only supports the S2 Power Envelope Based Control (PEBC) control type.

## Disclaimer

This software is an open reference implementation, developed under the
coordination of ElaadNL as part of the
[Residential Flexibility](https://elaad.nl/en/projects/residential-flexibility/) initiative. It is not a finished,
certified, or end-user product. Before publication, the code was reviewed for
security by the European Network for Cyber Security (ENCS); that review is a
snapshot in time, not a guarantee. Anyone who integrates this software into a
product does so at their own risk and is responsible for its secure integration,
ongoing security testing, regulatory compliance, and any certification of that
product. The software is provided "as is", without warranty, under the Apache
License 2.0.

Read the full [disclaimer](DISCLAIMER.md) before using this software.

## Overview

The S2 library provides a high-level abstraction for ESP32 devices to interact with a Customer Energy Manager (CEM) via WebSockets. It handles the complexities of the S2 protocol, including:

- **S2 Connect v1.0 Pairing**: HMAC challenge-response with TLS cert fingerprint pinning (TOFU).
- **DNS-SD Discovery**: Auto-discover CEM on LAN via mDNS.
- **PEBC (Power Envelope Based Control)**: Managing power and energy limits through instructions.
- **Measurements & Forecasts**: Providing real-time telemetry and future load predictions.
- **Long Polling**: Alternative pairing path for RMs without an HTTP server.
- **NVS Persistence**: Access tokens and TLS fingerprint survive reboots.

## Getting Started

### Prerequisites

- [ESP-IDF v5.5.5](https://docs.espressif.com/projects/esp-idf/en/v5.5.5/esp32/get-started/index.html#installation) installed and sourced in your shell.

### Running the Basic Example

The `examples/basic` folder provides a comprehensive example implementation.

**Build only (no device needed):**

```bash
cd examples/basic
idf.py build
```

**Flash to ESP32:**

Copy `examples/basic/.env.example` to `examples/basic/.env` and fill in
your WiFi credentials, CEM URL, and pairing token. Then:

```bash
cd examples/basic
idf.py flash monitor
```

The pairing process uses the `PAIRING_ACCESS_TOKEN` from your `.env` file.

## Integration Guide

To integrate this library into your own ESP-IDF project, follow these steps:

### 1. Configuration

Initialize the `s2_client_config_t` structure. You must define your device's identity, role, and the commodities it handles.

```c
s2_client_config_t config = {
    .node = {
        .id = "your-unique-uuid",
        .brand = "MyBrand",
        .model = "MyModel",
        .role = S2_ROLE_RM,
        .deployment = S2_DEPLOYMENT_LAN,
        .supported_versions = {"0.0.2-beta"},
        // ... other node fields
    },
    .server_config = {
        .base_url = "https://cem.local:443/api/s2_pebc",  // fallback if DNS-SD fails
        .enable_tls = true,
        .skip_cert_cn_check = true,  // needed for LAN self-signed certs
    },
    .rm_config = {
        .control_type = S2_CONTROL_TYPE_PEBC,
        // ... PEBC configuration
    },
    .dns_sd_enabled = true,  // auto-discover CEM via mDNS
    // ... callbacks
};
```

### 2. Implementing Callbacks

The library is event-driven. You must provide callback functions to bridge the library with your hardware:

| Callback | Purpose |
| :--- | :--- |
| `on_instruction` | Triggered when a CEM instruction (e.g., power limit) starts or is revoked. Use this to apply limits to your hardware. |
| `on_request_measurement` | Triggered when the library needs current power/energy data. |
| `on_request_forecast` | Triggered if your device supports load forecasting. |
| `on_request_param` | Triggered during the PEBC handshake to define the valid ranges for constraints. |
| `on_session_ready` | Triggered when the WebSocket connection and PEBC session are fully established. |

### 3. Initialization and Connection

```c
// 1. Initialize
s2_client_handle_t s2_handle = s2_client_init(&config);

// 2. Discover CEM via DNS-SD (LAN), falls back to config base_url
#define MAX_CEMS 4  // set to desired amount

char *cem_urls[MAX_CEMS];
int count = s2_dns_sd_find_cems(3000, cem_urls, MAX_CEMS);
if (count > 0) {
    // pick the first one, or let the user choose when count > 1
    strlcpy(s2_handle->server.base_url, cem_urls[0], sizeof(s2_handle->server.base_url));
    for (int i = 0; i < count; i++) free(cem_urls[i]);
}

// 3. Version negotiation. Check CEM supports v1
s2_client_verify_pairing_version(s2_handle);

// 4. Fetch TLS cert fingerprint (TOFU - trust on first use)
s2_client_fetch_server_certificate(s2_handle);

// 5. Pairing flow (see docs/1-Pairing.md for full details)
s2_client_prepare_pairing(s2_handle, server_node_id);
// ... HMAC challenge-response, requestConnectionDetails, finalizePairing

// 6. Session initiation + WebSocket connect
s2_client_initiate_session(s2_handle, server_node_id, access_token, &init_resp);
s2_client_confirm_access_token_extended(s2_handle, init_resp.access_token, &confirm_resp);
s2_client_websocket_connect(s2_handle, confirm_resp.websocket_url,
                     confirm_resp.websocket_token, access_token);
```

## Documentation

For deep dives into specific protocols and feature implementations:

- [Pairing Process](docs/1-Pairing.md)
- [PEBC (Power Envelope Based Control)](docs/2-PEBC.md)

### Logs -> CEM

- The firmware has a `log_forwarder` that batches local logs and sends `Log.Batch` JSON to the CEM.
- API you use: `log_forwarder_init(handle)` and `log_forwarder_enqueue(level, source, message)`.
- It keeps a small in‑RAM queue, batches (default 6), and flushes periodically (~5s) or when full.
- Batches are sent with the library's tracked send; the CEM must reply with S2 `ReceptionStatus` so the device can untrack on `OK` or retry on `TEMPORARY_ERROR`.
- Unit test: `test/main/test_log_forwarder.c`.

## Development

### Linting

To check code quality using `clang-check`:

```bash
idf.py clang-check
```

### Testing

Tests are available in the `test/` directory. They include:

- **Unit Tests**: For logic validation.
