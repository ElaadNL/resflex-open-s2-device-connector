// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

#include "s2_dns_sd.h"

#include "mdns.h"
#include "esp_log.h"

#include <string.h>
#include <stdlib.h>

static const char *TAG = "s2_dns_sd";

#define DEFAULT_TIMEOUT_MS 3000
#define MAX_CEM_URL_LEN 255  /* S2 Connect TXT values are max 255 bytes */

/**
 * @brief Find a TXT value by key.
 *
 * @return Value pointer, or NULL if not found.
 */
static const char *txt_get(const mdns_result_t *r, const char *key) {
    for (size_t i = 0; i < r->txt_count; i++) {
        if (strcmp(r->txt[i].key, key) == 0) return r->txt[i].value;
    }
    return NULL;
}

/**
 * @brief Get a CEM URL from one mDNS result.
 *
 * @return malloc'd URL string, or NULL if the result is not a valid CEM.
 */
char *try_build_cem_url(const mdns_result_t *r) {
    ESP_LOGI(TAG, "mDNS result: host=%s port=%d txt_count=%d",
             r->hostname ? r->hostname : "(null)", r->port, (int)r->txt_count);
    if (!r->txt) return NULL;

    const char *ver = txt_get(r, "txtvers");
    if (!ver) ver = txt_get(r, "txtver");
    if (!ver || strcmp(ver, "1") != 0) {
        ESP_LOGW(TAG, "skipped: txtvers=%s", ver ? ver : "(null)");
        return NULL;
    }

    const char *u = txt_get(r, "pairingUrl");
    if (!u) u = txt_get(r, "longpollingUrl");
    if (!u) {
        ESP_LOGW(TAG, "skipped: no pairingUrl or longpollingUrl");
        return NULL;
    }

    // use the URL from the TXT record (max 255 bytes)
    char *url = malloc(MAX_CEM_URL_LEN + 1);
    if (url) strlcpy(url, u, MAX_CEM_URL_LEN + 1);
    ESP_LOGI(TAG, "found CEM at %s", url ? url : "(null)");
    return url;
}

/**
 * @brief Collect CEM URLs from a list of mDNS results.
 *
 * @return Number of URLs stored, bounded by max_urls.
 */
int collect_cem_urls(const mdns_result_t *results, char *urls[], size_t max_urls) {
    if (!results || !urls || max_urls == 0) return 0;

    int count = 0;
    for (const mdns_result_t *r = results; r && count < (int)max_urls; r = r->next) {
        char *url = try_build_cem_url(r);
        if (url) urls[count++] = url;
    }
    return count;
}

int s2_dns_sd_find_cems(int timeout_ms, char *urls[], size_t max_urls) {
    if (!urls || max_urls == 0) return 0;
    if (timeout_ms <= 0) timeout_ms = DEFAULT_TIMEOUT_MS;

    mdns_result_t *results = NULL;
    esp_err_t err = mdns_query_ptr("_s2connect", "_tcp",
                                    timeout_ms, max_urls, &results);
    if (err != ESP_OK || !results) {
        ESP_LOGW(TAG, "no _s2connect CEM services found on LAN (err=%d, results=%p)",
                 err, (void*)results);
        return 0;
    }

    int count = collect_cem_urls(results, urls, max_urls);

    mdns_query_results_free(results);
    return count;
}
