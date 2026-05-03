// Minimal USB-HOST gamepad driver for the Narya hid firmware.
//
// Brings up the ESP-IDF USB host stack, attaches the espressif/usb_host_hid
// class driver, and dispatches button-edge events to a callback. The decode
// path is deliberately small: the first 8 bits of a generic report layout
// are treated as buttons and the lower nibble of byte 4 is treated as a
// HAT switch (0..7 directions, 0xF center). This covers the vast majority
// of USB joypads / clones without per-VID/PID quirks.

#ifndef NARYA_USB_GAMEPAD_H
#define NARYA_USB_GAMEPAD_H

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Button index mapping aligns 1:1 with hid_uart_proto.h's NARYA_EVT_BTN_*
// payload byte. See doc/porting_plan.md.
//   0=up, 1=down, 2=left, 3=right
//   4=start, 5=select, 6=A, 7=B
typedef void (*usb_gamepad_btn_cb_t)(int btn_idx, int pressed, void *user);

// Bring up USB host + HID host. Spawns one background task that pumps both
// stacks and one foreground listener that dispatches HID input reports.
// `cb` may be NULL during P5 bring-up; in that case events are only logged.
esp_err_t usb_gamepad_init(usb_gamepad_btn_cb_t cb, void *user);

#ifdef __cplusplus
}
#endif

#endif // NARYA_USB_GAMEPAD_H
