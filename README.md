# esp_8_bit_for_narya

A two-MCU port of [esp_8_bit](https://github.com/rossumur/esp_8_bit)
(Peter Barrett) onto the **Narya** board: an ESP32-WROVER drives NTSC
video and I2S audio while running the Nofrendo NES core; an ESP32-S3
hosts a USB HID gamepad and forwards button events over UART. See
[doc/porting_plan.md](doc/porting_plan.md) for the implementation plan.

## Topology

```
USB Gamepad ─► ESP32-S3 (hid)
                ├─ Vbus enable
                ├─ EN reset (open-drain) ──► ESP32-WROVER EN
                └─ UART1 TX ──────────────► ESP32-WROVER UART1 RX
                                              │
                                  ESP32-WROVER (core)
                                  ├─ NTSC composite (DAC1 / GPIO25) ─► TV
                                  ├─ I2S (BCK/WS/DOUT = 32/33/27) ──► amp
                                  └─ Nofrendo emulator
```

Pin maps live in `core/main/include/narya_pin_assign.h` and
`hid/main/include/narya_pin_assign.h`.

## Layout

| Path | MCU | Role |
|------|-----|------|
| [core/](core/) | ESP32-WROVER | NTSC, I2S audio, ROM store, Nofrendo, UART RX |
| [hid/](hid/) | ESP32-S3 | USB-Host, HID decoder, UART TX, core reset GPIO |
| [doc/](doc/) | — | Design notes |

Both firmwares are pure ESP-IDF v5.x and built inside Docker.

## Build & flash

Prerequisite: Docker. The default toolchain image is
`ghcr.io/family-mruby/fmruby-esp32-build:latest`; override with
`DOCKER_IMAGE` if needed.

Place NES ROMs (any `*.nes`) under `core/data/nofrendo/`. They are
gitignored and concatenated into a custom raw `roms` partition at
build time.

```sh
rake build         # build core then hid
cd core && rake check-port && rake flash
cd hid  && rake check-port && rake flash
```

Use `rake monitor` (per subproject) for the serial console.

## Controls

The HID decoder targets PS-style HID gamepads without a report-ID byte
(D-pad on byte 2 hat, face buttons on byte 0, menu buttons on byte 1).

| Pad | NES |
|-----|-----|
| D-pad | D-pad |
| Cross / Circle | B / A |
| Share / Options | Select / Start |
| **Share + R2** | **Reset core (back to ROM picker)** |

The first 20 raw HID reports per device are logged in hex on the hid
serial console so a different layout can be added in
`hid/main/usb/usb_gamepad.c::decode_hori`.

## Compatibility

Compatibility tracks the underlying Nofrendo core, not this port:

- mapper 0 (NROM), mapper 7 (AxROM): work
- mapper 4 (MMC3) with **CHR-RAM**: works
- mapper 4 (MMC3) with **CHR-ROM**: do not render correctly (most
  freeze or stay blank). The mapper IRQ has been patched toward the
  standard FCEUX auto-reload pattern, but a deeper cycle-accuracy
  issue remains. See [`core/main/emu/nofrendo/map004.c`](core/main/emu/nofrendo/map004.c).
- mapper 1 (MMC1): partial; some titles never finish boot.

The task watchdog is set to log-only, so a stuck game keeps the
firmware alive. Press **Share + R2** on the pad to soft-reset the core
back to the ROM picker.

## Architecture highlights

- **NTSC out**: I2S0 + APLL + DMA + DAC1, ported from esp_8_bit with
  all Arduino API removed.
- **Audio**: 15 720 Hz / 16-bit stereo I2S; the blocking write paces
  emulator frames.
- **ROM store**: a custom raw partition (subtype 0x40) holds a
  4 KB directory plus 4 KB-aligned ROM blobs;
  `tools/build_roms.py` generates the image at build time and
  `esp_partition_mmap` hands the emulator a flash-XIP pointer at
  runtime (no PSRAM round-trip).
- **ROM picker**: pre-emulator menu rendered into a 256x240 NES
  framebuffer with the bundled 5x7 font; the buffer is freed before
  `Emu::insert` so Nofrendo's primary buffer can land in DRAM.
- **HID UART link**: 5-byte minimum frame (SOF / type / seq+len /
  payload / CRC8). Stateless, no ACK.
- **Coordinated reset**: the hid firmware drives an open-drain line
  into the core's EN pin, so a press of the hid reset button or the
  Share+R2 combo brings both MCUs back together.

## License

Licensed as a whole under **GPL v3 or later**; see [LICENSE](LICENSE).
Per-component attribution and compatibility reasoning are in
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

NES ROM copyrights are out of scope of this repository; bring your own.
