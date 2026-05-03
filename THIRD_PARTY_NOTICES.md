# Third-party notices

`esp_8_bit_for_narya` is licensed as a whole under **GNU General Public
License, version 3 or later** (see `LICENSE`). The repository bundles
or fetches code from the upstream projects listed below; each item is
either originally GPL-compatible or has been relicensed via an
explicit upstream provision.

The vendored source files retain their original copyright headers in
addition to falling under this project's GPLv3 umbrella.

---

## Vendored sources

### Nofrendo NES emulator core

- **Files**: `core/main/emu/nofrendo/**`
- **Origin**: Nofrendo, by Matthew Conte and contributors (1998-2000),
  as included in the esp_8_bit upstream tree.
- **Original license**: GNU Library General Public License, version 2
  (LGPL v2.0).
- **Compatibility with GPLv3**: LGPL v2.0 section 3 explicitly allows
  re-licensing the covered files under "the ordinary GNU General Public
  License, version 2, or, at your option, any later version". This
  project exercises that option and re-licenses the bundled Nofrendo
  files under GPL version 3 or later.
- **Modifications by this project**: see git history; in particular,
  `map004.c` has been patched for MMC3 IRQ auto-reload behaviour.

### esp_8_bit (video pipeline, emulator glue)

- **Files**: `core/main/video/video_out.{h,cpp}`,
  `core/main/emu/emu.{h,cpp}`, `core/main/emu/emu_nofrendo.cpp`
  (porting derivatives only; the original esp_8_bit `media.h` ROM blobs
  are not included).
- **Origin**: esp_8_bit, by Peter Barrett (2020).
- **Original license**: ISC-style permissive license (see file headers
  in the corresponding sources).
- **Compatibility with GPLv3**: ISC is a permissive license; works
  derived from it can be redistributed under GPLv3 as long as the
  original copyright notice and license text are preserved, which we
  do at the top of each ported file.

### Adafruit GFX bitmap font

- **Files**: `core/main/menu/glcdfont.h`
- **Origin**: Adafruit_GFX library, by Adafruit Industries (2012).
- **Original license**: BSD 3-Clause License (see file header).
- **Compatibility with GPLv3**: BSD 3-Clause is a permissive license;
  works derived from it can be redistributed under GPLv3 as long as the
  original copyright notice and license text are preserved, which we
  do at the top of `glcdfont.h`.

---

## Build-time managed components

The following components are pulled at build time by the ESP-IDF
component manager and linked into the firmware binary; their sources
are not committed to this repository.

### espressif/usb_host_hid

- **Source**: ESP-IDF Component Registry,
  `espressif/usb_host_hid >= 1.0.0`.
- **License**: Apache License, Version 2.0.
- **Compatibility with GPLv3**: Apache 2.0 is GPLv3-compatible (the
  Free Software Foundation explicitly considers them compatible).

### joltwallet/littlefs (no longer used)

Earlier revisions of this project pulled `joltwallet/littlefs` to host
ROMs in a LittleFS partition; that dependency was removed when ROM
storage moved to a custom raw partition served via
`esp_partition_mmap` (see `core/main/rom_store/`).

---

## Reference projects (no code copied)

The following projects influenced the architecture of this port but no
source files were copied from them. They are mentioned for credit only.

- **fmruby-graphics-audio** (GPL v3) — informed the I2S audio pipeline
  shape (`core/main/audio/audio_i2s.cpp` is an independent rewrite).
- **fmruby-core** (GPL v3) — informed the open-drain reset GPIO scheme
  for the inter-MCU coordination, and the USB-HOST + HID host class
  driver pattern (`hid/main/usb/usb_gamepad.c` is an independent
  rewrite that targets a single HID gamepad layout instead of the full
  multi-device matrix in fmruby-core).

---

## NES ROM files

The repository's `core/data/nofrendo/` directory holds developer-side
NES ROM files at build time but they are excluded from version control
(see the `*.nes` entry in `.gitignore`). Distribute only ROMs you have
rights to.
