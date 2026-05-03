// narya_hid - ESP32-S3 USB-HOST gamepad bridge skeleton.
// Brings up app_main and yields. USB host + UART TX wire in later phases.

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "narya_hid";

void app_main(void)
{
    ESP_LOGI(TAG, "narya_hid boot on core %d", xPortGetCoreID());
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
