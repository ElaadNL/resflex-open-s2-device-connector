<!--
SPDX-FileCopyrightText: ElaadNL

SPDX-License-Identifier: Apache-2.0
-->

# Pairing Workflow

Implements [S2 Connect v1.0](https://docs.s2standard.org/s2-connect/1.0.0/discovery-pairing-authentication/)
pairing, connection initiation, and unpairing. All endpoints use the `/v1/`
version prefix.

> **NOTE**
>
> You need an S2 client handle from `s2_client_init()`. See
> `docs/2-PEBC.md` for configuration.

## Overview

```text
DNS-SD discovery  ->  version negotiation  ->  preparePairing
->  TLS cert fingerprint (TOFU)  ->  requestPairing (HMAC)
->  requestConnectionDetails  ->  finalizePairing
->  initiateSession  ->  confirmAccessToken  ->  WebSocket
```

## 0. CEM Discovery (LAN only)

Discovers CEM endpoints via mDNS / DNS-SD on the local network.
Falls back to the configured `base_url` if discovery fails. Since more than one
CEM may be advertised on the LAN, the API returns a list of URLs and the
application decides which one to use (e.g. by asking the end user).

```c
#define MAX_CEMS 4  // arbitrary, not spec limited

char *cem_urls[MAX_CEMS];
int count = s2_dns_sd_find_cems(3000, cem_urls, MAX_CEMS);  // 3s timeout
if (count > 0) {
    // pick the first one, or let the user choose when count > 1
    strlcpy(handle->server.base_url, cem_urls[0], sizeof(handle->server.base_url));
    for (int i = 0; i < count; i++) free(cem_urls[i]);
}
```

## 1. Version Negotiation

Check which S2 Connect API versions the CEM supports. Must be called before
any pairing calls so the library knows the version prefix.

```c
s2_client_verify_pairing_version(handle);
// -> GET /api/s2_pebc/pairing/
// verifies v1 is supported

// Fetch server node ID:
char server_node_id[40];
s2_client_fetch_server_node_id(handle, server_node_id, sizeof(server_node_id));
// -> GET /api/s2_pebc/pairing/v1/nodes
```

## 2. Prepare Pairing

Notify the CEM that a pairing attempt is about to start. LAN only.

```c
s2_client_prepare_pairing(handle, server_node_id);
// -> POST /api/s2_pebc/pairing/v1/preparePairing
// -> 204 on success

// Cancel if user aborts:
s2_client_cancel_prepare_pairing(handle, server_node_id);
// -> POST /api/s2_pebc/pairing/v1/cancelPreparePairing
```

## 3. TLS Cert Fingerprint (TOFU)

Extract the CEM's TLS certificate and compute its SHA-256 fingerprint.
Used during the HMAC challenge-response. Trust-On-First-Use: the fingerprint
is pinned after the first successful pairing with `s2_client_confirm_certificate_pin`.

```c
s2_client_fetch_server_certificate(handle);
// Connects via TLS, extracts peer cert DER, stores SHA-256 fingerprint.
```

Re-fetch before each new pairing attempt: a CEM restart can change its certificate, and the HMAC is bound to the fingerprint.

## 4. Request Pairing (HMAC Challenge-Response)

Generate a random challenge, base64-encode it, and send it to the CEM along
with node info. The CEM responds with its own challenge and an HMAC response
proving it knows the pairing token.

```c
// Generate 32 random bytes and base64-encode
uint8_t raw_challenge[32];
s2_pairing_generate_challenge(raw_challenge);
char client_challenge_b64[45];
s2_base64_encode_32(raw_challenge, client_challenge_b64);

s2_client_request_pairing_response_t resp;
s2_client_request_pairing(handle, client_challenge_b64, true, false, &resp);
// -> POST /api/s2_pebc/pairing/v1/requestPairing
// -> 200: pairingAttemptId, serverHmacChallenge, clientHmacChallengeResponse

// Verify the CEM's HMAC response (proves it knows the token):
//   expected = HMAC-SHA256(client_challenge, pairing_token + cert_fingerprint)
//   actual   = base64 decode resp.client_hmac_challenge_response
//   compare with hmac.compare_digest

// Compute your response to the CEM's challenge:
//   server_response = HMAC-SHA256(resp.server_hmac_challenge, pairing_token + cert_fingerprint)
//   base64 encode server_response -> server_hmac_response_b64
```

Note: S2 Connect v1.0 defines SHA256 as the only hashing algorithm, so this
library currently only supports SHA256.

## 5. Request Connection Details

Send the server HMAC response to receive connection credentials.

```c
s2_client_connection_details_response_t conn;
s2_client_request_connection_details(handle, resp.pairing_attempt_id,
                                      server_hmac_response_b64, &conn);
// -> POST /api/s2_pebc/pairing/v1/requestConnectionDetails
//   Authorization: Bearer <pairingAttemptId>
// -> 200: conn.access_token, conn.initiate_session_url
```

## 6. Finalize Pairing

Confirm the pairing was successful. Pass `false` if HMAC verification failed.

```c
s2_client_finalize_pairing(handle, resp.pairing_attempt_id, true);
// -> POST /api/s2_pebc/pairing/v1/finalizePairing
//   Authorization: Bearer <pairingAttemptId>
// -> 204

// After successful pairing, pin the cert fingerprint for subsequent connections:
s2_client_confirm_certificate_pin(handle);
```

## 7. Initiate Session

Start a communication session. Returns a pending access token.

```c
s2_client_initiate_session_response_t init_resp;
s2_client_initiate_session(handle, server_node_id, conn.access_token, &init_resp);
// -> POST /api/s2_pebc/connection/v1/initiateSession
//   Authorization: Bearer <conn.access_token>
// -> 200: init_resp.access_token (pending), init_resp.selected_s2_version
```

## 8. Confirm Access Token

Activate the pending token. Returns WebSocket credentials.

```c
s2_client_confirm_access_token_response_t confirm_resp;
s2_client_confirm_access_token_extended(handle, init_resp.access_token, &confirm_resp);
// -> POST /api/s2_pebc/connection/v1/confirmAccessToken
//   Authorization: Bearer <init_resp.access_token>
// -> 200: confirm_resp.websocket_token, confirm_resp.websocket_url
```

## 9. Connect WebSocket

Open the S2 WebSocket channel. Pass the original access token as the fourth
parameter (used for reconnect).

```c
s2_client_websocket_connect(handle, confirm_resp.websocket_url,
                     confirm_resp.websocket_token, conn.access_token);
// -> wss://<host>/api/s2_pebc/ws  (the URL from confirmAccessToken, /connection -> /ws)
//    Authorization: Bearer <websocketToken>
// On connect: sends ResourceManagerDetails, then PEBC constraints.
// Triggers on_session_ready callback.
```

## 10. Unpair

Remove the RM from the CEM's paired nodes.

```c
s2_client_unpair(handle, conn.access_token, server_node_id);
// -> POST /api/s2_pebc/connection/v1/unpair
//   Authorization: Bearer <conn.access_token>
// -> 204
```

## Long Polling

For RMs that can't run an HTTP server (no `preparePairing`), the CEM
initiates pairing via long polling. The RM polls `/waitForPairing` in a loop;
the CEM holds the request (up to 25s) and responds with an action.

```c
s2_long_poll_result_t result;
s2_long_poll_wait(handle, node_id_str, &result);
// -> POST /api/s2_pebc/pairing/v1/waitForPairing
// Holds connection open until CEM sends an action.
// result.action == S2_LP_NO_ACTION: poll again.
// result.action == S2_LP_REQUEST_PAIRING: start pairing flow.
```

Actions and the required client response:

| action | client response |
| --- | --- |
| `sendNodeDescription` | Next poll must include `clientNodeDescription` + `clientEndpointDescription` |
| `preparePairing` | Show the pairing code, poll again |
| `cancelPreparePairing` | Hide the pairing code, poll again |
| `requestPairing` | Start pairing. If no token is available, next poll must send `errorMessage: "NoValidTokenOnPairingClient"` |

Use `s2_long_poll_wait_extended()` to attach those optional fields to the next poll:

```c
s2_long_poll_request_options_t opts = {
    .include_descriptions = true,  // after sendNodeDescription
    .no_valid_token = true,        // after requestPairing with no token
};
s2_long_poll_wait_extended(handle, node_id_str, &opts, &result);
```

Set `include_descriptions` / `no_valid_token` only for the poll immediately
following the matching action; reset them (false) on every other poll.
