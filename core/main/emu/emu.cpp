// Emu base class implementation for the Narya port.
//
// Origin: derived from esp_8_bit/src/emu.cpp (Peter Barrett, 2020).
// Narya port changes:
//   - Removed the CrapFS-on-app1 partition cache (esp_partition_mmap +
//     manual directory). ROMs now live on a LittleFS data partition and are
//     loaded into PSRAM via stdio.
//   - Removed runtime miniz unpack(); ROMs are pre-baked into the LittleFS
//     image at build time, no runtime decompression needed.
//   - Dropped MALLOC32 / Atari/SMS factories (only Nofrendo is built here).

#include "emu.h"

#include <stdio.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_memory_utils.h"

using namespace std;

static const char *TAG = "emu";

uint8_t* map_file(const char* path, int len)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        ESP_LOGE(TAG, "map_file: cannot open %s", path);
        return nullptr;
    }

    // Prefer PSRAM for ROM body; fall back to internal heap if PSRAM not present.
    uint8_t *data = (uint8_t*)heap_caps_malloc(len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!data) {
        ESP_LOGW(TAG, "map_file: PSRAM alloc %d failed; trying internal heap", len);
        data = (uint8_t*)heap_caps_malloc(len, MALLOC_CAP_8BIT);
    }
    if (!data) {
        ESP_LOGE(TAG, "map_file: alloc %d failed", len);
        fclose(f);
        return nullptr;
    }

    size_t got = fread(data, 1, (size_t)len, f);
    fclose(f);
    if ((int)got != len) {
        ESP_LOGE(TAG, "map_file: short read %u/%d for %s", (unsigned)got, len, path);
        heap_caps_free(data);
        return nullptr;
    }
    ESP_LOGI(TAG, "map_file: %s -> %p (%d bytes, psram=%d)",
             path, data, len,
             (int)heap_caps_get_allocated_size(data) > 0 &&
                 esp_ptr_external_ram(data));
    return data;
}

void unmap_file(uint8_t* ptr)
{
    if (ptr) heap_caps_free(ptr);
}

FILE* mkfile(const char* path)
{
    return fopen(path, "wb");
}

// Map one bit array to another (used by the generic gamepad mapper).
uint32_t generic_map(uint32_t bits, const uint32_t* m)
{
    uint32_t b = 0;
    for (int i = 0; i < 16; i++) {
        if ((0x8000 >> i) & bits)
            b |= m[i];
    }
    return b;
}

Emu::Emu(const char* n, int w, int h, int st, int aformat, int cc, int f) :
    name(n), width(w), height(h), standard(st),
    audio_format(aformat), cc_width(cc), flavor(f)
{
    audio_frequency      = standard == 1 ? 15720 : 15600;
    audio_frame_samples  = standard ? (audio_frequency << 16) / 60
                                    : (audio_frequency << 16) / 50;
    audio_fraction       = 0;
}

Emu::~Emu()
{
}

int Emu::frame_sample_count()
{
    int n = audio_frame_samples + audio_fraction;
    audio_fraction = n & 0xFFFF;
    return n >> 16;
}

const uint32_t* Emu::composite_palette()
{
    return standard ? ntsc_palette() : pal_palette();
}

int Emu::head(const std::string& path, uint8_t* data, int len)
{
    FILE *f = fopen(path.c_str(), "rb");
    if (!f)
        return -1;
    size_t got = fread(data, 1, len, f);
    (void)got;
    fseek(f, 0, SEEK_END);
    int flen = (int)ftell(f);
    fclose(f);
    return flen;
}

int Emu::load(const std::string& path, uint8_t** data, int* len)
{
    *data = nullptr;
    *len  = 0;
    FILE *f = fopen(path.c_str(), "rb");
    if (!f) {
        ESP_LOGE(TAG, "load failed for %s", path.c_str());
        return -1;
    }
    fseek(f, 0, SEEK_END);
    int fsize = (int)ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *d = (uint8_t*)heap_caps_malloc(fsize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!d) d = (uint8_t*)heap_caps_malloc(fsize, MALLOC_CAP_8BIT);
    if (!d) {
        ESP_LOGE(TAG, "load: %d-byte alloc failed for %s", fsize, path.c_str());
        fclose(f);
        return -1;
    }
    size_t got = fread(d, 1, fsize, f);
    fclose(f);
    if ((int)got != fsize) {
        heap_caps_free(d);
        return -1;
    }
    ESP_LOGI(TAG, "load %d bytes from %s", fsize, path.c_str());
    *data = d;
    *len  = fsize;
    return 0;
}

// Filename extension helper used by Emu subclasses.
std::string get_ext(const std::string& s)
{
    auto dot = s.find_last_of('.');
    if (dot == std::string::npos) return "";
    std::string e = s.substr(dot + 1);
    for (auto& c : e) c = (char)tolower((unsigned char)c);
    return e;
}
