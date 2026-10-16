// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "mdns.h"
#include <stddef.h>

/**
 * @brief Discover CEM endpoints on the LAN via DNS-SD.
 *
 * Browses _s2connect._tcp and returns the pairingUrl (fallback
 * longpollingUrl) from each TXT record.
 *
 * @param timeout_ms Browse timeout in milliseconds, 0 for default (3000ms).
 * @param urls       Array of char* to fill with malloc'd URLs; caller frees them.
 * @param max_urls   Maximum number of URLs to write.
 *
 * @return Number of URLs written, or 0 if none found.
 */
int s2_dns_sd_find_cems(int timeout_ms, char *urls[], size_t max_urls);

/**
 * @brief Build a CEM URL from one mDNS result (unit testing helper).
 */
char *try_build_cem_url(const mdns_result_t *r);

/**
 * @brief Collect CEM URLs from mDNS results (unit testing helper).
 */
int collect_cem_urls(const mdns_result_t *results, char *urls[], size_t max_urls);
