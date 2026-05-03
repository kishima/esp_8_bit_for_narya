// Public API for the NTSC composite video pipeline.
//
// Origin: derived from esp_8_bit/src/video_out.h (Peter Barrett, 2020).
// Adapted for pure ESP-IDF v5.x (Arduino dependencies removed; LEDC PWM audio
// path stripped because audio is delivered via I2S in this port).

#ifndef NARYA_VIDEO_OUT_H
#define NARYA_VIDEO_OUT_H

#include <stdint.h>

// Emulator family ids used by the per-line blit routine.
// Mirrors esp_8_bit's emu.h values; redefined locally to avoid pulling in
// the full emu.h header before P4.
#ifndef EMU_NES
#define EMU_ATARI 1
#define EMU_NES   2
#define EMU_SMS   3
#define EMU_NES6  4
#endif

#ifdef __cplusplus
extern "C" {
#endif

// Initialize the NTSC video pipeline (APLL + I2S0 in DAC parallel mode + DMA).
//   samples_per_cc: samples per color clock (3 or 4). 4 = NTSC 4xCC.
//   machine:        EMU_NES / EMU_ATARI / EMU_SMS (drives blit pixel layout).
//   palette:        256-entry YUV-phase palette, packed 4 samples per uint32_t.
//   ntsc:           1 = NTSC, 0 = PAL. PAL is not supported in the Narya port.
void video_init(int samples_per_cc, int machine, const uint32_t* palette, int ntsc);

// Wait until just before the next active region ends, so the emulator can
// refresh _lines without tearing.
void video_sync(void);

// Set of scanline source pointers consumed by the ISR. The emulator owns
// the underlying memory; the ISR reads one byte-array per active scanline.
// While _lines == NULL the ISR is a no-op and the DMA buffers are replayed
// verbatim, which is useful for bring-up.
extern uint8_t** _lines;

// Frame / line counters maintained by the ISR.
extern volatile int _frame_counter;
extern volatile int _line_counter;

// Pre-fill the two DMA line buffers with a synthetic test waveform so that,
// while _lines is NULL, GPIO25 (DAC1) emits a periodic signal observable on
// a scope. Call after video_init().
void video_test_fill(void);

#ifdef __cplusplus
}
#endif

#endif // NARYA_VIDEO_OUT_H
