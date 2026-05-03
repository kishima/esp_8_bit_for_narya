// Emu base class implementation for the Narya port.
//
// Origin: derived from esp_8_bit/src/emu.cpp (Peter Barrett, 2020).
// Narya port changes:
//   - Removed the CrapFS-on-app1 partition cache and the LittleFS+heap
//     code path that briefly replaced it. ROMs now live in a dedicated
//     'roms' raw partition and are surfaced through esp_partition_mmap
//     (see rom_store.{c,h}). The flash XIP virtual addresses returned
//     this way avoid the PSRAM cache thrashing that was breaking MMC3
//     PPU timing in earlier revisions.
//   - Path arguments are now treated as ROM names (the trailing path
//     component is looked up in the rom_store directory).

#include "emu.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"

#include "rom_store.h"

using namespace std;

static const char *TAG = "emu";

// Strip any "/dir/.../" prefix and return a pointer to the basename.
static const char *basename_of(const char *path)
{
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

// Single-ROM mmap session; the legacy emu API only loads one cart at a
// time, so we get away with a single global handle.
static esp_partition_mmap_handle_t s_active_handle;
static const uint8_t              *s_active_ptr;

uint8_t* map_file(const char* path, int /*len*/)
{
    // Tear down any previous mapping first.
    if (s_active_handle) {
        rom_store_munmap(s_active_handle);
        s_active_handle = 0;
        s_active_ptr    = nullptr;
    }

    const char *name = basename_of(path);
    uint32_t size = 0;
    s_active_ptr = rom_store_mmap(name, &s_active_handle, &size);
    if (!s_active_ptr) {
        ESP_LOGE(TAG, "map_file: '%s' not in rom_store", name);
        return nullptr;
    }
    ESP_LOGI(TAG, "map_file: %s -> %p (%lu bytes, flash xip)",
             name, s_active_ptr, (unsigned long)size);
    // The flash mmap pointer is read-only at the API level, but the rest
    // of the emulator treats the ROM as `uint8_t*`. Cast away const for
    // contract compatibility; nofrendo never writes through this pointer.
    return const_cast<uint8_t*>(s_active_ptr);
}

void unmap_file(uint8_t* /*ptr*/)
{
    if (s_active_handle) {
        rom_store_munmap(s_active_handle);
        s_active_handle = 0;
        s_active_ptr    = nullptr;
    }
}

FILE* mkfile(const char* path)
{
    // Writable filesystem support has been retired with the LittleFS
    // partition; nothing in the Narya port needs to call this.
    (void)path;
    return nullptr;
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

Emu::~Emu() = default;

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

// Read the first `len` bytes of a ROM. Returns the full ROM size, or -1
// on failure.
int Emu::head(const std::string& path, uint8_t* data, int len)
{
    const char *name = basename_of(path.c_str());
    int n = rom_store_count();
    for (int i = 0; i < n; ++i) {
        rom_store_entry_t e;
        if (rom_store_get(i, &e) != ESP_OK) continue;
        if (strcmp(e.name, name) != 0) continue;

        // Map enough to satisfy the read; munmap immediately - the caller
        // only needs a few bytes (e.g. the iNES header).
        esp_partition_mmap_handle_t h;
        uint32_t mapped_size = 0;
        const uint8_t *p = rom_store_mmap(e.name, &h, &mapped_size);
        if (!p) return -1;
        int copy = (int)mapped_size < len ? (int)mapped_size : len;
        memcpy(data, p, (size_t)copy);
        rom_store_munmap(h);
        return (int)e.size;
    }
    return -1;
}

int Emu::load(const std::string& path, uint8_t** data, int* len)
{
    *data = nullptr;
    *len  = 0;
    // load() is no longer used in the Narya port. ROMs are mmap'd via
    // map_file(); leave a stub so any vestigial call is loud rather than
    // silently allocating from PSRAM.
    ESP_LOGE(TAG, "Emu::load(%s) is unsupported in this port", path.c_str());
    return -1;
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
