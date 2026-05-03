// UART TX side of the HID link.

#include "hid_uart.h"

#include <string.h>

#include "esp_log.h"
#include "driver/uart.h"

#include "narya_pin_assign.h"
#include "hid_uart_proto.h"

#define TAG "hid_uart_tx"

static bool    s_inited;
static uint8_t s_seq;

esp_err_t hid_uart_tx_init(void)
{
    if (s_inited) return ESP_OK;

    uart_config_t cfg = {
        .baud_rate = NARYA_HID_UART_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    esp_err_t err = uart_param_config(NARYA_HID_UART_PORT, &cfg);
    if (err != ESP_OK) { ESP_LOGE(TAG, "param_config: %s", esp_err_to_name(err)); return err; }

    err = uart_set_pin(NARYA_HID_UART_PORT, NARYA_HID_UART_TX, NARYA_HID_UART_RX,
                       UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (err != ESP_OK) { ESP_LOGE(TAG, "set_pin: %s", esp_err_to_name(err)); return err; }

    // RX buffer kept minimal; we are TX-only on this side. driver requires it
    // to be at least UART_HW_FIFO_LEN_DEFAULT bytes, so use 256.
    err = uart_driver_install(NARYA_HID_UART_PORT, 256, 1024, 0, NULL, 0);
    if (err != ESP_OK) { ESP_LOGE(TAG, "driver_install: %s", esp_err_to_name(err)); return err; }

    s_inited = true;
    ESP_LOGI(TAG, "tx ok port=%d tx=%d baud=%d",
             (int)NARYA_HID_UART_PORT, (int)NARYA_HID_UART_TX, NARYA_HID_UART_BAUD);
    return ESP_OK;
}

static void send_msg(uint8_t type, const uint8_t *payload, uint8_t len)
{
    if (!s_inited) return;

    narya_hid_msg_t msg = {
        .type = type,
        .seq  = (uint8_t)(s_seq & 0x0F),
        .len  = len,
    };
    if (payload && len) memcpy(msg.payload, payload, len);

    uint8_t buf[NARYA_HID_MAX_FRAME];
    size_t n = narya_hid_encode(&msg, buf, sizeof(buf));
    if (n == 0) {
        ESP_LOGW(TAG, "encode failed type=0x%02X len=%u", type, len);
        return;
    }
    int written = uart_write_bytes(NARYA_HID_UART_PORT, (const char*)buf, n);
    if (written != (int)n) {
        ESP_LOGW(TAG, "write_bytes short: %d/%u", written, (unsigned)n);
    }
    s_seq++;
}

void hid_uart_send_btn(int btn_idx, int pressed)
{
    uint8_t p = (uint8_t)(btn_idx & 0xFF);
    send_msg(pressed ? NARYA_EVT_BTN_DOWN : NARYA_EVT_BTN_UP, &p, 1);
}

void hid_uart_send_heartbeat(void)
{
    send_msg(NARYA_EVT_HEARTBEAT, NULL, 0);
}
