// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

#include "unity.h"
#include "s2_dns_sd.h"
#include <string.h>
#include <stdlib.h>

TEST_CASE("returns 0 when no CEM found", "[dns_sd]") {
    char *urls[4] = {0};
    TEST_ASSERT_EQUAL(0, s2_dns_sd_find_cems(1000, urls, 4));
}

TEST_CASE("uses default timeout for non-positive", "[dns_sd]") {
    char *urls[4] = {0};
    TEST_ASSERT_EQUAL(0, s2_dns_sd_find_cems(0, urls, 4));
    TEST_ASSERT_EQUAL(0, s2_dns_sd_find_cems(-1, urls, 4));
}

TEST_CASE("returns 0 for NULL urls or zero max", "[dns_sd]") {
    char *urls[4] = {0};
    TEST_ASSERT_EQUAL(0, s2_dns_sd_find_cems(1000, NULL, 4));
    TEST_ASSERT_EQUAL(0, s2_dns_sd_find_cems(1000, urls, 0));
    TEST_ASSERT_EQUAL(0, collect_cem_urls(NULL, urls, 4));
}

// helper: build a minimal valid result with pairingUrl
static mdns_result_t make_result(const char *url) {
    static mdns_txt_item_t items[3];
    items[0] = (mdns_txt_item_t){.key = "txtvers", .value = "1"};
    items[1] = (mdns_txt_item_t){.key = "pairingUrl", .value = url};
    items[2] = (mdns_txt_item_t){.key = NULL, .value = NULL};
    return (mdns_result_t){
        .txt = items, .txt_count = 2, .port = 443,
        .hostname = "cem.local",
    };
}

TEST_CASE("txt NULL returns NULL", "[dns_sd]") {
    mdns_result_t r = {0};
    TEST_ASSERT_NULL(try_build_cem_url(&r));
}

TEST_CASE("missing txtvers returns NULL", "[dns_sd]") {
    mdns_txt_item_t items[] = {{.key = "pairingUrl", .value = "https://x"}};
    mdns_result_t r = {.txt = items, .txt_count = 1};
    TEST_ASSERT_NULL(try_build_cem_url(&r));
}

TEST_CASE("wrong txtvers returns NULL", "[dns_sd]") {
    mdns_txt_item_t items[] = {
        {.key = "txtvers", .value = "2"},
        {.key = "pairingUrl", .value = "https://x"},
    };
    mdns_result_t r = {.txt = items, .txt_count = 2};
    TEST_ASSERT_NULL(try_build_cem_url(&r));
}

TEST_CASE("txtver without s is accepted", "[dns_sd]") {
    mdns_txt_item_t items[] = {
        {.key = "txtver", .value = "1"},
        {.key = "pairingUrl", .value = "https://x"},
    };
    mdns_result_t r = {.txt = items, .txt_count = 2};
    char *url = try_build_cem_url(&r);
    TEST_ASSERT_NOT_NULL(url);
    TEST_ASSERT_EQUAL_STRING("https://x", url);
    free(url);
}

TEST_CASE("no pairingUrl or longpollingUrl returns NULL", "[dns_sd]") {
    mdns_txt_item_t items[] = {{.key = "txtvers", .value = "1"}};
    mdns_result_t r = {.txt = items, .txt_count = 1};
    TEST_ASSERT_NULL(try_build_cem_url(&r));
}

TEST_CASE("falls back to longpollingUrl", "[dns_sd]") {
    mdns_txt_item_t items[] = {
        {.key = "txtvers", .value = "1"},
        {.key = "longpollingUrl", .value = "https://x"},
    };
    mdns_result_t r = {.txt = items, .txt_count = 2};
    char *url = try_build_cem_url(&r);
    TEST_ASSERT_NOT_NULL(url);
    TEST_ASSERT_EQUAL_STRING("https://x", url);
    free(url);
}

TEST_CASE("returns pairingUrl from TXT record as-is", "[dns_sd]") {
    mdns_result_t r = make_result("https://cem.local:443/pairing/");
    char *url = try_build_cem_url(&r);
    TEST_ASSERT_NOT_NULL(url);
    TEST_ASSERT_EQUAL_STRING("https://cem.local:443/pairing/", url);
    free(url);
}

TEST_CASE("collects CEM urls up to max_urls", "[dns_sd]") {
    mdns_ip_addr_t ip1 = {0}, ip2 = {0};
    mdns_result_t r2 = make_result("https://cem2.local/pairing/");
    r2.addr = &ip2;
    mdns_result_t r1 = make_result("https://cem1.local/pairing/");
    r1.addr = &ip1;
    r1.next = &r2;

    char *urls[3] = {0};
    int n = collect_cem_urls(&r1, urls, 3);
    TEST_ASSERT_EQUAL(2, n);
    TEST_ASSERT_NOT_NULL(urls[0]);
    TEST_ASSERT_NOT_NULL(urls[1]);
    TEST_ASSERT_NULL(urls[2]);
    for (int i = 0; i < n; i++) free(urls[i]);

    char *one[2] = {0};
    n = collect_cem_urls(&r1, one, 1);
    TEST_ASSERT_EQUAL(1, n);
    TEST_ASSERT_NOT_NULL(one[0]);
    TEST_ASSERT_NULL(one[1]);
    free(one[0]);
}

TEST_CASE("skips invalid results and keeps collecting", "[dns_sd]") {
    // first result is invalid (no pairingUrl), second is valid
    mdns_txt_item_t bad[] = {{.key = "txtvers", .value = "1"}};
    mdns_ip_addr_t ip = {0};
    mdns_result_t r2 = make_result("https://cem.local/pairing/");
    r2.addr = &ip;
    mdns_result_t r1 = {.txt = bad, .txt_count = 1, .port = 443,
                        .hostname = "bad.local", .addr = &ip, .next = &r2};

    char *urls[2] = {0};
    int n = collect_cem_urls(&r1, urls, 2);
    TEST_ASSERT_EQUAL(1, n);
    TEST_ASSERT_NOT_NULL(urls[0]);
    TEST_ASSERT_NULL(urls[1]);
    free(urls[0]);
}
