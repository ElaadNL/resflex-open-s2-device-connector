// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

#include "s2_client_pairing.h"
#include "esp_err.h"

/* Stub for TOFU TLS no-verify callback — the real implementation is gated
   behind #ifndef UNITY_INCLUDE_CONFIG_H, which is defined in test builds. */
esp_err_t s2_client_no_verify_attach(void *conf)
{
    (void)conf;
    return ESP_OK;
}

/* Stub for s2_client_set_pinned_handle — the real implementation is also
   gated behind #ifndef UNITY_INCLUDE_CONFIG_H. */
void s2_client_set_pinned_handle(s2_client_t *handle)
{
    (void)handle;
}
