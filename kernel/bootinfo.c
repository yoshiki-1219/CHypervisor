#include "bootinfo.h"
#include "common.h"

/*
 * UEFI から受け取るメモリマップを「そのままのレイアウト」で保持するため、
 * BOOT_INFO / MEMORY_MAP / Memory_map_Descriptors[] を .bss に確保する。
 */

/* スナップショット用に確保する最大バイト数 */
#define BOOTINFO_MM_BUF_BYTES (16 * 1024)

static struct {
    BOOT_INFO Boot_info;

    /* UEFI descriptors をコピー保持するためのバッファ */
    unsigned char Memory_map_Descriptors[BOOTINFO_MM_BUF_BYTES];

    int ready;
} g_boot_snapshot;


/* ---- 公開 API ---- */

int bootinfo_snapshot_init(const BOOT_INFO* src_bi)
{
    if (!src_bi) return -1;

    const MEMORY_MAP* src_mm = &src_bi->memory_map;
    if (!src_mm->descriptors || src_mm->map_size == 0) return -2;

    if (src_mm->map_size > (uint64_t)BOOTINFO_MM_BUF_BYTES) {
        return -3;
    }

    /* descriptors コピー */
    memcpy(g_boot_snapshot.Memory_map_Descriptors,
           src_mm->descriptors,
           (size_t)src_mm->map_size);

    /* Boot_info 全体をコピー */
    g_boot_snapshot.Boot_info = *src_bi;

    /* memory_map の descriptors を内部バッファに差し替え */
    g_boot_snapshot.Boot_info.memory_map.descriptors =
        (void*)g_boot_snapshot.Memory_map_Descriptors;
    g_boot_snapshot.Boot_info.memory_map.buffer_size = BOOTINFO_MM_BUF_BYTES;

    g_boot_snapshot.ready = 1;
    return 0;
}

const BOOT_INFO* bootinfo_snapshot(void)
{
    return g_boot_snapshot.ready ? &g_boot_snapshot.Boot_info : NULL;
}

const MEMORY_MAP* bootinfo_snapshot_memmap(void)
{
    return g_boot_snapshot.ready ? &g_boot_snapshot.Boot_info.memory_map : NULL;
}

const GUEST_INFO* bootinfo_snapshot_guestinfo(void)
{
    return g_boot_snapshot.ready ? &g_boot_snapshot.Boot_info.guest_info : NULL;
}
