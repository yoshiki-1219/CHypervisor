#include "bootinfo.h"
#include <string.h>

/*
 * UEFI から受け取るメモリマップを「そのままのレイアウト」で保持するため、
 * BOOT_INFO / MEMORY_MAP / Memory_map_Descriptors[] を .bss に確保する。
 *
 * 注意：
 *  - descriptor_size は UEFI のバージョンで異なり得るため、バイト列として格納する。
 *  - “十分な数”はバイト数で確保しておくのが安全。
 */

/* スナップショット用に確保する最大バイト数：必要なら増やしてOK */
#ifndef BOOTINFO_MM_BUF_BYTES
#  define BOOTINFO_MM_BUF_BYTES (16 * 1024)   /* 64KiB */
#endif

/* ここに「全く同じレイアウト」を揃える */
static struct {
    /* Boot_info 相当（呼び出し側へ返すビュー） */
    BOOT_INFO Boot_info;

    /* Memory_map 相当（Boot_info.memory_map がこれを指す） */
    MEMORY_MAP Memory_map;

    /* Memory_map_Descriptors 相当（生バイト配列。UEFIのdescriptor_sizeに依存しない） */
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
        /* バッファ不足。必要なら BOOTINFO_MM_BUF_BYTES を増やす。 */
        return -3;
    }

    /* Descriptors を .bss の固定配列へコピー */
    const uint8_t* src = (const uint8_t*)src_mm->descriptors;
    uint8_t* dst = (uint8_t*)g_boot_snapshot.Memory_map_Descriptors;
    size_t n = (size_t)src_mm->map_size;

    for (size_t i = 0; i < n; i++) {
        dst[i] = src[i];
    }

    /* Memory_map を同レイアウトで再構成（descriptors は内蔵配列を指すように書き換え） */
    g_boot_snapshot.Memory_map.buffer_size        = BOOTINFO_MM_BUF_BYTES;
    g_boot_snapshot.Memory_map.descriptors        = (void*)g_boot_snapshot.Memory_map_Descriptors;
    g_boot_snapshot.Memory_map.map_size           = src_mm->map_size;
    g_boot_snapshot.Memory_map.map_key            = src_mm->map_key;
    g_boot_snapshot.Memory_map.descriptor_size    = src_mm->descriptor_size;
    g_boot_snapshot.Memory_map.descriptor_version = src_mm->descriptor_version;

    /* Boot_info もコピーして、memory_map だけ上の固定ビューに差し替え */
    g_boot_snapshot.Boot_info = *src_bi;
    g_boot_snapshot.Boot_info.memory_map = g_boot_snapshot.Memory_map;

    g_boot_snapshot.ready = 1;
    return 0;
}

const BOOT_INFO* bootinfo_snapshot(void)
{
    return g_boot_snapshot.ready ? &g_boot_snapshot.Boot_info : NULL;
}

const MEMORY_MAP* bootinfo_snapshot_memmap(void)
{
    return g_boot_snapshot.ready ? &g_boot_snapshot.Memory_map : NULL;
}
