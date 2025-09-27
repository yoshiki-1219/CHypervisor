// kernel/linux_guest.c
#include "linux_guest.h"
#include "common.h"
#include "log.h"
#include "bin_alloc.h"

size_t get_protected_code_offset(const SetupHeader *hdr)
{
    return ((size_t)hdr->setup_sects + 1u) * (size_t)SECTOR_SIZE;
}

int linux_bootparams_addE820entry(BootParams *bp, uint64_t addr, uint64_t size, E820Type type)
{
    if (!bp) return -1;
    if (size == 0) return 0; // 無視
    if (bp->e820_entries >= E820_MAX) return -2;

    E820Entry *e = &bp->e820_map[bp->e820_entries++];
    e->addr = addr;
    e->size = size;
    e->type = type;
    return 0;
}