// Read-only ROM store backed by the dedicated 'roms' raw flash partition.
// The partition is laid out by tools/build_roms.py at build time:
//   - 4 KB directory header (magic, version, count, entries)
//   - 4 KB-aligned ROM blobs
// At runtime we parse the directory once at boot, then hand back flash
// XIP virtual addresses through esp_partition_mmap so the emulator can
// read ROM bytes through the flash cache (avoiding the PSRAM round-trip
// that hurt MMC3 PPU timing in earlier revisions of the port).

#ifndef NARYA_ROM_STORE_H
#define NARYA_ROM_STORE_H

#include <stdint.h>
#include <stddef.h>

#include "esp_err.h"
#include "esp_partition.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ROM_STORE_NAME_MAX 24

typedef struct {
    char     name[ROM_STORE_NAME_MAX];
    uint32_t offset;
    uint32_t size;
} rom_store_entry_t;

// Initialize the store: locate the 'roms' partition, validate the header,
// load directory entries into memory. Idempotent.
esp_err_t rom_store_init(void);

// Number of valid ROM entries discovered. 0 if the partition is missing,
// has the wrong magic, or holds zero ROMs.
int rom_store_count(void);

// Copy the i-th entry into *out. Returns ESP_OK or ESP_ERR_INVALID_ARG.
esp_err_t rom_store_get(int index, rom_store_entry_t *out);

// Map the named ROM into the CPU address space using esp_partition_mmap.
// Returns a pointer into the flash XIP region on success and writes the
// mmap handle (needed for unmap) into *handle_out. Returns NULL on error.
const uint8_t *rom_store_mmap(const char *name,
                              esp_partition_mmap_handle_t *handle_out,
                              uint32_t *size_out);

// Release a previous mapping. Safe to call with handle == 0.
void rom_store_munmap(esp_partition_mmap_handle_t handle);

#ifdef __cplusplus
}
#endif

#endif // NARYA_ROM_STORE_H
