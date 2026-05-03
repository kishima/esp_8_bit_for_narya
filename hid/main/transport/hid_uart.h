// UART TX side of the HID link from hid (ESP32-S3) to core (ESP32-WROVER).
// Wire format and CRC live in hid_uart_proto.h (shared between core and hid).

#ifndef NARYA_HID_TX_H
#define NARYA_HID_TX_H

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Bring up the configured UART (NARYA_HID_UART_PORT) and prepare TX.
// Idempotent. Returns ESP_OK on success.
esp_err_t hid_uart_tx_init(void);

// Send one button-edge event. btn_idx must match hid_uart_proto.h convention
// (0..3 = D-pad, 4 = Start, 5 = Select, 6 = A, 7 = B).
void hid_uart_send_btn(int btn_idx, int pressed);

// Send a heartbeat packet (no payload) so the core can detect link liveness.
void hid_uart_send_heartbeat(void);

#ifdef __cplusplus
}
#endif

#endif // NARYA_HID_TX_H
