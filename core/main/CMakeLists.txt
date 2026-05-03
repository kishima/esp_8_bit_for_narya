# Glob Anemoia .cpp sources (core + mappers).
file(GLOB ANEMOIA_CORE_SRCS    "emu/anemoia/*.cpp")
file(GLOB ANEMOIA_MAPPER_SRCS  "emu/anemoia/mappers/*.cpp")

idf_component_register(
    SRCS
        "main.cpp"
        "video/video_out.cpp"
        "audio/audio_i2s.cpp"
        "emu/emu.cpp"
        "emu/emu_anemoia.cpp"
        "menu/rom_menu.cpp"
        "rom_store/rom_store.c"
        "transport/hid_uart.c"
        ${ANEMOIA_CORE_SRCS}
        ${ANEMOIA_MAPPER_SRCS}
    INCLUDE_DIRS
        "include"
        "video"
        "audio"
        "emu"
        "emu/anemoia"
        "menu"
        "rom_store"
        "transport"
    REQUIRES
        driver
        esp_driver_i2s
        esp_partition
        esp_timer
        freertos
        spi_flash
)

# Anemoia upstream is plain modern C++ but uses a few patterns that trip
# stricter ESP-IDF defaults. Soften only the warnings that fire, leave
# real diagnostics intact.
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wno-error=unused-but-set-variable
    -Wno-error=unused-variable
    -Wno-error=unused-function
    -Wno-error=missing-field-initializers
    -Wno-error=uninitialized
    -Wno-error=maybe-uninitialized
    -Wno-error=sequence-point
)

# Anemoia's CPU/PPU/APU interpreters expect aggressive optimisation
# (-Ofast + jump-table dispatch + loop unrolling) to hit real-time speed
# on the ESP32; the upstream ino has a #error guard for this. Apply the
# subset of flags that work with ESP-IDF's compile pipeline. Skip -flto
# (link-time, needs broader sdkconfig changes) and -mlongcalls (already
# part of the ESP-IDF default toolchain).
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Ofast
    -funroll-loops
    -fjump-tables
    -ftree-switch-conversion
    -fno-tree-vectorize
    -frename-registers
    -fno-plt
)
target_compile_definitions(${COMPONENT_LIB} PRIVATE OPTIMIZATION_FLAGS=1)
