#ifndef MAPPER_H
#define MAPPER_H

#include "narya_compat.h"
#include <cstring>
#include <stdint.h>
#include <stdlib.h>

class Cartridge;
class Mapper
{
public:
    enum ROM_TYPE : uint8_t
    {
        PRG_ROM,
        CHR_ROM
    };

    void* state = nullptr;
};

inline void mapperNoScanline(Mapper*)
{
}
inline void mapperNoCycle(Mapper*, int)
{
}

// Narya port: the upstream Bank/BankCache pair was a software cache that
// memcpy'd 170+ KB of PRG/CHR data out of an SD card into internal DRAM.
// We mmap the entire ROM into flash XIP space at boot, so the cache is
// pure overhead. Bank stays as a placeholder for the per-mapper Bank
// arrays (kept zero-cost for source compatibility with upstream); the
// real lookup happens through Cartridge::prgRomPtr / chrRomPtr.
struct Bank
{
    uint8_t  bank_id;
    uint8_t* bank_ptr;
    uint32_t last_used;
    uint32_t size;
};

struct BankCache
{
    Bank*      banks;       // unused, retained for upstream layout
    uint8_t    num_banks;   // unused
    uint32_t   tick;        // unused
    Cartridge* cart;
    uint32_t   bank_size;   // size each getBank() call resolves
};

void bankInit(BankCache* cache, Bank* banks, uint8_t num_banks, uint32_t bank_size,
              Cartridge* cart);
uint8_t* getBank(BankCache* cache, uint8_t bank_id, Mapper::ROM_TYPE rom);
uint8_t getBankIndex(BankCache* cache, uint8_t* ptr);
void invalidateCache(BankCache* cache);

#endif