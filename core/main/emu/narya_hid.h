// Narya HID compatibility shim. Replaces the upstream hid_server.h that
// esp_8_bit's emu.h originally pulled in. We do not run a Bluetooth HID
// stack on the core MCU; HID input arrives over UART from the ESP32-S3
// (see hid_uart_proto.h). Wii / IR joystick mappings are dropped.
//
// The KEY_MOD_* and GENERIC_* macros that emu_nofrendo references are
// defined inside class Emu itself (see emu.h), so this shim only needs to
// exist so emu.h's #include line resolves.

#ifndef NARYA_HID_H
#define NARYA_HID_H

#endif // NARYA_HID_H
