# Glob nofrendo .c sources (mappers + core).
file(GLOB NOFRENDO_SRCS "emu/nofrendo/*.c")

idf_component_register(
    SRCS
        "main.cpp"
        "video/video_out.cpp"
        "audio/audio_i2s.cpp"
        "emu/emu.cpp"
        "emu/emu_nofrendo.cpp"
        "menu/rom_menu.cpp"
        "transport/hid_uart.c"
        ${NOFRENDO_SRCS}
    INCLUDE_DIRS
        "include"
        "video"
        "audio"
        "emu"
        "emu/nofrendo"
        "menu"
        "transport"
    REQUIRES
        driver
        esp_driver_i2s
        esp_partition
        esp_timer
        freertos
        joltwallet__littlefs
)

# nofrendo upstream is older C and trips several modern GCC -Werror=* flags.
# Soften them to warnings without disabling the underlying checks; expose
# strdup() via _GNU_SOURCE for the few sources that use it.
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wno-error=char-subscripts
    -Wno-error=misleading-indentation
    -Wno-error=maybe-uninitialized
    -Wno-error=implicit-function-declaration
    -Wno-error=unused-but-set-variable
    -Wno-error=unused-variable
    -Wno-error=unused-function
)
target_compile_definitions(${COMPONENT_LIB} PRIVATE _GNU_SOURCE)
