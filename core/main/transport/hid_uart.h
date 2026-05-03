// UART RX side of the HID link from the hid firmware (ESP32-S3) to the
// core firmware (ESP32-WROVER).

#ifndef NARYA_HID_RX_H
#define NARYA_HID_RX_H

#include <stdint.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "hid_uart_proto.h"

#ifdef __cplusplus
extern "C" {
#endif

// Bring up the UART, install a streaming framer task, and expose decoded
// narya_hid_msg_t through a FreeRTOS queue. Returns ESP_OK on success.
//
// The returned queue is owned by this module; do not delete it. Callers
// xQueueReceive on it to consume events. Frames with a CRC mismatch are
// silently dropped (HID events are state-driven so loss is tolerable).
esp_err_t hid_uart_rx_init(void);

// Receive one decoded HID event. Returns pdTRUE if an event was popped
// before the timeout, pdFALSE otherwise. `out` is filled on success.
BaseType_t hid_uart_rx_recv(narya_hid_msg_t *out, TickType_t timeout);

#ifdef __cplusplus
}
#endif

#endif // NARYA_HID_RX_H
