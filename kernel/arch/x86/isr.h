#pragma once
#include <stdint.h>

/* 共通 ISR が C に渡す「スタック上のスナップショット」 */
typedef struct __attribute__((packed)) intr_regs {
    /* push した順の逆順で並べる都合で、この並びにしています */
    uint64_t r8, r9, r10, r11, r12, r13, r14, r15;
    uint64_t rdi, rsi, rbp, rsp, rbx, rdx, rcx, rax;
} intr_regs_t;

typedef struct __attribute__((packed)) intr_context {
    intr_regs_t regs;
    uint64_t    vector;      /* 共通部で push した vector */
    uint64_t    error_code;  /* 実 error_code またはダミー 0 */

    /* CPU が自動 push した順（privilege 変化無し前提）*/
    uint64_t    rip;
    uint64_t    cs;
    uint64_t    rflags;
} intr_context_t;

/* C 側ディスパッチ入口（共通 ISR から呼ぶ） */
void intr_dispatch_entry(intr_context_t* ctx);

/* ベクタごとの C ハンドラ型と、登録/初期化 API */
typedef void (*intr_handler_t)(intr_context_t* ctx);
void intr_register_handler(int vec, intr_handler_t fn);
void intr_init_all_vectors(void);    /* 256本のスタブを IDT に登録し STI する */
