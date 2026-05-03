// narya_core - ESP32-WROVER core firmware top-level.
// Currently exercises the NTSC video pipeline only (P2 bring-up).
// Audio (I2S), HID UART, and the Nofrendo emulator wire in later phases.

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "narya_pin_assign.h"
#include "hid_uart_proto.h"
#include "video_out.h"

static const char *TAG = "narya_core";

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "narya_core boot on core %d", xPortGetCoreID());
    ESP_LOGI(TAG, "[pins] video_dac=%d i2s(bck=%d ws=%d dout=%d) uart(rx=%d tx=%d baud=%d)",
             NARYA_CORE_VIDEO_DAC,
             NARYA_CORE_I2S_BCK, NARYA_CORE_I2S_WS, NARYA_CORE_I2S_DOUT,
             NARYA_CORE_HID_UART_RX, NARYA_CORE_HID_UART_TX, NARYA_CORE_HID_UART_BAUD);
    ESP_LOGI(TAG, "[proto] sof=0x%02X max_frame=%u", NARYA_HID_SOF, (unsigned)NARYA_HID_MAX_FRAME);

    // P2: bring up NTSC video chain (I2S0 + APLL + DMA + DAC1).
    // _lines stays NULL so the ISR is a no-op; the DMA buffers are then
    // pre-filled with a synthetic test waveform so GPIO25 emits a periodic
    // signal that can be observed on a scope.
    video_init(/*samples_per_cc=*/4, /*machine=*/EMU_NES, /*palette=*/nullptr, /*ntsc=*/1);
    video_test_fill();
    ESP_LOGI(TAG, "[video] init ok ntsc=1 samples_per_cc=4 machine=EMU_NES");

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        ESP_LOGI(TAG, "[perf] frame=%d", _frame_counter);
    }
}
