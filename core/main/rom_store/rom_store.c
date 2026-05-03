// Read-only ROM store backed by the 'roms' raw flash partition.

#include "rom_store.h"

#include <string.h>

#include "esp_log.h"

#define TAG "rom_store"

#define ROMS_PARTITION_NAME    "roms"
#define ROMS_PARTITION_SUBTYPE 0x40
#define MAGIC                  "NRYAROMS"
#define MAX_ENTRIES            127     // matches build_roms.py

#pragma pack(push, 1)
typedef struct {
    char     magic[8];
    uint32_t version;
    uint32_t count;
} rom_store_header_t;

typedef struct {
    char     name[ROM_STORE_NAME_MAX];
    uint32_t offset;
    uint32_t size;
} rom_store_disk_entry_t;
#pragma pack(pop)

static const esp_partition_t  *s_part;
static rom_store_entry_t       s_entries[MAX_ENTRIES];
static int                     s_count;
static bool                    s_inited;

esp_err_t rom_store_init(void)
{
    if (s_inited) return ESP_OK;

    s_part = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA,
        (esp_partition_subtype_t)ROMS_PARTITION_SUBTYPE,
        ROMS_PARTITION_NAME);
    if (!s_part) {
        ESP_LOGE(TAG, "partition '%s' (subtype 0x%02X) not found",
                 ROMS_PARTITION_NAME, ROMS_PARTITION_SUBTYPE);
        return ESP_ERR_NOT_FOUND;
    }

    rom_store_header_t hdr;
    esp_err_t err = esp_partition_read(s_part, 0, &hdr, sizeof(hdr));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "header read failed: %s", esp_err_to_name(err));
        return err;
    }
    if (memcmp(hdr.magic, MAGIC, sizeof(hdr.magic)) != 0) {
        ESP_LOGE(TAG, "bad magic in roms partition");
        return ESP_ERR_INVALID_STATE;
    }
    if (hdr.version != 1) {
        ESP_LOGE(TAG, "unsupported version %lu", (unsigned long)hdr.version);
        return ESP_ERR_INVALID_VERSION;
    }
    if (hdr.count > MAX_ENTRIES) {
        ESP_LOGE(TAG, "too many entries: %lu", (unsigned long)hdr.count);
        return ESP_ERR_INVALID_SIZE;
    }

    s_count = (int)hdr.count;
    for (int i = 0; i < s_count; ++i) {
        rom_store_disk_entry_t de;
        err = esp_partition_read(s_part, 0x10 + i * sizeof(de), &de, sizeof(de));
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "entry %d read failed: %s", i, esp_err_to_name(err));
            return err;
        }
        memcpy(s_entries[i].name, de.name, ROM_STORE_NAME_MAX);
        s_entries[i].name[ROM_STORE_NAME_MAX - 1] = '\0';
        s_entries[i].offset = de.offset;
        s_entries[i].size   = de.size;
    }

    ESP_LOGI(TAG, "found %d ROM(s) in '%s' partition", s_count, ROMS_PARTITION_NAME);
    s_inited = true;
    return ESP_OK;
}

int rom_store_count(void) { return s_count; }

esp_err_t rom_store_get(int index, rom_store_entry_t *out)
{
    if (!out || index < 0 || index >= s_count) return ESP_ERR_INVALID_ARG;
    *out = s_entries[index];
    return ESP_OK;
}

const uint8_t *rom_store_mmap(const char *name,
                              esp_partition_mmap_handle_t *handle_out,
                              uint32_t *size_out)
{
    if (!name || !handle_out) return NULL;
    if (!s_inited) return NULL;

    for (int i = 0; i < s_count; ++i) {
        if (strncmp(s_entries[i].name, name, ROM_STORE_NAME_MAX) == 0) {
            const void *mapped = NULL;
            esp_err_t err = esp_partition_mmap(
                s_part,
                s_entries[i].offset,
                s_entries[i].size,
                ESP_PARTITION_MMAP_DATA,
                &mapped,
                handle_out);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "mmap '%s' (off=0x%lX size=%lu) failed: %s",
                         name,
                         (unsigned long)s_entries[i].offset,
                         (unsigned long)s_entries[i].size,
                         esp_err_to_name(err));
                return NULL;
            }
            if (size_out) *size_out = s_entries[i].size;
            ESP_LOGI(TAG, "mapped '%s' -> %p (%lu bytes)", name, mapped,
                     (unsigned long)s_entries[i].size);
            return (const uint8_t *)mapped;
        }
    }
    ESP_LOGE(TAG, "mmap: rom '%s' not found", name);
    return NULL;
}

void rom_store_munmap(esp_partition_mmap_handle_t handle)
{
    if (handle) esp_partition_munmap(handle);
}
