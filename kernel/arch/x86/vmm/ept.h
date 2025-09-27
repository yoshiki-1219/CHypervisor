#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "arch/x86/paging.h"
#include "page_alloc.h"
// #include "arch/x86/vmm/vcpu.h"
struct Vcpu;
/* ---- 基本型・定数（Zig の定義に対応） ---- */
typedef uint64_t Phys;

enum {
    num_table_entries = 512,
    lv4_shift = 39,
    lv3_shift = 30,
    lv2_shift = 21,
    lv1_shift = 12,
    index_mask = 0x1FF,
};

enum {
    page_shift_4k = 12,
    page_shift_2m = 21,
    page_shift_1g = 30,
    page_size_4k  = 1ULL << page_shift_4k,
    page_size_2m  = 1ULL << page_shift_2m,
    page_size_1g  = 1ULL << page_shift_1g,
    page_mask_4k  = page_size_4k - 1,
    page_mask_2mb = page_size_2m - 1,
    page_mask_1gb = page_size_1g - 1,
};

/* ---- エラー（Zig: error{ AlreadyMapped, OutOfMemory } に対応＋α） ---- */
typedef enum {
    EptOk = 0,
    EptErrAlreadyMapped = -1,
    EptErrOutOfMemory   = -2,
    EptErrInvalidArg    = -3,
    EptErrTooLarge      = -4,
} ept_error_t;

/* ---- MemoryType / PageLevel（Zig の enum に対応） ---- */
typedef enum {
    MemoryType_Uncacheable = 0,
    MemoryType_WriteBack   = 6,
} MemoryType;

typedef enum {
    PageLevel_Four = 3,
    PageLevel_Five = 4,
} PageLevel;

/* ---- EntryBase 相当（Zig の packed struct に忠実） ---- */
typedef union {
    uint64_t u64;
    struct {
        uint64_t read        : 1;  /* R */
        uint64_t write       : 1;  /* W */
        uint64_t exec_super  : 1;  /* X (supervisor) */
        uint64_t type        : 3;  /* メモリタイプ（ページを直接マップ時は ReservedZ=0） */
        uint64_t ignore_pat  : 1;  /* IPAT */
        uint64_t map_memory  : 1;  /* 1=ページ直マップ, 0=下位テーブル参照 */
        uint64_t accessed    : 1;  /* A (EPTP.enable_ad=1 のとき有効) */
        uint64_t dirty       : 1;  /* D (EPTP.enable_ad=1 のとき有効) */
        uint64_t exec_user   : 1;  /* X (user) */
        uint64_t _ignored2   : 1;  /* 予約/無視 */
        uint64_t phys        : 52; /* 物理アドレス >> 12 */
    } f;
} EptEntry;

/* レベル別の別名（Zig: const LvXEntry = EntryBase(.lvX); に対応） */
typedef EptEntry Lv4Entry;
typedef EptEntry Lv3Entry;
typedef EptEntry Lv2Entry;
typedef EptEntry Lv1Entry;

/* テーブル（4KiB, 512 entries） */
typedef struct {
    EptEntry e[num_table_entries];
} EptTable;

/* ---- EPTP（Zig: Eptp packed struct(u64)） ---- */
typedef union {
    uint64_t u64;
    struct {
        uint64_t type      : 3;   /* MemoryType（6=WB 推奨） */
        uint64_t level     : 3;   /* page-walk length-1（four=3, five=4） */
        uint64_t enable_ad : 1;   /* A/D 有効 */
        uint64_t enable_ar : 1;   /* SSS access rights（未使用） */
        uint64_t _res1     : 4;
        uint64_t phys      : 52;  /* Lv4 EPT 物理アドレス >> 12 */
    } f;
} Eptp;

/* ---- API（Zig に忠実な関数構成） ---- */

/* Eptp.new(lv4tbl) 相当：Lv4 テーブルから EPTP を構築 */
Eptp eptp_new(Lv4Entry* lv4tbl);

/* Eptp.getLv4() 相当：EPTP から Lv4 テーブル（ホスト仮想）を取得 */
Lv4Entry* eptp_getLv4(Eptp* eptp);

/* initEpt(): GPA[guest_start..size) を HPA[host_start..size) に 2MiB ページでマップし、EPTP を返す */
ept_error_t initEpt(Phys guest_start,
                    Phys host_start,
                    size_t size,
                    Eptp* out_eptp);

/* translate(): GPA → HPA 変換（Lv4 テーブルを渡す） */
bool translate_gpa_to_hpa(Phys guest_gpa, Lv4Entry* lv4tbl, Phys* out_hpa);

/* ept.h の最後を修正 */
int vcpu_setup_ept(struct Vcpu* vcpu);
