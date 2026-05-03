// EmuNofrendo glue for the Narya port.
//
// Origin: derived from esp_8_bit/src/emu_nofrendo.cpp (Peter Barrett, 2020).
// Narya port changes:
//   - Removed media.h dependency and make_default_media(); ROMs come from
//     LittleFS instead of compiled-in compressed blobs.
//   - Replaced the Wii/IR HID mapping inside hid() with a slim event_btn()
//     entry-point that takes button indices 0..7 directly. UART HID frames
//     from the ESP32-S3 will drive this in a later phase.
//   - Dropped the Atari800 / SMS palette generators that were keyed by the
//     same _machine variable; only EMU_NES is exercised.

#include "emu.h"
#include <math.h>
#include <string>

extern "C" {
#include "nofrendo/osd.h"
#include "nofrendo/event.h"
};

using namespace std;

// 3-phase NTSC encoding (samples per color clock = 3).
uint32_t nes_3_phase[64] = {
    0x2C2C2C00,0x241D2400,0x221D2600,0x1F1F2700,0x1D222600,0x1D242400,0x1D262200,0x1F271F00,
    0x22261D00,0x24241D00,0x26221D00,0x271F1F00,0x261D2200,0x14141400,0x14141400,0x14141400,
    0x38383800,0x2C252C00,0x2A252E00,0x27272F00,0x252A2E00,0x252C2C00,0x252E2A00,0x272F2700,
    0x2A2E2500,0x2C2C2500,0x2E2A2500,0x2F272700,0x2E252A00,0x1F1F1F00,0x15151500,0x15151500,
    0x45454500,0x3A323A00,0x37333C00,0x35353C00,0x33373C00,0x323A3A00,0x333C3700,0x353C3500,
    0x373C3300,0x3A3A3200,0x3C373300,0x3C353500,0x3C333700,0x2B2B2B00,0x16161600,0x16161600,
    0x45454500,0x423B4200,0x403B4400,0x3D3D4500,0x3B404400,0x3B424200,0x3B444000,0x3D453D00,
    0x40443B00,0x42423B00,0x44403B00,0x453D3D00,0x443B4000,0x39393900,0x17171700,0x17171700,
};

// 4-phase NTSC encoding (samples per color clock = 4).
uint32_t nes_4_phase[64] = {
    0x2C2C2C2C,0x241D1F26,0x221D2227,0x1F1D2426,0x1D1F2624,0x1D222722,0x1D24261F,0x1F26241D,
    0x2227221D,0x24261F1D,0x26241D1F,0x27221D22,0x261F1D24,0x14141414,0x14141414,0x14141414,
    0x38383838,0x2C25272E,0x2A252A2F,0x27252C2E,0x25272E2C,0x252A2F2A,0x252C2E27,0x272E2C25,
    0x2A2F2A25,0x2C2E2725,0x2E2C2527,0x2F2A252A,0x2E27252C,0x1F1F1F1F,0x15151515,0x15151515,
    0x45454545,0x3A33353C,0x3732373C,0x35333A3C,0x33353C3A,0x32373C37,0x333A3C35,0x353C3A33,
    0x373C3732,0x3A3C3533,0x3C3A3335,0x3C373237,0x3C35333A,0x2B2B2B2B,0x16161616,0x16161616,
    0x45454545,0x423B3D44,0x403B4045,0x3D3B4244,0x3B3D4442,0x3B404540,0x3B42443D,0x3D44423B,
    0x4045403B,0x42443D3B,0x44423B3D,0x45403B40,0x443D3B42,0x39393939,0x17171717,0x17171717,
};

// PAL yuyv table (kept for completeness; PAL is not exercised in the Narya port).
uint32_t _nes_yuv_4_phase_pal[] = {
    0x31313131,0x2D21202B,0x2720252D,0x21212B2C,0x1D23302A,0x1B263127,0x1C293023,0x202B2D22,
    0x262B2722,0x2C2B2122,0x2F2B1E23,0x31291F27,0x30251F2A,0x18181818,0x19191919,0x19191919,
    0x3D3D3D3D,0x34292833,0x2F282D34,0x29283334,0x252B3732,0x232E392E,0x2431382B,0x28333429,
    0x2D342F28,0x33342928,0x3732252A,0x392E232E,0x382B2431,0x24242424,0x1A1A1A1A,0x1A1A1A1A,
    0x49494949,0x42373540,0x3C373B40,0x36374040,0x3337433F,0x3139433B,0x323D4338,0x35414237,
    0x3B423D35,0x41413736,0x453F3238,0x473C313B,0x4639323F,0x2F2F2F2F,0x1A1A1A1A,0x1A1A1A1A,
    0x49494949,0x48413D45,0x42404345,0x3D3F4644,0x3B3D4543,0x3B3E4542,0x3B42453F,0x3E47463E,
    0x434A453E,0x46483E3D,0x4843393E,0x4A403842,0x4B403944,0x3E3E3E3E,0x1B1B1B1B,0x1B1B1B1B,
    // odd field
    0x31313131,0x20212D2B,0x2520272D,0x2B21212C,0x30231D2A,0x31261B27,0x30291C23,0x2D2B2022,
    0x272B2622,0x212B2C22,0x1E2B2F23,0x1F293127,0x1F25302A,0x18181818,0x19191919,0x19191919,
    0x3D3D3D3D,0x28293433,0x2D282F34,0x33282934,0x372B2532,0x392E232E,0x3831242B,0x34332829,
    0x2F342D28,0x29343328,0x2532372A,0x232E392E,0x242B3831,0x24242424,0x1A1A1A1A,0x1A1A1A1A,
    0x49494949,0x35374240,0x3B373C40,0x40373640,0x4337333F,0x4339313B,0x433D3238,0x42413537,
    0x3D423B35,0x37414136,0x323F4538,0x313C473B,0x3239463F,0x2F2F2F2F,0x1A1A1A1A,0x1A1A1A1A,
    0x49494949,0x3D414845,0x43404245,0x463F3D44,0x453D3B43,0x453E3B42,0x45423B3F,0x46473E3E,
    0x454A433E,0x3E48463D,0x3943483E,0x38404A42,0x39404B44,0x3E3E3E3E,0x1B1B1B1B,0x1B1B1B1B,
};

// Active ROM image (mapped via map_file). Read by nofrendo's osd_getromdata.
uint8_t* _nofrendo_rom = nullptr;
extern "C" char* osd_getromdata()
{
    return (char*)_nofrendo_rom;
}

// Forward decls into the C nofrendo subsystem.
extern "C" int nes_emulate_init(const char* path, int width, int height);
extern "C" uint8_t** nes_emulate_frame(bool draw_flag);

// Audio: nofrendo registers a "play" callback via osd_setsound which produces
// 8-bit unsigned samples; we convert to signed-16-bit in audio_buffer().
static void (*nes_sound_cb)(void *buffer, int length) = nullptr;
extern uint32_t nes_pal[256];

int _audio_frequency;
extern "C" void osd_getsoundinfo(sndinfo_t *info)
{
    info->sample_rate = _audio_frequency;
    info->bps         = 8;
}

extern "C" void osd_setsound(void (*playfunc)(void *buffer, int length))
{
    nes_sound_cb = playfunc;
}

static const char* _nes_ext[] = { "nes", nullptr };
static const char* _nes_help[] = {
    "Keyboard:",
    "  Arrow Keys - D-Pad",
    "  Left Shift - Button A",
    "  Option     - Button B",
    "  Return     - Start",
    "  Tab        - Select",
    nullptr
};

class EmuNofrendo : public Emu {
    uint8_t** _lines;
public:
    EmuNofrendo(int ntsc) : Emu("nofrendo", 256, 240, ntsc, (16 | (1 << 8)), 4, EMU_NES)
    {
        _lines = nullptr;
        _ext   = _nes_ext;
        _help  = _nes_help;
        _audio_frequency = audio_frequency;
    }

    virtual void gen_palettes() {
        // Tables above are already pre-baked. The upstream code uses this
        // method to regenerate palettes; in the Narya port they are static.
    }

    static void pad_event(int pressed, int index) {
        event_t e = event_get(index);
        if (e) e(pressed);
    }

    // Map UART HID button indices (0..7) and reset signals to nofrendo events.
    // Index legend (must match hid firmware emission):
    //   0=up, 1=down, 2=left, 3=right, 4=start, 5=select, 6=A, 7=B
    //   8=soft_reset, 9=hard_reset
    void event_btn(int btn_idx, int pressed) {
        static const int map[10] = {
            event_joypad1_up,
            event_joypad1_down,
            event_joypad1_left,
            event_joypad1_right,
            event_joypad1_start,
            event_joypad1_select,
            event_joypad1_a,
            event_joypad1_b,
            event_soft_reset,
            event_hard_reset,
        };
        if (btn_idx < 0 || btn_idx >= 10) return;
        pad_event(pressed, map[btn_idx]);
    }

    virtual int insert(const std::string& path, int flags = 1, int disk_index = 0) {
        unmap_file(_nofrendo_rom);
        _nofrendo_rom = nullptr;

        uint8_t h[16];
        int len = head(path, h, sizeof(h));
        if (len < 0) {
            printf("nofrendo: cannot open %s\n", path.c_str());
            return -1;
        }

        printf("nofrendo: %s is %d bytes\n", path.c_str(), len);
        _nofrendo_rom = map_file(path.c_str(), len);
        if (!_nofrendo_rom) {
            printf("nofrendo: cannot map %s\n", path.c_str());
            return -1;
        }

        nes_emulate_init(path.c_str(), width, height);
        _lines = nes_emulate_frame(true);   // first frame primes _lines
        return 0;
    }

    virtual int update() {
        if (_nofrendo_rom)
            _lines = nes_emulate_frame(true);
        return 0;
    }

    virtual uint8_t** video_buffer() { return _lines; }

    virtual int audio_buffer(int16_t* b, int len) {
        int n = frame_sample_count();
        if (n > len) n = len;
        if (nes_sound_cb) {
            nes_sound_cb(b, n);  // 8-bit unsigned, written into the buffer's lower bytes
            uint8_t* b8 = (uint8_t*)b;
            // Walk back-to-front so we do not overwrite source bytes.
            for (int i = n - 1; i >= 0; i--)
                b[i] = (b8[i] ^ 0x80) << 8;
        } else {
            memset(b, 0, sizeof(int16_t) * n);
        }
        return n;
    }

    virtual const uint32_t* ntsc_palette() override {
        return cc_width == 3 ? nes_3_phase : nes_4_phase;
    }
    virtual const uint32_t* pal_palette()  override { return _nes_yuv_4_phase_pal; }
    virtual const uint32_t* rgb_palette()  override { return nes_pal; }

    // Narya port: ROM bring-up does not auto-populate the LittleFS partition.
    // ROMs are baked at build time via littlefs_create_partition_image.
    virtual int make_default_media(const std::string& /*path*/) override {
        return 0;
    }
};

Emu* NewNofrendo(int ntsc)
{
    return new EmuNofrendo(ntsc);
}

// Public C entry-point for the UART HID handler (avoids exposing C++ class).
extern "C" void nofrendo_event_btn(void* emu, int btn_idx, int pressed)
{
    if (!emu) return;
    static_cast<EmuNofrendo*>(emu)->event_btn(btn_idx, pressed);
}
