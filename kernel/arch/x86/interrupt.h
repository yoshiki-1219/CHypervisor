#pragma once
#include <stdint.h>

/* レジスタスナップショット */
typedef struct __attribute__((packed)) intr_regs {
    uint64_t r8, r9, r10, r11, r12, r13, r14, r15;
    uint64_t rdi, rsi, rbp, rsp, rbx, rdx, rcx, rax;
} intr_regs_t;

typedef struct __attribute__((packed)) intr_context {
    intr_regs_t regs;
    uint64_t    vector;      /* ベクタ番号 */
    uint64_t    error_code;  /* エラーコード or ダミー 0 */
    uint64_t    rip, cs, rflags;
} intr_context_t;

/* ISR から呼ばれる C 側ディスパッチ */
void intr_dispatch_entry(intr_context_t* ctx);

typedef void (*intr_handler_t)(intr_context_t* ctx);
void intr_register_handler(int vec, intr_handler_t fn);
void intr_init_all_vectors(void);

/* ISR スタブの関数テーブル (実体は .c 側) */
extern void (*__isr_stub_table[])(void);

