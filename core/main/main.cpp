// narya_core - ESP32-WROVER core firmware top-level.
//
// Wiring at this stage (P4 first-light without external HID):
//   - Mount LittleFS at /storage
//   - Bring up the NTSC video pipeline (P2)
//   - Bring up I2S1 audio (P3)
//   - Construct the Nofrendo emulator and insert /storage/nestest.nes
//   - emu_task (core 0):  one frame per iteration, hands _lines + audio
//   - perf_task (core 1): 1 Hz status logger
//
// HID UART input (P6) and tighter video frame sync hooks come later.

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_littlefs.h"

#include "narya_pin_assign.h"
#include "hid_uart_proto.h"
#include "video_out.h"
#include "audio_i2s.h"
#include "emu/emu.h"

static const char *TAG = "narya_core";

#define NARYA_LITTLEFS_BASE        "/storage"
#define NARYA_LITTLEFS_PARTITION   "storage"
#define NARYA_DEFAULT_ROM_PATH     "/storage/nestest.nes"
#define NARYA_AUDIO_BUF_SAMPLES    NARYA_AUDIO_MAX_MONO_SAMPLES

static Emu *g_emu = nullptr;

static esp_err_t mount_littlefs(void)
{
    esp_vfs_littlefs_conf_t conf = {
        .base_path              = NARYA_LITTLEFS_BASE,
        .partition_label        = NARYA_LITTLEFS_PARTITION,
        .partition              = nullptr,
        .format_if_mount_failed = false,
        .read_only              = true,
        .dont_mount             = false,
        .grow_on_mount          = false,
    };
    esp_err_t err = esp_vfs_littlefs_register(&conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "littlefs mount failed: %s", esp_err_to_name(err));
        return err;
    }
    size_t total = 0, used = 0;
    if (esp_littlefs_info(NARYA_LITTLEFS_PARTITION, &total, &used) == ESP_OK) {
        ESP_LOGI(TAG, "[littlefs] mounted total=%u used=%u", (unsigned)total, (unsigned)used);
    }
    return ESP_OK;
}

static void emu_task(void *arg)
{
    Emu *emu = (Emu*)arg;
    static int16_t audio_block[NARYA_AUDIO_BUF_SAMPLES];

    while (true) {
        emu->update();                              // run one NES frame
        _lines = emu->video_buffer();               // hand frame to video_isr
        int n = emu->audio_buffer(audio_block, NARYA_AUDIO_BUF_SAMPLES);
        if (n > 0) audio_i2s_write_mono(audio_block, n);   // blocks on DMA queue -> paces loop
    }
}

static void perf_task(void *arg)
{
    (void)arg;
    int last_frames = 0;
    int64_t last_us = esp_timer_get_time();
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        int64_t now = esp_timer_get_time();
        int frames = _frame_counter;
        int dframes = frames - last_frames;
        int64_t dus = now - last_us;
        last_frames = frames;
        last_us     = now;
        ESP_LOGI(TAG, "[perf] frame=%d (+%d in %lldms)", frames, dframes, dus / 1000);
    }
}

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "narya_core boot on core %d", xPortGetCoreID());
    ESP_LOGI(TAG, "[pins] video_dac=%d i2s(bck=%d ws=%d dout=%d) uart(rx=%d tx=%d baud=%d)",
             NARYA_CORE_VIDEO_DAC,
             NARYA_CORE_I2S_BCK, NARYA_CORE_I2S_WS, NARYA_CORE_I2S_DOUT,
             NARYA_CORE_HID_UART_RX, NARYA_CORE_HID_UART_TX, NARYA_CORE_HID_UART_BAUD);
    ESP_LOGI(TAG, "[proto] sof=0x%02X max_frame=%u", NARYA_HID_SOF, (unsigned)NARYA_HID_MAX_FRAME);

    if (mount_littlefs() != ESP_OK) {
        ESP_LOGE(TAG, "halt: littlefs mount failed");
        return;
    }
    if (audio_i2s_init() != ESP_OK) {
        ESP_LOGE(TAG, "halt: i2s init failed");
        return;
    }

    // Construct emulator and insert ROM. video_init() must be called with
    // the emulator's NTSC palette so the ISR's blit() produces correct color.
    g_emu = NewNofrendo(/*ntsc=*/1);
    if (!g_emu) {
        ESP_LOGE(TAG, "halt: NewNofrendo failed");
        return;
    }

    if (g_emu->insert(NARYA_DEFAULT_ROM_PATH, 1, 0) != 0) {
        ESP_LOGE(TAG, "halt: insert %s failed", NARYA_DEFAULT_ROM_PATH);
        return;
    }
    ESP_LOGI(TAG, "[emu] rom=%s loaded", NARYA_DEFAULT_ROM_PATH);

    video_init(g_emu->cc_width, EMU_NES, g_emu->composite_palette(), /*ntsc=*/1);
    ESP_LOGI(TAG, "[video] init ok ntsc=1 samples_per_cc=%d", g_emu->cc_width);

    xTaskCreatePinnedToCore(emu_task,  "emu_task",  6 * 1024, g_emu, 4, nullptr, 0);
    xTaskCreatePinnedToCore(perf_task, "perf_task", 3 * 1024, nullptr, 2, nullptr, 1);
}
