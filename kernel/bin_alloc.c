#include "bin_alloc.h"
#include "page_alloc.h"

/* 4KiB ページ固定 */
#ifndef PAGE_SIZE
#define PAGE_SIZE  4096ULL
#endif

/* 扱う bin サイズ（必要に応じて調整可） */
static const size_t g_bin_sizes[] = { 0x20, 0x40, 0x80, 0x100, 0x200, 0x400, 0x800 };
enum { BIN_COUNT = (int)(sizeof(g_bin_sizes)/sizeof(g_bin_sizes[0])) };

/* フリー時だけ使うメタノード（単方向リスト） */
typedef struct chunk_node {
    struct chunk_node* next;
} chunk_node;

/* 各 bin のフリーリスト先頭 */
static chunk_node* g_free_heads[BIN_COUNT];

/* --- ユーティリティ --- */
static inline size_t max_size(size_t a, size_t b) { return a > b ? a : b; }

/* 要求サイズから bin index を求める（無ければ -1） */
static int bin_index_for(size_t need)
{
    for (int i = 0; i < BIN_COUNT; ++i) {
        if (need <= g_bin_sizes[i]) return i;
    }
    return -1;
}

/* push/pop（LIFO） */
static inline void push_node(chunk_node** head, chunk_node* n)
{
    n->next = *head;
    *head = n;
}
static inline chunk_node* pop_node(chunk_node** head)
{
    chunk_node* n = *head;
    if (!n) return NULL;
    *head = n->next;
    return n;
}

/* bin 用の新規 4KiB ページを その bin サイズで割ってフリーリストに積む */
static int refill_bin(int idx)
{
    const size_t bsz = g_bin_sizes[idx];

    /* 1ページ確保（ページ境界・ページサイズ） */
    uint8_t* page = (uint8_t*)page_alloc_pages(1, PAGE_SIZE);
    if (!page) return 0;

    const size_t cnt = PAGE_SIZE / bsz; /* 端数は切り捨て（bsz は 4KiB 以下前提） */
    for (size_t i = 0; i < cnt; ++i) {
        chunk_node* n = (chunk_node*)(page + i * bsz);
        push_node(&g_free_heads[idx], n);
    }
    return 1;
}

/* --- 公開 --- */

void bin_alloc_init(void)
{
    for (int i = 0; i < BIN_COUNT; ++i) g_free_heads[i] = 0;
}

/* kmalloc:
 * - size <= 最大 bin → 該当 bin から確保
 * - size > 最大 bin → バックエンド（ページアロケータ）へ
 * - align > bin_size の場合は「need = max(size, align)」で bin を選ぶ
 */
void* kmalloc(size_t n, size_t align)
{
    if (n == 0) return NULL;

    /* アライン 0/1 は無視。2^k でなくても「とにかくその値以上」の bin を選ぶのでOK */
    size_t need = max_size(n, align ? align : 0);

    int idx = bin_index_for(need);
    if (idx >= 0) {
        /* 該当 bin が空なら 4KiB 追加割当 */
        if (!g_free_heads[idx] && !refill_bin(idx)) return NULL;

        chunk_node* n0 = pop_node(&g_free_heads[idx]);
        return (void*)n0; /* メタ無しでそのままユーザに返す */
    }

    /* 大物はページアロケータへ。
       - align がページサイズ以下 → バイト確保で OK（ページ境界保証が不要なら）
       - align がページサイズ超 → ページ API で align 指定
     */
    if (align > PAGE_SIZE) {
        size_t pages = (n + PAGE_SIZE - 1) / PAGE_SIZE;
        return page_alloc_pages(pages, align);
    } else {
        /* ページ境界不要なら bytes API（内部はページ割当） */
        return page_alloc_bytes(n);
    }
}

void kfree(void* p, size_t n)
{
    if (!p || n == 0) return;

    /* どの bin 相当か判定
       解放サイズが bin のちょうど値でなくても、「そのサイズが入る最小 bin」に戻すだけで OK */
    int idx = bin_index_for(n);

    if (idx >= 0) {
        chunk_node* node = (chunk_node*)p;
        push_node(&g_free_heads[idx], node);
        return;
    }

    /* 大物はそのまま bytes で返す（ページ境界に切り上げて確保している想定） */
    page_free_bytes(p, n);
}
