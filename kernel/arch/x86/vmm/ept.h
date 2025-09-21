#pragma once
#include <stdint.h>
#include <stddef.h>

/* ===== EPT 定数 ===== */
#define EPT_ENTRIES         512
#define EPT_IDX_MASK        0x1FFULL

#define EPT_LV4_SHIFT       39
#define EPT_LV3_SHIFT       30
#define EPT_LV2_SHIFT       21
#define EPT_LV1_SHIFT       12

#define PAGE_SIZE_4K        0x1000ULL
#define PAGE_SIZE_2M        0x200000ULL
#define PAGE_2M_ALIGN       0x200000ULL

/* EPT メモリタイプ */
/* EPT メモリタイプ (Intel SDM Vol.3 Table 11-12) */
enum {
    EPT_TYPE_UC   = 0,  /* Uncacheable */
    EPT_TYPE_WC   = 1,  /* Write Combining */
    /* 2, 3 は予約済み */
    EPT_TYPE_WT   = 4,  /* Write Through */
    EPT_TYPE_WP   = 5,  /* Write Protected */
    EPT_TYPE_WB   = 6,  /* Write Back */
    EPT_TYPE_UC_  = 7   /* Uncacheable- */
};


/* ===== EPT エントリ（共通）: SDM 29.3.3 Figure 29-1 準拠の最小実装 ===== */
/* Lv4/Lv3 は “次テーブル参照”（super=0）、Lv2 は 2MiB 大ページ（super=1）で使う */
typedef union {
    uint64_t u64;
    struct {
        uint64_t r      : 1;  // 0
        uint64_t w      : 1;  // 1
        uint64_t xs     : 1;  // 2
        uint64_t mt     : 3;  // 3–5
        uint64_t ipat   : 1;  // 6
        uint64_t super  : 1;  // 7 (PTEでは0)
        uint64_t a      : 1;  // 8
        uint64_t d      : 1;  // 9
        uint64_t xu     : 1;  // 10 (MBEC)
        uint64_t rsv1   : 1;  // 11
        uint64_t phys   : 40; // 12–51 (MAXPHYADDR次第で要注意)
        uint64_t rsv2   : 6;  // 52–57
        uint64_t pw     : 1;  // 58 [paging-write access]
        uint64_t ign59  : 1;  // 59 Ignored
        uint64_t sss    : 1;  // 60 [supervisor shadow stack]
        uint64_t spwp   : 1;  // 61 [sub-page write perms]
        uint64_t ign62  : 1;  // 62 Ignored
        uint64_t spve   : 1;  // 63 Suppress #VE
    } f;
} ept_entry_t;



typedef struct {
    ept_entry_t e[EPT_ENTRIES];
} ept_table_t;

/* EPTP: Extended Page Table Pointer */
typedef union {
    uint64_t u64;
    struct {
        uint64_t type   : 3;   /* 6=WB 推奨 */
        uint64_t pwl    : 3;   /* page-walk length-1 (4段=3) */
        uint64_t ad     : 1;   /* accessed/dirty 有効 */
        uint64_t ear    : 1;   /* SSSP（未使用） */
        uint64_t rsv1   : 4;
        uint64_t pml4   : 40;  /* PML4 物理アドレス >> 12 */
        uint64_t rsv2   : 12;
    } f;
} eptp_t;


/* ===== EPT API ===== */
/* 連続 HPA を GPA=guest_start..(size) に 2MiB ページで恒等マップする EPT を作成 */
int  ept_build_identity_2m(uint64_t guest_start_gpa,
                           uint64_t host_start_hpa,
                           size_t   size_bytes,
                           eptp_t*  out_eptp);

/* ゲスト用連続メモリを確保（2MiB アライン, size=guest_bytes）、EPT を構築して返す */
int  ept_prepare_guest_region(uint64_t* out_guest_hva,
                              uint64_t* out_guest_hpa,
                              size_t    guest_bytes,
                              eptp_t*   out_eptp);

/* VMCS: Secondary-Proc Controls で EPT/Unrestricted/VPID を有効化し、EPTP をセット */
int  vmx_enable_ept_unrestricted_vpid(eptp_t eptp, uint16_t vpid);

/* ゲストの RIP=0 実行用に、ホスト側関数 blobGuest をゲスト物理先頭へコピー（HLT ループ想定） */

int  ept_copy_blob_and_set_rip0(uint64_t guest_base_hpa);
