// UART RX side of the HID link.
//
// Reads a continuous byte stream from NARYA_CORE_HID_UART_PORT and runs a
// 4-state framer:
//   STATE_SOF    -> wait for 0xAA
//   STATE_TYPE   -> read type byte
//   STATE_SEQLEN -> read seq+len byte (high nibble seq, low nibble payload len)
//   STATE_PAY    -> read N payload bytes
//   STATE_CRC    -> read CRC8, validate, post message, return to STATE_SOF
//
// Frames with bad CRC, oversized length, or sync drift are dropped silently.

#include "hid_uart.h"

#include <string.h>

#include "esp_log.h"
#include "freertos/task.h"
#include "driver/uart.h"

#include "narya_pin_assign.h"
#include "hid_uart_proto.h"

#define TAG "hid_uart_rx"

#define RX_BUF_BYTES        1024
#define RX_TASK_STACK       3072
#define RX_TASK_PRIO        6
#define EVENT_QUEUE_DEPTH   16
#define READ_BLOCK_BYTES    32
#define READ_TIMEOUT_TICKS  pdMS_TO_TICKS(50)

typedef enum {
    STATE_SOF = 0,
    STATE_TYPE,
    STATE_SEQLEN,
    STATE_PAY,
    STATE_CRC,
} framer_state_t;

static QueueHandle_t s_q;
static bool          s_inited;

static void rx_task(void *arg)
{
    (void)arg;
    framer_state_t st = STATE_SOF;
    narya_hid_msg_t msg = {0};
    uint8_t pay_idx = 0;
    uint32_t total_rx = 0;
    uint32_t total_drop = 0;

    uint8_t buf[READ_BLOCK_BYTES];
    while (true) {
        int got = uart_read_bytes(NARYA_CORE_HID_UART_PORT, buf, sizeof(buf), READ_TIMEOUT_TICKS);
        if (got <= 0) continue;

        for (int i = 0; i < got; ++i) {
            uint8_t b = buf[i];
            switch (st) {
            case STATE_SOF:
                if (b == NARYA_HID_SOF) st = STATE_TYPE;
                break;
            case STATE_TYPE:
                msg.type = b;
                st = STATE_SEQLEN;
                break;
            case STATE_SEQLEN:
                msg.seq = (uint8_t)((b >> 4) & 0x0F);
                msg.len = (uint8_t)(b & 0x0F);
                pay_idx = 0;
                if (msg.len > NARYA_HID_MAX_PAYLOAD) {
                    total_drop++;
                    st = STATE_SOF;
                } else if (msg.len == 0) {
                    st = STATE_CRC;
                } else {
                    st = STATE_PAY;
                }
                break;
            case STATE_PAY:
                msg.payload[pay_idx++] = b;
                if (pay_idx >= msg.len) st = STATE_CRC;
                break;
            case STATE_CRC: {
                // CRC is over [type, seqlen, payload...].
                uint8_t crc_buf[2 + NARYA_HID_MAX_PAYLOAD];
                crc_buf[0] = msg.type;
                crc_buf[1] = (uint8_t)(((msg.seq & 0x0F) << 4) | (msg.len & 0x0F));
                memcpy(&crc_buf[2], msg.payload, msg.len);
                uint8_t want = narya_hid_crc8(crc_buf, (size_t)(2 + msg.len));
                if (want == b) {
                    if (xQueueSend(s_q, &msg, 0) != pdTRUE) total_drop++;
                    total_rx++;
                } else {
                    total_drop++;
                }
                st = STATE_SOF;
                break;
            }
            }
        }
        // Light periodic counter trace (rare so it does not flood the log).
        if (total_rx && (total_rx & 0xFF) == 0) {
            ESP_LOGD(TAG, "rx=%lu drop=%lu", (unsigned long)total_rx, (unsigned long)total_drop);
        }
    }
}

esp_err_t hid_uart_rx_init(void)
{
    if (s_inited) return ESP_OK;

    s_q = xQueueCreate(EVENT_QUEUE_DEPTH, sizeof(narya_hid_msg_t));
    if (!s_q) return ESP_ERR_NO_MEM;

    uart_config_t cfg = {
        .baud_rate = NARYA_CORE_HID_UART_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    esp_err_t err = uart_param_config(NARYA_CORE_HID_UART_PORT, &cfg);
    if (err != ESP_OK) { ESP_LOGE(TAG, "param_config: %s", esp_err_to_name(err)); return err; }

    err = uart_set_pin(NARYA_CORE_HID_UART_PORT, NARYA_CORE_HID_UART_TX, NARYA_CORE_HID_UART_RX,
                       UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (err != ESP_OK) { ESP_LOGE(TAG, "set_pin: %s", esp_err_to_name(err)); return err; }

    err = uart_driver_install(NARYA_CORE_HID_UART_PORT, RX_BUF_BYTES, 0, 0, NULL, 0);
    if (err != ESP_OK) { ESP_LOGE(TAG, "driver_install: %s", esp_err_to_name(err)); return err; }

    BaseType_t ok = xTaskCreatePinnedToCore(rx_task, "hid_uart_rx",
                                            RX_TASK_STACK, NULL, RX_TASK_PRIO, NULL, 1);
    if (ok != pdPASS) return ESP_FAIL;

    s_inited = true;
    ESP_LOGI(TAG, "rx ok port=%d rx=%d baud=%d",
             (int)NARYA_CORE_HID_UART_PORT, (int)NARYA_CORE_HID_UART_RX, NARYA_CORE_HID_UART_BAUD);
    return ESP_OK;
}

BaseType_t hid_uart_rx_recv(narya_hid_msg_t *out, TickType_t timeout)
{
    if (!s_inited || !out) return pdFALSE;
    return xQueueReceive(s_q, out, timeout);
}
