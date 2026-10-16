// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

#include "freertos_stubs.h"
#include "Mockqueue.h"
#include "s2_client_http.h"

static esp_err_t m_request_result = ESP_OK;

static BaseType_t queue_send_set_request_result_stub(
    QueueHandle_t xQueue,
    const void *const pvItemToQueue,
    TickType_t xTicksToWait,
    const BaseType_t xCopyPosition,
    int cmock_num_calls
) {
    // The queue stores s2_request_t * (a pointer). pvItemToQueue points at the
    // pointer that should be copied into the queue, so dereference once to get
    // the request pointer itself
    if (pvItemToQueue) {
        s2_request_t *req = *(s2_request_t **)pvItemToQueue;
        if (req) req->result = m_request_result;
    }
    return pdTRUE;
}

void freertos_stubs_queue_send_set_request_result(esp_err_t result) {
    m_request_result = result;
    xQueueGenericSend_Stub(queue_send_set_request_result_stub);
}
