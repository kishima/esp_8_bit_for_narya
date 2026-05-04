#include "mapper.h"
#include "cartridge.h"

// Narya port: bank "cache" is a no-op pass-through. The cartridge image
// is already mmap'd into flash XIP space, so getBank() just hands back
// a live pointer into it (or into the per-cart 8 KB CHR-RAM buffer when
// the ROM has no CHR-ROM). The Bank* / num_banks / tick fields stay in
// BankCache for source compatibility with upstream Anemoia mappers.

void bankInit(BankCache* cache, Bank* banks, uint8_t num_banks, uint32_t bank_size, Cartridge* cart)
{
    cache->banks     = banks;
    cache->num_banks = num_banks;
    cache->tick      = 0;
    cache->cart      = cart;
    cache->bank_size = bank_size;
}

IRAM_ATTR uint8_t* getBank(BankCache* cache, uint8_t bank_id, Mapper::ROM_TYPE rom)
{
    uint32_t offset = (uint32_t)bank_id * cache->bank_size;
    if (rom == Mapper::ROM_TYPE::PRG_ROM) return cache->cart->prgRomPtr(offset);
    if (rom == Mapper::ROM_TYPE::CHR_ROM) return cache->cart->chrRomPtr(offset);
    return nullptr;
}

uint8_t getBankIndex(BankCache* cache, uint8_t* ptr)
{
    // Some mappers reverse-map a live bank pointer back to its bank id
    // (e.g. for mirror selection). With the bypass we know
    //   ptr = base + bank_id * bank_size
    // so reconstruct the index by subtracting the base.
    if (!cache || !ptr || cache->bank_size == 0) return 0;
    uint8_t* base_prg = cache->cart->prgRomPtr(0);
    uint8_t* base_chr = cache->cart->chrRomPtr(0);
    if (base_prg && ptr >= base_prg) {
        size_t delta = (size_t)(ptr - base_prg);
        if ((delta % cache->bank_size) == 0) return (uint8_t)(delta / cache->bank_size);
    }
    if (base_chr && ptr >= base_chr) {
        size_t delta = (size_t)(ptr - base_chr);
        if ((delta % cache->bank_size) == 0) return (uint8_t)(delta / cache->bank_size);
    }
    return 0;
}

void invalidateCache(BankCache* /*cache*/)
{
    // No-op: nothing to invalidate when there is no cache.
}
