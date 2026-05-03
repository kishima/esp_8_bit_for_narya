// I2S1 audio output for the Narya core firmware.
//
// 15720 Hz / 16-bit / stereo (Philips slot). Mono frames passed via
// audio_i2s_write_mono are expanded into stereo internally. Calls block on
// the I2S DMA queue, which provides natural back-pressure.

#ifndef NARYA_AUDIO_I2S_H
#define NARYA_AUDIO_I2S_H

#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Anemoia produces audio at 44100 Hz in 128-frame stereo blocks. Match
// that on the I2S side; the driver will reclock as needed for whatever
// downstream DAC is wired to BCK/WS/DOUT.
#define NARYA_AUDIO_SAMPLE_RATE_HZ   44100
#define NARYA_AUDIO_MAX_MONO_SAMPLES 768      // covers 44100/60 = 735 frames + slack

// Bring up I2S1 with the Narya pin assignment. Returns ESP_OK on success.
// Must be called before audio_i2s_write_mono. Idempotent.
esp_err_t audio_i2s_init(void);

// Push `n_samples` 16-bit signed mono samples; each is duplicated to L+R.
// `n_samples` must be <= NARYA_AUDIO_MAX_MONO_SAMPLES. Blocks until the DMA
// queue has accepted all bytes.
void audio_i2s_write_mono(const int16_t *samples, size_t n_samples);

#ifdef __cplusplus
}
#endif

#endif // NARYA_AUDIO_I2S_H
