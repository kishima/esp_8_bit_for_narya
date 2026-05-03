// ROM picker shown on the NES NTSC output before any emulator state exists.
// Scans LittleFS at /storage for *.nes, renders a list with a 5x7 ASCII font
// into a 256x240 NES-indexed framebuffer, and lets the user move the cursor
// with D-pad up/down and select with A. Selection is returned as the full
// LittleFS path of the chosen ROM.

#ifndef NARYA_ROM_MENU_H
#define NARYA_ROM_MENU_H

#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Block on the menu UI until the user selects a ROM (A or Start) or until
// `default_timeout_ms` elapses with no input (in which case the first ROM
// is auto-selected). On success writes the chosen ROM name (basename only,
// NUL-terminated) into `out_path`. Pass 0 for `default_timeout_ms` to
// wait forever. ROM enumeration comes from rom_store, so rom_store_init()
// must have already succeeded before calling this.
esp_err_t rom_menu_run(char *out_path, size_t out_cap,
                       uint32_t default_timeout_ms);

// Release the menu framebuffer + row-pointer array so the ~60 KB they
// occupy in internal DRAM can be reused by the emulator (Nofrendo's
// primary_buffer wants DRAM for ISR-safe access). After this call the
// global _lines is left at nullptr; the emulator task will repopulate
// it on its first frame. Safe to call multiple times.
void rom_menu_release(void);

#ifdef __cplusplus
}
#endif

#endif // NARYA_ROM_MENU_H
