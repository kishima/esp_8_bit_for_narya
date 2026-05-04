// NTSC composite video pipeline for ESP32 (DAC1 / GPIO25), ported from
// esp_8_bit/src/video_out.h (Peter Barrett, 2020).
//
// Original copyright (preserved):
//
// Copyright (c) 2020, Peter Barrett
// Permission to use, copy, modify, and/or distribute this software for
// any purpose with or without fee is hereby granted, provided that the
// above copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL
// WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED
// WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR
// BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES
// OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS,
// WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION,
// ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS
// SOFTWARE.
//
// Narya port changes:
//   - Removed all Arduino API uses (LEDC, pinMode, audio_sample, ir_sample).
//   - Removed the LEDC-PWM audio side-channel (audio_buffer / audio_write_16).
//     Audio is delivered via I2S1 in a separate module (audio_i2s.cpp).
//   - Removed simulator branch (#ifndef ESP_PLATFORM); ESP32 only.
//   - Public surface narrowed to video_init / video_sync / video_test_fill.

#include <math.h>
#include <stdint.h>
#include <stdio.h>

#include "esp_attr.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_intr_alloc.h"
#include "esp_types.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "rom/lldesc.h"
#include "soc/gpio_reg.h"
#include "soc/i2s_reg.h"
#include "soc/i2s_struct.h"
#include "soc/io_mux_reg.h"
#include "soc/rtc.h"
#include "soc/rtc_io_reg.h"
#include "soc/soc.h"

#include "driver/dac.h"           // legacy: dac_output_enable / dac_i2s_enable
#include "driver/gpio.h"
#include "esp_private/periph_ctrl.h"  // periph_module_enable (moved in IDF v5)

#include "video_out.h"

// The upstream esp_8_bit code uses [N^1] indexing pervasively for byte-swapped
// 16-bit DAC writes, and post-increments volatile counters from the ISR.
// Both are safe in this context but trigger newer GCC warnings; silence them
// at file scope rather than rewriting the time-critical render loop.
#pragma GCC diagnostic ignored "-Wxor-used-as-pow"
#pragma GCC diagnostic ignored "-Wvolatile"

//====================================================================================================
// Low-level HW: DAC / DMA / APLL
//====================================================================================================

static int _pal_ = 0;

static lldesc_t _dma_desc[4] = {};
static intr_handle_t _isr_handle;

extern "C" void video_isr(volatile void* buf);

// I2S0 EOF interrupt -> hand the just-finished line buffer to the renderer.
static void IRAM_ATTR i2s_intr_handler_video(void *arg)
{
    if (I2S0.int_st.out_eof)
        video_isr(const_cast<uint8_t*>(((lldesc_t*)I2S0.out_eof_des_addr)->buf));
    I2S0.int_clr.val = I2S0.int_st.val;
}

static esp_err_t start_dma(int line_width, int samples_per_cc, int ch = 1)
{
    periph_module_enable(PERIPH_I2S0_MODULE);

    if (esp_intr_alloc(ETS_I2S0_INTR_SOURCE,
                       ESP_INTR_FLAG_LEVEL1 | ESP_INTR_FLAG_IRAM,
                       i2s_intr_handler_video, 0, &_isr_handle) != ESP_OK) {
        return ESP_FAIL;
    }

    // reset conf
    I2S0.conf.val = 1;
    I2S0.conf.val = 0;
    I2S0.conf.tx_right_first = 1;
    I2S0.conf.tx_mono = (ch == 2 ? 0 : 1);

    I2S0.conf2.lcd_en = 1;
    I2S0.fifo_conf.tx_fifo_mod_force_en = 1;
    I2S0.sample_rate_conf.tx_bits_mod = 16;
    I2S0.conf_chan.tx_chan_mod = (ch == 2) ? 0 : 1;

    // Create TX DMA buffers (two-line ping-pong).
    for (int i = 0; i < 2; i++) {
        int n = line_width * 2 * ch;
        if (n >= 4092) {
            printf("DMA chunk too big: %d\n", n);
            return ESP_FAIL;
        }
        _dma_desc[i].buf = (uint8_t*)heap_caps_calloc(1, n, MALLOC_CAP_DMA);
        if (!_dma_desc[i].buf)
            return ESP_FAIL;

        _dma_desc[i].owner  = 1;
        _dma_desc[i].eof    = 1;
        _dma_desc[i].length = n;
        _dma_desc[i].size   = n;
        _dma_desc[i].empty  = (uint32_t)(i == 1 ? _dma_desc : _dma_desc + 1);
    }
    I2S0.out_link.addr = (uint32_t)_dma_desc;

    //  APLL setup. See esp32 ref 3.2.7 Audio PLL.
    //  f_xtal = (int)rtc_clk_xtal_freq_get() * 1000000;
    //  f_out  = xtal_freq * (4 + sdm2 + sdm1/256 + sdm0/65536); // 250 < f_out < 500
    //  apll_freq = f_out / ((o_div + 2) * 2)
    //  Pick sdm0/sdm1/sdm2 to produce nice multiples of colorburst frequencies.
    //  ESP-IDF v5 split the legacy rtc_clk_apll_enable(en, sdm0, sdm1, sdm2, o_div)
    //  into rtc_clk_apll_enable(en) + rtc_clk_apll_coeff_set(o_div, sdm0, sdm1, sdm2).
    rtc_clk_apll_enable(true);
    if (!_pal_) {
        switch (samples_per_cc) {
            case 3: rtc_clk_apll_coeff_set(2, 0x46, 0x97, 0x4); break; // 10.7386 MHz, 3x NTSC
            case 4: rtc_clk_apll_coeff_set(1, 0x46, 0x97, 0x4); break; // 14.3181 MHz, 4x NTSC
        }
    } else {
        rtc_clk_apll_coeff_set(1, 0x04, 0xA4, 0x6); // 17.7344 MHz, ~4x PAL
    }

    I2S0.clkm_conf.clkm_div_num = 1;
    I2S0.clkm_conf.clkm_div_b   = 0;
    I2S0.clkm_conf.clkm_div_a   = 1;
    I2S0.sample_rate_conf.tx_bck_div_num = 1;
    I2S0.clkm_conf.clka_en      = 1;
    I2S0.fifo_conf.tx_fifo_mod  = (ch == 2) ? 0 : 1;

    dac_output_enable(DAC_CHAN_0);      // DAC1 (alias DAC_CHAN_0) -> GPIO25 (Narya video out)
    dac_i2s_enable();                   // route I2S0 to internal DAC

    I2S0.conf.tx_start = 1;             // start DMA
    I2S0.int_clr.val   = 0xFFFFFFFF;
    I2S0.int_ena.out_eof = 1;
    I2S0.out_link.start  = 1;
    return esp_intr_enable(_isr_handle);
}

static void video_init_hw(int line_width, int samples_per_cc)
{
    start_dma(line_width, samples_per_cc, 1);
    // Audio is on I2S1 in this port; nothing else to do here.
}

//====================================================================================================
// Video timing constants
//====================================================================================================

// Color clock frequency is 315/88 MHz (3.57954545455).
// DAC_MHZ is 315/11, or 8x color clock.
// 455/2 color clocks per line, round up to maintain phase.
// HSYNC period is 44/315 * 455 us  ~= 63.5556 us.
// Field period is 262 * (44/315 * 455) us ~= 16651.6 us.

#define IRE(_x)          ((uint32_t)(((_x) + 40) * 255 / 3.3 / 147.5) << 8)   // 3.3V DAC
#define SYNC_LEVEL       IRE(-40)
#define BLANKING_LEVEL   IRE(0)
#define BLACK_LEVEL      IRE(7.5)
#define GRAY_LEVEL       IRE(50)
#define WHITE_LEVEL      IRE(100)

// Each palette entry packs 4 phase samples, accessed via these macros.
#define P0 (color >> 16)
#define P1 (color >> 8)
#define P2 (color)
#define P3 (color << 8)

// Public exports (declared in video_out.h).
uint8_t** _lines = nullptr;
volatile int _line_counter  = 0;
volatile int _frame_counter = 0;

static int _active_lines;
static int _line_count;
static int _line_width;
static int _samples_per_cc;
static int _machine;
static const uint32_t* _palette;

static float _sample_rate;

static int _hsync;
static int _hsync_long;
static int _hsync_short;
static int _burst_start;
static int _burst_width;
static int _active_start;

static int16_t* _burst0 = nullptr;
static int16_t* _burst1 = nullptr;

static int usec(float us)
{
    uint32_t r = (uint32_t)(us * _sample_rate);
    // Round to multiple of color clock, word-aligned.
    return ((r + _samples_per_cc) / (_samples_per_cc << 1)) * (_samples_per_cc << 1);
}

#define NTSC_COLOR_CLOCKS_PER_SCANLINE 228       // really 227.5 for NTSC, avoid half-phase fiddling
#define NTSC_FREQUENCY                 (315000000.0 / 88)
#define NTSC_LINES                     262

#define PAL_COLOR_CLOCKS_PER_SCANLINE  284
#define PAL_FREQUENCY                  4433618.75
#define PAL_LINES                      312

static void pal_init();

void video_init(int samples_per_cc, int machine, const uint32_t* palette, int ntsc)
{
    _samples_per_cc = samples_per_cc;
    _machine        = machine;
    _palette        = palette;

    if (ntsc) {
        _sample_rate  = 315.0 / 88 * samples_per_cc;
        _line_width   = NTSC_COLOR_CLOCKS_PER_SCANLINE * samples_per_cc;
        _line_count   = NTSC_LINES;
        _hsync_long   = usec(63.555 - 4.7);
        // Shift the active area ~1 us earlier in the scanline so the
        // 192 cc (~53.7 us) NES image stops fitting flush against the
        // line end. Upstream esp_8_bit's usec(10) had no front porch on
        // the right, which on TVs with even slight overscan clipped
        // several pixels off Wizardry-style edge UIs. usec(9) shifts the
        // image left by ~3 NES pixels, leaving ~0.86 us front porch and
        // splitting overscan loss roughly evenly between the two edges.
        // 9 us is still well past the colorburst (~5..8 us) so the
        // chroma latch has ample settle time before the active region.
        _active_start = usec(samples_per_cc == 4 ? 9 : 10.5);
        _hsync        = usec(4.7);
        _pal_         = 0;
    } else {
        pal_init();
        _pal_ = 1;
    }

    _active_lines = 240;
    video_init_hw(_line_width, _samples_per_cc);
}

//====================================================================================================
// PAL  (kept compiled but not exercised in the Narya NTSC port)
//====================================================================================================

static void pal_init()
{
    int cc_width = 4;
    _sample_rate  = PAL_FREQUENCY * cc_width / 1000000.0;
    _line_width   = PAL_COLOR_CLOCKS_PER_SCANLINE * cc_width;
    _line_count   = PAL_LINES;
    _hsync_short  = usec(2);
    _hsync_long   = usec(30);
    _hsync        = usec(4.7);
    _burst_start  = usec(5.6);
    _burst_width  = (int)(10 * cc_width + 4) & 0xFFFE;
    _active_start = usec(10.4);

    _burst0 = new int16_t[_burst_width];
    _burst1 = new int16_t[_burst_width];
    float phase = 2 * M_PI / 2;
    for (int i = 0; i < _burst_width; i++) {
        _burst0[i] = BLANKING_LEVEL + sinf(phase + 3 * M_PI / 4) * BLANKING_LEVEL / 1.5f;
        _burst1[i] = BLANKING_LEVEL + sinf(phase - 3 * M_PI / 4) * BLANKING_LEVEL / 1.5f;
        phase += 2 * M_PI / cc_width;
    }
}

static void IRAM_ATTR blit_pal(uint8_t* src, uint16_t* dst)
{
    uint32_t c, color;
    bool even = _line_counter & 1;
    const uint32_t* p = even ? _palette : _palette + 256;
    int left = 0;
    int right = 256;
    uint8_t mask = 0xFF;
    uint8_t c0, c1, c2, c3, c4;
    uint8_t y1, y2, y3;

    switch (_machine) {
        case EMU_ATARI:
            // PAL is 5/4 wider than NTSC (288 cc vs 228), stretch luma to fit.
            left  = 24;
            right = 384 - 24;
            dst += 40;
            for (int i = left; i < right; i += 4) {
                c = *((uint32_t*)(src + i));

                c0 = c;
                c1 = c >> 8;
                c3 = c >> 16;
                c4 = c >> 24;
                y1 = (((c1 & 0xF) << 1) + ((c0 + c1) & 0x1F) + 2) >> 2;
                y2 = ((c1 + c3 + 1) >> 1) & 0xF;
                y3 = (((c3 & 0xF) << 1) + ((c3 + c4) & 0x1F) + 2) >> 2;
                c1 = (c1 & 0xF0) + y1;
                c2 = (c1 & 0xF0) + y2;
                c3 = (c3 & 0xF0) + y3;

                color = p[c0]; dst[0^1] = P0; dst[1^1] = P1;
                color = p[c1]; dst[2^1] = P2; dst[3^1] = P3;
                color = p[c2]; dst[4^1] = P0; dst[5^1] = P1;
                color = p[c3]; dst[6^1] = P2; dst[7^1] = P3;
                color = p[c4]; dst[8^1] = P0; dst[9^1] = P1;

                i += 4;
                c = *((uint32_t*)(src + i));

                c0 = c;
                c1 = c >> 8;
                c3 = c >> 16;
                c4 = c >> 24;
                y1 = (((c1 & 0xF) << 1) + ((c0 + c1) & 0x1F) + 2) >> 2;
                y2 = ((c1 + c3 + 1) >> 1) & 0xF;
                y3 = (((c3 & 0xF) << 1) + ((c3 + c4) & 0x1F) + 2) >> 2;
                c1 = (c1 & 0xF0) + y1;
                c2 = (c1 & 0xF0) + y2;
                c3 = (c3 & 0xF0) + y3;

                color = p[c0]; dst[10^1] = P2; dst[11^1] = P3;
                color = p[c1]; dst[12^1] = P0; dst[13^1] = P1;
                color = p[c2]; dst[14^1] = P2; dst[15^1] = P3;
                color = p[c3]; dst[16^1] = P0; dst[17^1] = P1;
                color = p[c4]; dst[18^1] = P2; dst[19^1] = P3;
                dst += 20;
            }
            return;

        case EMU_NES:
            mask = 0x3F;
            if (!even)
                p = _palette + 64;
            dst += 88;
            break;

        case EMU_SMS:
            dst += 88;
            break;
    }

    for (int i = left; i < right; i += 4) {
        c = *((uint32_t*)(src + i));
        color = p[c & mask];
        dst[0^1]  = P0; dst[1^1]  = P1; dst[2^1]  = P2;
        color = p[(c >> 8) & mask];
        dst[3^1]  = P3; dst[4^1]  = P0; dst[5^1]  = P1;
        color = p[(c >> 16) & mask];
        dst[6^1]  = P2; dst[7^1]  = P3; dst[8^1]  = P0;
        color = p[(c >> 24) & mask];
        dst[9^1]  = P1; dst[10^1] = P2; dst[11^1] = P3;
        dst += 12;
    }
}

static void IRAM_ATTR burst_pal(uint16_t* line)
{
    line += _burst_start;
    int16_t* b = (_line_counter & 1) ? _burst0 : _burst1;
    for (int i = 0; i < _burst_width; i += 2) {
        line[i^1] = b[i];
        line[(i + 1)^1] = b[i + 1];
    }
}

//====================================================================================================
// NTSC scanline rendering
//====================================================================================================

static void IRAM_ATTR blit(uint8_t* src, uint16_t* dst)
{
    uint32_t* d = (uint32_t*)dst;
    const uint32_t* p = _palette;
    uint32_t color, c;
    uint32_t mask = 0xFF;
    int i;

    if (_pal_) {
        blit_pal(src, dst);
        return;
    }

    switch (_machine) {
        case EMU_ATARI:
            // 2 pixels per color clock, 4 samples per cc; show only center 336 px.
            src += 24;
            d   += 16;
            for (i = 0; i < (384 - 48); i += 4) {
                uint32_t cc = *((uint32_t*)src);
                d[0] = p[(uint8_t)cc];
                d[1] = p[(uint8_t)(cc >> 8)] << 8;
                d[2] = p[(uint8_t)(cc >> 16)];
                d[3] = p[(uint8_t)(cc >> 24)] << 8;
                d   += 4;
                src += 4;
            }
            break;

        case EMU_NES:
            mask = 0x3F;
            // fallthrough
        case EMU_SMS:
            // 4 pixels over 3 color clocks, 4 samples per cc; 192 cc wide.
            for (i = 0; i < 256; i += 4) {
                c = *((uint32_t*)(src + i));
                color = p[c & mask];
                dst[0^1]  = P0; dst[1^1]  = P1; dst[2^1]  = P2;
                color = p[(c >> 8) & mask];
                dst[3^1]  = P3; dst[4^1]  = P0; dst[5^1]  = P1;
                color = p[(c >> 16) & mask];
                dst[6^1]  = P2; dst[7^1]  = P3; dst[8^1]  = P0;
                color = p[(c >> 24) & mask];
                dst[9^1]  = P1; dst[10^1] = P2; dst[11^1] = P3;
                dst += 12;
            }
            break;
    }
}

static void IRAM_ATTR burst(uint16_t* line)
{
    if (_pal_) {
        burst_pal(line);
        return;
    }

    int i, phase;
    switch (_samples_per_cc) {
        case 4:
            for (i = _hsync; i < _hsync + (4 * 10); i += 4) {
                line[i + 1] = BLANKING_LEVEL;
                line[i + 0] = BLANKING_LEVEL + BLANKING_LEVEL / 2;
                line[i + 3] = BLANKING_LEVEL;
                line[i + 2] = BLANKING_LEVEL - BLANKING_LEVEL / 2;
            }
            break;
        case 3:
            phase = 0.866025 * BLANKING_LEVEL / 2;
            for (i = _hsync; i < _hsync + (3 * 10); i += 6) {
                line[i + 1] = BLANKING_LEVEL;
                line[i + 0] = BLANKING_LEVEL + phase;
                line[i + 3] = BLANKING_LEVEL - phase;
                line[i + 2] = BLANKING_LEVEL;
                line[i + 5] = BLANKING_LEVEL + phase;
                line[i + 4] = BLANKING_LEVEL - phase;
            }
            break;
    }
}

static void IRAM_ATTR sync(uint16_t* line, int syncwidth)
{
    for (int i = 0; i < syncwidth; i++)
        line[i] = SYNC_LEVEL;
}

static void IRAM_ATTR blanking(uint16_t* line, bool vbl)
{
    int syncwidth = vbl ? _hsync_long : _hsync;
    sync(line, syncwidth);
    for (int i = syncwidth; i < _line_width; i++)
        line[i] = BLANKING_LEVEL;
    if (!vbl)
        burst(line);
}

// Fancy PAL non-interlace (kept for future PAL support; unused in NTSC mode).
static void IRAM_ATTR pal_sync2(uint16_t* line, int width, int swidth)
{
    swidth = swidth ? _hsync_long : _hsync_short;
    int i;
    for (i = 0; i < swidth; i++)
        line[i] = SYNC_LEVEL;
    for (; i < width; i++)
        line[i] = BLANKING_LEVEL;
}

static const uint8_t DRAM_ATTR _sync_type[8] = {0, 0, 0, 3, 3, 2, 0, 0};
static void IRAM_ATTR pal_sync(uint16_t* line, int i)
{
    uint8_t t = _sync_type[i - 304];
    pal_sync2(line, _line_width / 2, t & 2);
    pal_sync2(line + _line_width / 2, _line_width / 2, t & 1);
}

//====================================================================================================
// Test pattern
//====================================================================================================

static const uint8_t _sin64[64] = {
    0x20, 0x22, 0x25, 0x28, 0x2B, 0x2E, 0x30, 0x33,
    0x35, 0x37, 0x38, 0x3A, 0x3B, 0x3C, 0x3D, 0x3D,
    0x3D, 0x3D, 0x3D, 0x3C, 0x3B, 0x3A, 0x38, 0x37,
    0x35, 0x33, 0x30, 0x2E, 0x2B, 0x28, 0x25, 0x22,
    0x20, 0x1D, 0x1A, 0x17, 0x14, 0x11, 0x0F, 0x0C,
    0x0A, 0x08, 0x07, 0x05, 0x04, 0x03, 0x02, 0x02,
    0x02, 0x02, 0x02, 0x03, 0x04, 0x05, 0x07, 0x08,
    0x0A, 0x0C, 0x0F, 0x11, 0x14, 0x17, 0x1A, 0x1D,
};
static uint8_t _x;

static void IRAM_ATTR test_wave(volatile void* vbuf, int t = 1)
{
    uint16_t* buf = (uint16_t*)vbuf;
    int n = _line_width;
    switch (t) {
        case 0: // f/64 sinewave
            for (int i = 0; i < n; i += 2) {
                buf[0^1] = GRAY_LEVEL + (_sin64[_x++ & 0x3F] << 8);
                buf[1^1] = GRAY_LEVEL + (_sin64[_x++ & 0x3F] << 8);
                buf += 2;
            }
            break;
        case 1: // fast square wave
            for (int i = 0; i < n; i += 2) {
                buf[0^1] = GRAY_LEVEL - (0x10 << 8);
                buf[1^1] = GRAY_LEVEL + (0x10 << 8);
                buf += 2;
            }
            break;
    }
}

void video_test_fill(void)
{
    // lldesc_t.buf is const-volatile; we own the buffer so cast is safe here.
    if (_dma_desc[0].buf) test_wave(const_cast<uint8_t*>(_dma_desc[0].buf), 1);
    if (_dma_desc[1].buf) test_wave(const_cast<uint8_t*>(_dma_desc[1].buf), 1);
}

//====================================================================================================
// Frame sync / ISR
//====================================================================================================

void video_sync(void)
{
    if (!_lines)
        return;
    int n = 0;
    if (_pal_) {
        if (_line_counter < _active_lines)
            n = (_active_lines - _line_counter) * 1000 / 15600;
    } else {
        if (_line_counter < _active_lines)
            n = (_active_lines - _line_counter) * 1000 / 15720;
    }
    vTaskDelay(n + 1);
}

// Workhorse ISR: renders one full scanline (sync + burst + blit) into vbuf.
// Audio is *not* tapped here in the Narya port; it lives on I2S1 instead.
extern "C"
void IRAM_ATTR video_isr(volatile void* vbuf)
{
    if (!_lines)
        return;

    int i = _line_counter++;
    uint16_t* buf = (uint16_t*)vbuf;
    if (_pal_) {
        if (i < 32) {
            blanking(buf, false);
        } else if (i < _active_lines + 32) {
            sync(buf, _hsync);
            burst(buf);
            blit(_lines[i - 32], buf + _active_start);
        } else if (i < 304) {
            if (i < 272)
                blanking(buf, false);
        } else {
            pal_sync(buf, i);
        }
    } else {
        if (i < _active_lines) {
            sync(buf, _hsync);
            burst(buf);
            blit(_lines[i], buf + _active_start);
        } else if (i < (_active_lines + 5)) {
            blanking(buf, false);
        } else if (i < (_active_lines + 8)) {
            blanking(buf, true);
        } else {
            blanking(buf, false);
        }
    }

    if (_line_counter == _line_count) {
        _line_counter = 0;
        _frame_counter++;
    }
}
