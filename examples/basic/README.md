<!--
SPDX-FileCopyrightText: ElaadNL

SPDX-License-Identifier: Apache-2.0
-->

# Basic Example Setup

This example reads runtime credentials from `examples/basic/.env` at CMake configure time.

## 1) Create local env file

Copy `.env.example` to `.env` in this folder:

```bash
# Linux / macOS
cp .env.example .env

# Windows PowerShell
Copy-Item .env.example .env
```

Edit `.env` and set:

- `WIFI_SSID`
- `WIFI_PASSWORD`
- `CEM_S2_INTEGRATION_URL`
- `PAIRING_ACCESS_TOKEN`

`.env.example` is enough for a build-only check. The CMake configure step falls back to it when `.env` is missing.

## 2) Build, flash, monitor

```bash
# Build only (no device needed)
idf.py build

# Flash to ESP32 and open serial monitor
idf.py flash monitor
```

## 3) Responder-node demo (long polling)

For an RM without an HTTP server, the CEM initiates pairing. Set
`#define RESPONDER_NODE_DEMO 1` in `main/main.c`. The example then polls `waitForPairing` and pairs when the CEM
sends `requestPairing`. In this mode `PAIRING_ACCESS_TOKEN` in `.env` is the
RM's own static token.

`RESPONDER_NODE_DEMO` and `INITIATOR_NODE_DEMO` are exclusive: exactly one must be `1`.
