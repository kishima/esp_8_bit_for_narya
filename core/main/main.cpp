// narya_core — ESP32-WROVER skeleton.
// Brings up app_main and yields. Real subsystems (video, audio, hid_uart) wire in later phases.

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "narya_core";

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "narya_core boot on core %d", xPortGetCoreID());
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
