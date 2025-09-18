#include "page_alloc.h"
#include "memmap.h"
#include "bootinfo.h"
#include "arch/x86/paging.h"
#include <string.h>

/* ========== 設計方針 ==========
 * - 最大 128GiB の物理メモリを管理（必要に応じて増減可）
 * - 1bit = 1 フレーム（4KiB）
 * - 既定は「使用中」にしておき、UEFI の「使用可能」領域だけ unused に落とす
 * - frame_begin=1（フレーム0は予約）
 */

/* ====== 管理上限 ====== */
#define MAX_PHYS_SIZE   (128ULL << 30)            /* 128 GiB */
#define FRAME_SIZE      PAGE_SIZE                 /* 4 KiB */
#define FRAME_COUNT     (MAX_PHYS_SIZE / FRAME_SIZE)  /* 32Mi frames */

/* ====== ビットマップ（u64 配列） ====== */
typedef uint64_t mapline_t;
enum { BITS_PER_MAPLINE = (int)(sizeof(mapline_t) * 8) };
enum { MAPLINE_COUNT    = (int)(FRAME_COUNT / BITS_PER_MAPLINE) };

/* 4MiB の固定ビットマップ */
static mapline_t g_bitmap[MAPLINE_COUNT];

/* 管理範囲（フレーム番号） */
static uint64_t g_frame_begin = 1;   /* 0 は予約 */
static uint64_t g_frame_end   = 0;   /* 有効な終端（[begin, end)） */

/* この章の方針どおり：
 * - 使用可能: ConventionalMemory, BootServicesCode
 * - BootServicesData は「使用中」のまま（PT が載っている可能性があるため）
 */
static inline int is_usable_type(uint32_t t)
{
    return (t == EfiConventionalMemory) || (t == EfiBootServicesCode);
}

/* ====== 補助 ====== */
static inline uint64_t phys_to_frame(uint64_t phys) { return phys / FRAME_SIZE; }
static inline uint64_t frame_to_phys(uint64_t frm)  { return frm  * FRAME_SIZE; }

/* ビット操作 */
static inline int  bm_get(uint64_t frame)
{
    uint64_t li = frame / BITS_PER_MAPLINE;
    uint64_t bi = frame % BITS_PER_MAPLINE;
    return (g_bitmap[li] >> bi) & 1ULL;      /* 1 = used, 0 = unused */
}

static inline void bm_set_used(uint64_t frame)
{
    uint64_t li = frame / BITS_PER_MAPLINE;
    uint64_t bi = frame % BITS_PER_MAPLINE;
    g_bitmap[li] |= (1ULL << bi);
}

static inline void bm_set_unused(uint64_t frame)
{
    uint64_t li = frame / BITS_PER_MAPLINE;
    uint64_t bi = frame % BITS_PER_MAPLINE;
    g_bitmap[li] &= ~(1ULL << bi);
}

static void mark_range_used(uint64_t start_frame, uint64_t num_frames)
{
    for (uint64_t i = 0; i < num_frames; ++i) bm_set_used(start_frame + i);
}
static void mark_range_unused(uint64_t start_frame, uint64_t num_frames)
{
    for (uint64_t i = 0; i < num_frames; ++i) bm_set_unused(start_frame + i);
}

/* ====== 初期化 ======
 * 1) すべて used に初期化
 * 2) 使える領域（Type が使用可能）だけ unused にする
 * 3) frame_end を「使用可能領域の最大終端」にする
 */
void page_allocator_init(const MEMORY_MAP* map)
{
    /* 1) 既定 used 埋め */
    for (int i = 0; i < MAPLINE_COUNT; ++i) g_bitmap[i] = ~0ULL;

    g_frame_begin = 1;         /* フレーム 0 は予約 */
    g_frame_end   = 1;

    /* 2) メモリマップ走査 */
    const uint8_t* p    = (const uint8_t*)map->descriptors;
    const size_t   step = (size_t)map->descriptor_size;
    const size_t   end  = (size_t)map->map_size;

    for (size_t off = 0; off + step <= end; off += step) {
        const EFI_MEMORY_DESCRIPTOR* d = (const EFI_MEMORY_DESCRIPTOR*)(p + off);

        /* UEFI ディスクリプタはバージョン差でサイズが違うことがあるが、
           Type/PhysicalStart/NumberOfPages は先頭にあるため利用可能 */
        const uint64_t phys_start = d->PhysicalStart;
        const uint64_t phys_end   = phys_start + d->NumberOfPages * PAGE_SIZE;

        /* 範囲外は切り詰め */
        uint64_t s = phys_start;
        uint64_t e = phys_end;
        if (s >= MAX_PHYS_SIZE) continue;
        if (e >  MAX_PHYS_SIZE) e = MAX_PHYS_SIZE;

        const uint64_t f0  = phys_to_frame(s);
        const uint64_t fN  = phys_to_frame(e);        /* [f0, fN) */
        const uint64_t cnt = (fN > f0) ? (fN - f0) : 0;

        if (cnt == 0) continue;

        if (is_usable_type(d->Type)) {
            mark_range_unused(f0, cnt);
            if (fN > g_frame_end) g_frame_end = fN;
        } else {
            /* 明示的に used にしておく（既定 ~0 なので実際は不要だが安全側に） */
            mark_range_used(f0, cnt);
        }
    }

    /* 最低限のガード：begin < end を保証 */
    if (g_frame_end <= g_frame_begin) g_frame_end = g_frame_begin + 1;
}

void page_allocator_release_boot_services_data(const MEMORY_MAP* map)
{
    if (!map) return;

    const uint8_t* p    = (const uint8_t*)map->descriptors;
    const size_t   step = (size_t)map->descriptor_size;
    const size_t   end  = (size_t)map->map_size;

    for (size_t off = 0; off + step <= end; off += step) {
        const EFI_MEMORY_DESCRIPTOR* d = (const EFI_MEMORY_DESCRIPTOR*)(p + off);
        if (d->Type != EfiBootServicesData) continue;

        uint64_t s = d->PhysicalStart;
        uint64_t e = s + d->NumberOfPages * PAGE_SIZE;

        /* 範囲を上限に丸める */
        if (s >= MAX_PHYS_SIZE) continue;
        if (e >  MAX_PHYS_SIZE) e = MAX_PHYS_SIZE;

        if (e <= s) continue;

        const uint64_t f0  = phys_to_frame(s);
        const uint64_t fN  = phys_to_frame(e);              /* [f0, fN) */
        const uint64_t cnt = (fN > f0) ? (fN - f0) : 0;
        if (cnt == 0) continue;

        /* ここで unused に落とす（この時点では新PTは別領域から確保済み） */
        mark_range_unused(f0, cnt);

        /* g_frame_end を必要なら伸ばす（保守的に） */
        if (fN > g_frame_end) g_frame_end = fN;
    }
}

/* ====== 探索：連続した空きフレームを見つける（先頭から） ====== */
static int find_run(uint64_t needed, uint64_t align_frames, uint64_t* out_start)
{
    if (align_frames == 0) align_frames = 1;

    uint64_t start = (g_frame_begin + align_frames - 1) / align_frames * align_frames;

    while (start + needed <= g_frame_end) {
        uint64_t i = 0;
        for (; i < needed; ++i) {
            if (bm_get(start + i)) break; /* used に当たった */
        }
        if (i == needed) {
            *out_start = start;
            return 1;
        }
        /* 次の整列位置へ */
        uint64_t next = start + i + 1;
        start = (next + align_frames - 1) / align_frames * align_frames;
    }
    return 0;
}

/* ====== 公開 API ====== */

void* page_alloc_bytes(size_t nbytes)
{
    if (nbytes == 0) return NULL;

    uint64_t need_frames = (nbytes + PAGE_SIZE - 1) / PAGE_SIZE;
    uint64_t start;
    if (!find_run(need_frames, 1, &start)) return NULL;

    mark_range_used(start, need_frames);
    uintptr_t phys = frame_to_phys(start);
    return (void*)phys2virt(phys);
}

void page_free_bytes(void* ptr, size_t nbytes)
{
    if (!ptr || nbytes == 0) return;

    /* 先頭フレーム境界に丸め、nbytes もページ数に切り上げ */
    uintptr_t vaddr     = (uintptr_t)ptr;
    uintptr_t vbase     = vaddr & ~PAGE_MASK;
    uint64_t  frames    = (nbytes + PAGE_SIZE - 1) / PAGE_SIZE;
    uint64_t  start_frm = phys_to_frame(virt2phys(vbase));

    mark_range_unused(start_frm, frames);
}

void* page_alloc_pages(size_t num_pages, size_t align_bytes)
{
    if (num_pages == 0) return NULL;

    uint64_t align_frames = 1;
    if (align_bytes > PAGE_SIZE) {
        align_frames = (align_bytes + PAGE_SIZE - 1) / PAGE_SIZE;
    }

    uint64_t start;
    if (!find_run(num_pages, align_frames, &start)) return NULL;

    mark_range_used(start, num_pages);
    uintptr_t phys = frame_to_phys(start);
    return (void*)phys2virt(phys);
}

void* page_alloc_4k_aligned(void)
{
    /* 1ページ(4KiB) を 4KiB アラインで確保 */
    return page_alloc_pages(1, PAGE_SIZE);
}

void page_free_4k(void* ptr)
{
    if (!ptr) return;
    /* 4KiB固定サイズとして解放（内部で境界丸め） */
    page_free_bytes(ptr, PAGE_SIZE);
}
