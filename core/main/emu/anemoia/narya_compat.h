// Narya port glue for Anemoia-ESP32. Replaces Arduino-specific headers
// (Arduino.h / SD.h / SPI.h / TFT_eSPI.h / debug.h / config.h) with the
// minimum set of declarations the upstream sources need so we do not
// have to fork every .h / .cpp.

#ifndef NARYA_ANEMOIA_COMPAT_H
#define NARYA_ANEMOIA_COMPAT_H

#include <stddef.h>
#include <stdint.h>

#include "esp_attr.h"
#include "esp_log.h"

#ifdef __cplusplus
extern "C" {
#endif

// Logging macros. Anemoia gates its own debug output behind DEBUG; we
// always wire the public LOGF/LOG macros into ESP_LOGI so anything that
// remains in the source (e.g. mapper allocation messages) lands on the
// serial console.
#ifndef NARYA_ANEMOIA_TAG
#define NARYA_ANEMOIA_TAG "anemoia"
#endif

#define LOG(msg)             ESP_LOGI(NARYA_ANEMOIA_TAG, "%s", (msg))
#define LOGF(fmt, ...)       ESP_LOGI(NARYA_ANEMOIA_TAG, fmt, ##__VA_ARGS__)

// Match upstream Anemoia: every shipped board config defines FRAMESKIP, so
// the published 60 fps + mapper compatibility is achieved with this on.
#define FRAMESKIP

static inline void log_pin_config(void) { /* no-op for Narya */ }

// Hook called by Bus::renderImage with `count` scanlines worth of RGB565
// pixels (256 * count uint16_t values, lower 6 bits = NES palette index
// when the PPU is configured with the INDEX palette via narya_compat).
// Implemented in emu_anemoia.cpp: copies into the global _lines buffer
// that video_out.cpp's NTSC blit consumes.
void narya_anemoia_publish_scanlines(const uint16_t *src,
                                     int first_scanline,
                                     int count);

// Hook called by Apu2A03 to hand finished sample blocks off to the I2S
// pipeline. Receives signed 16-bit mono samples.
void narya_anemoia_publish_audio(const int16_t *samples, int count);

// Hook called by Bus::cpuRead/0x4016 to fetch the latched controller
// state. 8-bit mask matching the Anemoia CONTROLLER enum (A=bit0,
// B=bit1, Select=bit2, Start=bit3, Up/Down/Left/Right=bit4..7).
uint8_t narya_anemoia_read_controller(int port);

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
// ---------------------------------------------------------------------
// TFT_eSPI / SD stubs. The upstream code declares pointer-to-class types
// from these libraries; we only need the pointer types to compile, the
// underlying objects are never constructed when DISABLE_TFT / DISABLE_SD
// is in effect (state save/load is gated below).
// ---------------------------------------------------------------------
class TFT_eSPI {};
// File stub with no-op IO. Anemoia's dumpState / loadState chain calls
// state.read / state.write / state.print / state.close / operator bool;
// none of those paths fire in the Narya port (Bus::saveState/loadState
// are stubbed out) so the implementations just discard.
class File {
public:
    operator bool() const { return false; }
    size_t read(void *, size_t)              { return 0; }
    size_t read(uint8_t *, size_t)           { return 0; }
    size_t write(const void *, size_t)       { return 0; }
    size_t write(const uint8_t *, size_t)    { return 0; }
    size_t print(const char *)               { return 0; }
    bool   seek(uint32_t)                    { return false; }
    uint32_t position() const                { return 0; }
    void   close()                           {}
};
class SDClass {
public:
    bool exists(const char *) { return false; }
    bool mkdir(const char *)  { return false; }
    File open(const char *, const char * /*mode*/ = "r") { return File(); }
};
extern SDClass SD;
#define FILE_READ  "r"
#define FILE_WRITE "w"
#endif // __cplusplus

#endif // NARYA_ANEMOIA_COMPAT_H
