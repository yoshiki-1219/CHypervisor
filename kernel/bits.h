// kernel/bits.h
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <limits.h>

/*
 * オプション: 実行時チェック（DEBUG 時のみ有効化したい場合は Makefile で -DBITS_ENABLE_RUNTIME_CHECK を付与）
 */
#ifndef BITS_ENABLE_RUNTIME_CHECK
#define BITS_ASSERT(cond) ((void)0)
#else
#include "serial.h" // 任意。シリアルに出したい場合。不要なら削除可。
static inline void __bits_assert_fail(const char* expr, const char* file, int line) {
    (void)expr; (void)file; (void)line;
    // ベアメタルで止めたい場合:
    for(;;) __asm__ __volatile__("hlt");
}
#define BITS_ASSERT(cond) do { if(!(cond)) __bits_assert_fail(#cond, __FILE__, __LINE__); } while(0)
#endif

/* ========== 1) 指定ビットのみを立てる tobit ========== */
/*  使用例: uint8_t m = bits_tobit_u8(4); // 0b0001_0000 */
static inline uint8_t  bits_tobit_u8 (unsigned n){ BITS_ASSERT(n < 8 ); return (uint8_t )(UINT8_C(1)  << (n & 7)); }
static inline uint16_t bits_tobit_u16(unsigned n){ BITS_ASSERT(n < 16); return (uint16_t)(UINT16_C(1) << (n & 15)); }
static inline uint32_t bits_tobit_u32(unsigned n){ BITS_ASSERT(n < 32); return (uint32_t)(UINT32_C(1) << (n & 31)); }
static inline uint64_t bits_tobit_u64(unsigned n){ BITS_ASSERT(n < 64); return (uint64_t)(UINT64_C(1) << (n & 63)); }

/* 汎用マクロ：ターゲット型を明示*/
#define BITS_TOBIT(T, n) \
    ((T)(( (sizeof(T)==1)? (UINT8_C(1)  << ((n)&7)) : \
          (sizeof(T)==2)? (UINT16_C(1) << ((n)&15)): \
          (sizeof(T)==4)? (UINT32_C(1) << ((n)&31)): \
                          (UINT64_C(1) << ((n)&63)) ))))

/* enum を渡したいときは事前に整数化する（例： BITS_TOBIT(uint8_t, (unsigned)my_enum) ） */

/* ========== 2) 指定ビットが立っているか isset ========== */
/*  val は任意の符号なし整数を推奨（符号付きでも右シフトの実装定義を避けるためキャストする） */
#define BITS_ISSET(val, n)  ( (( (uintmax_t)(val) >> (unsigned)(n) ) & UINTMAX_C(1)) != 0 )

/* ========== 3) 2つの整数を連結する concat ========== */
static inline uint16_t bits_concat_u8_u8(uint8_t hi, uint8_t lo) {
    return (uint16_t)((uint16_t)hi << 8) | (uint16_t)lo;
}
static inline uint32_t bits_concat_u16_u16(uint16_t hi, uint16_t lo) {
    return (uint32_t)((uint32_t)hi << 16) | (uint32_t)lo;
}
static inline uint64_t bits_concat_u32_u32(uint32_t hi, uint32_t lo) {
    return (uint64_t)((uint64_t)hi << 32) | (uint64_t)lo;
}

/* 汎用版：a と b が同型・同ビット幅である前提。結果は 2 倍幅を返すのが理想だが、C では型演算がないため
 * よくある 8/16/32 の3ケースに限定したマクロを提供する。
 * 例：BITS_CONCAT(a, b) は sizeof(a) によって上記専用関数にディスパッチ。
 */
#define BITS_CONCAT(a, b) \
    (  sizeof(a)==1 ? (uint16_t)bits_concat_u8_u8 ((uint8_t )(a), (uint8_t )(b)) \
     : sizeof(a)==2 ? (uint32_t)bits_concat_u16_u16((uint16_t)(a), (uint16_t)(b)) \
                    : (uint64_t)bits_concat_u32_u32((uint32_t)(a), (uint32_t)(b)) )

/* WRMSR 用など：EDX:EAX → 64bit */
static inline uint64_t bits_concat_edx_eax(uint32_t edx, uint32_t eax) {
    return bits_concat_u32_u32(edx, eax);
}
