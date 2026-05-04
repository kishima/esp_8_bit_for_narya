#ifndef CARTRIDGE_H
#define CARTRIDGE_H

#include "narya_compat.h"
#include <vector>

#include "mapper.h"
#include "mappers/mapper000.h"
#include "mappers/mapper001.h"
#include "mappers/mapper002.h"
#include "mappers/mapper003.h"
#include "mappers/mapper004.h"
#include "mappers/mapper069.h"

class Bus;
class Cartridge
{
public:
    Cartridge(const char* filename);
    ~Cartridge();

    enum MIRROR : uint8_t
    {
        HORIZONTAL,
        VERTICAL,
        ONESCREEN_LOW,
        ONESCREEN_HIGH,
        HARDWARE
    };

    bool cpuRead(uint16_t addr, uint8_t& data);
    bool cpuWrite(uint16_t addr, uint8_t data);
    bool ppuRead(uint16_t addr, uint8_t& data);
    uint8_t* ppuReadPtr(uint16_t addr);
    bool ppuWrite(uint16_t addr, uint8_t data);
    void ppuScanline();
    void cpuCycle(int cycles);
    void reset();

    void loadPRGBank(uint8_t* bank, uint16_t size, uint32_t offset);
    void loadCHRBank(uint8_t* bank, uint16_t size, uint32_t offset);

    // Narya port: zero-copy bank access. Mappers used to malloc a 170 KB
    // bank cache and memcpy chunks out of it; with the ROM already
    // mmap'd into flash XIP space we can hand mappers the live pointer
    // directly. CHR-RAM ROMs (header.CHR_ROM_chunks == 0) get a single
    // 8 KB writable buffer instead, which all CHR bank lookups index.
    uint8_t* prgRomPtr(uint32_t offset);
    uint8_t* chrRomPtr(uint32_t offset);
    void setMirrorMode(MIRROR mirror);
    Cartridge::MIRROR getMirrorMode();
    void connectBus(Bus* n)
    {
        bus = n;
    }
    void IRQ();

    void dumpState(File& state);
    void loadState(File& state);
    bool isValid();

    uint8_t hardware_mirror;
    uint8_t mirror = HORIZONTAL;
    uint32_t CRC32 = ~0U;

private:
    Bus* bus = nullptr;
    bool is_valid = true;
    uint32_t prg_base;
    uint32_t chr_base;

    // Narya port: ROMs come from a flash-XIP mmap rather than an SD card.
    // rom_data points at the mapped image (header + PRG + CHR contiguous);
    // rom_size is the byte length so we can sanity-check offsets. chr_ram
    // is non-null only when the cartridge has no CHR-ROM (mapper-side
    // pattern table is RAM); allocated lazily on first chrRomPtr() call.
    const uint8_t* rom_data = nullptr;
    uint32_t       rom_size = 0;
    uint8_t*       chr_ram  = nullptr;
    Mapper mapper;
    uint8_t mapper_ID = 0;
    uint8_t number_PRG_banks = 0;
    uint8_t number_CHR_banks = 0;

    uint32_t crc32(const void* buf, size_t size, uint32_t seed = ~0U);
};

#endif