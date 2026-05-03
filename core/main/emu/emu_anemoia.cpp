// EmuAnemoia glue: wraps the Anemoia-ESP32 NES core (vendored under
// emu/anemoia/) in our Emu interface, plus the implementations of the
// narya_anemoia_publish_* hooks declared in narya_compat.h.

#include "emu.h"

#include <math.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "anemoia/bus.h"
#include "anemoia/cartridge.h"
#include "anemoia/narya_compat.h"
#include "audio_i2s.h"

// Anemoia's controller bitmask enum lives outside the bundled core; it is
// just the GPIO controller types header. We only need the enum values.
enum class CONTROLLER : uint8_t {
    A      = (1u << 0),
    B      = (1u << 1),
    Select = (1u << 2),
    Start  = (1u << 3),
    Up     = (1u << 4),
    Down   = (1u << 5),
    Left   = (1u << 6),
    Right  = (1u << 7),
};

using namespace std;

static const char *TAG = "emu_anemoia";

// 4-phase NTSC YUV palette identical to the one used by emu_nofrendo so
// the existing video_out.cpp blit() works unchanged. Each entry encodes
// four 16-bit DAC samples per NES color clock.
extern "C" const uint32_t nes_4_phase[64] = {
    0x2C2C2C2C,0x241D1F26,0x221D2227,0x1F1D2426,0x1D1F2624,0x1D222722,0x1D24261F,0x1F26241D,
    0x2227221D,0x24261F1D,0x26241D1F,0x27221D22,0x261F1D24,0x14141414,0x14141414,0x14141414,
    0x38383838,0x2C25272E,0x2A252A2F,0x27252C2E,0x25272E2C,0x252A2F2A,0x252C2E27,0x272E2C25,
    0x2A2F2A25,0x2C2E2725,0x2E2C2527,0x2F2A252A,0x2E27252C,0x1F1F1F1F,0x15151515,0x15151515,
    0x45454545,0x3A33353C,0x3732373C,0x35333A3C,0x33353C3A,0x32373C37,0x333A3C35,0x353C3A33,
    0x373C3732,0x3A3C3533,0x3C3A3335,0x3C373237,0x3C35333A,0x2B2B2B2B,0x16161616,0x16161616,
    0x45454545,0x423B3D44,0x403B4045,0x3D3B4244,0x3B3D4442,0x3B404540,0x3B42443D,0x3D44423B,
    0x4045403B,0x42443D3B,0x44423B3D,0x45403B40,0x443D3B42,0x39393939,0x17171717,0x17171717,
};

// 240x256 indexed framebuffer the NTSC blit reads through _lines.
// IRAM-friendly internal DRAM so the video ISR can fetch from it without
// PSRAM cache contention or cache-disabled flash hazards.
static uint8_t   s_fb[240][256];
static uint8_t  *s_fb_lines[240];
static bool      s_fb_lines_inited = false;

static void ensure_fb_lines(void)
{
    if (s_fb_lines_inited) return;
    for (int i = 0; i < 240; ++i) s_fb_lines[i] = &s_fb[i][0];
    s_fb_lines_inited = true;
}

extern "C" IRAM_ATTR void narya_anemoia_publish_scanlines(const uint16_t *src,
                                                          int first_scanline,
                                                          int count)
{
    if (!src || count <= 0) return;
    if (first_scanline < 0 || first_scanline >= 240) return;
    if (first_scanline + count > 240) count = 240 - first_scanline;

    for (int row = 0; row < count; ++row) {
        const uint16_t *srow = src + (size_t)row * 256;
        uint8_t        *drow = s_fb[first_scanline + row];
        // Identity palette mode keeps the 6-bit NES index in the low byte.
        for (int x = 0; x < 256; ++x) drow[x] = (uint8_t)(srow[x] & 0x3F);
    }
}

extern "C" void narya_anemoia_publish_audio(const int16_t *samples, int count)
{
    if (!samples || count <= 0) return;
    // Anemoia hands us interleaved stereo (count = frames * 2), 16-bit but
    // unsigned (raw NES output bits, midpoint = 0x8000). Convert to mono
    // signed centered around 0 for our blocking I2S writer.
    static int16_t mono[NARYA_AUDIO_MAX_MONO_SAMPLES];
    int frames = count / 2;
    if (frames > NARYA_AUDIO_MAX_MONO_SAMPLES) frames = NARYA_AUDIO_MAX_MONO_SAMPLES;
    for (int i = 0; i < frames; ++i) {
        uint16_t u = (uint16_t)samples[i * 2];     // left channel
        mono[i]    = (int16_t)((int32_t)u - 0x8000);
    }
    audio_i2s_write_mono(mono, frames);
}

extern "C" uint8_t narya_anemoia_read_controller(int /*port*/)
{
    // Unused: the bus polls bus->controller directly, which we update from
    // EmuAnemoia::event_btn below. Keep the symbol so narya_compat.h links.
    return 0;
}

// ---------------------------------------------------------------------
// Emu subclass

class EmuAnemoia : public Emu {
public:
    EmuAnemoia(int ntsc)
        : Emu("anemoia", 256, 240, ntsc, (16 | (1 << 8)), 4, EMU_NES)
    {
        ensure_fb_lines();
        bus = new Bus();
    }

    ~EmuAnemoia() override
    {
        delete bus;
        delete cart;
    }

    void gen_palettes() override {}

    int insert(const std::string& path, int /*flags*/ = 1, int /*disk_index*/ = 0) override
    {
        if (cart) { delete cart; cart = nullptr; }
        cart = new Cartridge(path.c_str());
        if (!cart->isValid()) {
            ESP_LOGE(TAG, "cartridge not valid: %s", path.c_str());
            return -1;
        }
        bus->insertCartridge(cart);
        bus->ppu.setPalette(Ppu2C02::PALETTE_INDEX);
        bus->reset();

        // Anemoia ships its APU as a busy-loop background task that runs in
        // parallel with the CPU/PPU loop. Without it the audio buffer is
        // never filled, our publish_audio hook never fires, and emu_task
        // races with no back-pressure (-> IDLE0 starvation watchdog).
        // Pin to core 0 so the APU shares the same cache locality with
        // the rest of the emulator. Low priority lets emu_task on the
        // same core preempt at frame boundaries.
        if (!apu_task_handle) {
            xTaskCreatePinnedToCore(EmuAnemoia::apu_task_entry,
                                    "apu_task", 4 * 1024,
                                    &bus->cpu.apu, 1, &apu_task_handle, 0);
        }
        ESP_LOGI(TAG, "rom inserted: %s", path.c_str());
        return 0;
    }

    static void apu_task_entry(void *arg)
    {
        Apu2A03 *apu = (Apu2A03 *)arg;
        while (true) {
            apu->clock();
        }
    }

    int update() override
    {
        if (!cart || !cart->isValid()) return -1;
        bus->clock();   // one full NES frame; pushes audio + scanlines via the hooks
        return 0;
    }

    uint8_t** video_buffer() override
    {
        return s_fb_lines;
    }

    int audio_buffer(int16_t* /*b*/, int /*len*/) override
    {
        // Audio is already pushed to I2S inline from inside bus->clock().
        return 0;
    }

    const uint32_t* ntsc_palette() override { return nes_4_phase; }
    const uint32_t* pal_palette()  override { return nullptr; }
    const uint32_t* rgb_palette()  override { return nullptr; }

    int make_default_media(const std::string& /*path*/) override { return 0; }

    void event_btn(int btn_idx, int pressed)
    {
        // narya_hid btn_idx layout (matches hid_uart_proto.h):
        //   0..3 = up/down/left/right, 4=Start, 5=Select, 6=A, 7=B
        static const uint8_t map[8] = {
            (uint8_t)CONTROLLER::Up,
            (uint8_t)CONTROLLER::Down,
            (uint8_t)CONTROLLER::Left,
            (uint8_t)CONTROLLER::Right,
            (uint8_t)CONTROLLER::Start,
            (uint8_t)CONTROLLER::Select,
            (uint8_t)CONTROLLER::A,
            (uint8_t)CONTROLLER::B,
        };
        if (btn_idx < 0 || btn_idx >= 8) return;
        if (pressed) bus->controller |=  map[btn_idx];
        else         bus->controller &= ~map[btn_idx];
    }

private:
    Bus          *bus             = nullptr;
    Cartridge    *cart            = nullptr;
    TaskHandle_t  apu_task_handle = nullptr;
};

// Stand-in for the Nofrendo factory; the rest of the firmware constructs
// the emulator through this entry-point.
Emu* NewNofrendo(int ntsc)
{
    return new EmuAnemoia(ntsc);
}

// C entry-point for the UART HID forwarder in main.cpp.
extern "C" void nofrendo_event_btn(void *emu, int btn_idx, int pressed)
{
    if (!emu) return;
    static_cast<EmuAnemoia*>(emu)->event_btn(btn_idx, pressed);
}
