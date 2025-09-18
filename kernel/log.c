#include "log.h"
#include <stdarg.h>
#include <stdint.h>

/* ---- 内部状態 ---- */
static serial_device_t* g_ser = 0;
static klog_level_t     g_level = KLOG_INFO;

/* ---- ユーティリティ：シリアルへ 1 文字/文字列 ---- */
static inline void putc_serial(char c) {
    if (!g_ser) return;
    serial_write_byte(g_ser, (uint8_t)c);
}
static void puts_serial_raw(const char* s) {
    if (!g_ser || !s) return;
    serial_write(g_ser, s);
}
/* 幅とゼロ埋め対応の数値出力（base=10/16、uppercase は 16進のみ有効） */
static void write_uint_padded(uint64_t v, unsigned base, int width, int zero_pad, int uppercase) {
    /* 十分に大きいバッファ（前ゼロ埋め分は別途 putc で出すので、ここは桁数分だけで足りる） */
    char tmp[32];
    const char* digits_l = "0123456789abcdef";
    const char* digits_u = "0123456789ABCDEF";
    const char* digits   = uppercase ? digits_u : digits_l;

    int n = 0;
    do {
        tmp[n++] = digits[v % base];
        v /= base;
    } while (v);

    /* 幅に満たない場合のパディング */
    int pad = (width > n) ? (width - n) : 0;
    for (int i = 0; i < pad; ++i) putc_serial(zero_pad ? '0' : ' ');

    /* 逆順で吐く */
    for (int i = n - 1; i >= 0; --i) putc_serial(tmp[i]);
}

static void write_int_padded(int64_t x, int width, int zero_pad) {
    if (x < 0) {
        /* ゼロ埋め時は、符号の直後からゼロ埋めするのが一般的 */
        putc_serial('-');
        uint64_t ux = (uint64_t)(-x);
        int inner_width = (width > 0) ? (width - 1) : 0; /* '-' を差し引く */
        write_uint_padded(ux, 10, inner_width, zero_pad, 0);
    } else {
        write_uint_padded((uint64_t)x, 10, width, zero_pad, 0);
    }
}
/* Zig の「scope 7 文字整形」を再現。
 * 長さ <=7 : "xxxxxxx | "
 * 長さ > 7 : "xxxxxxx-| "
 */
static void write_scope_field(const char* scope) {
    char buf[12]; /* 7 + (" | " or "-| ") = 最大 10。余裕を持って 12 */
    int i = 0;

    if (!scope) scope = "global"; /* デフォルト */

    int len = 0;
    while (scope[len] && len < 7) len++;

    /* 先頭 7 文字（足りなければ空白パディング） */
    for (i = 0; i < 7; ++i) {
        if (i < len) buf[i] = scope[i];
        else         buf[i] = ' ';
    }

    /* 7 文字を超えるかどうかで接尾辞を変える */
    if (scope[len] != '\0') { /* もっと長い */
        buf[7] = '-';
        buf[8] = '|';
        buf[9] = ' ';
        buf[10]= '\0';
    } else {
        buf[7] = ' ';
        buf[8] = '|';
        buf[9] = ' ';
        buf[10]= '\0';
    }
    puts_serial_raw(buf);
}

/* ---- 数値 → 文字列（最小限のフォーマッタ） ---- */
/* 基数は 10 または 16。buf は末尾 '\0' 付与。戻り値は書き込んだ長さ。*/
static int utoa_rev(uint64_t v, unsigned base, char* buf) {
    static const char* digits = "0123456789abcdef";
    int n = 0;
    do {
        buf[n++] = digits[v % base];
        v /= base;
    } while (v);
    return n;
}
static void write_uint(uint64_t v, unsigned base) {
    char tmp[32];
    int n = utoa_rev(v, base, tmp);
    for (int i = n - 1; i >= 0; --i) putc_serial(tmp[i]);
}
static void write_int(int64_t x) {
    if (x < 0) { putc_serial('-'); write_uint((uint64_t)(-x), 10); }
    else        write_uint((uint64_t)x, 10);
}

/* %s %c %d %u %x/%X %p %lu %llu %zu 程度をサポート（最小限）*/
static void kvprintf(const char* fmt, va_list ap) {
    for (const char* p = fmt; *p; ++p) {
        if (*p != '%') {
            if (*p == '\n') putc_serial('\r');
            putc_serial(*p);
            continue;
        }

        /* '%' を検出。以降：フラグ→幅→長さ→変換 という順で解析 */
        ++p;

        /* --- フラグ（必要最小限：'0' のみ対応） --- */
        int zero_pad = 0;
        int done_flags = 0;
        while (!done_flags) {
            if (*p == '0') { zero_pad = 1; ++p; }
            else { done_flags = 1; }
        }

        /* --- 幅（数字列） --- */
        int width = 0;
        while (*p >= '0' && *p <= '9') {
            width = width * 10 + (*p - '0');
            ++p;
        }

        /* --- 長さ修飾子（z, l, ll） --- */
        enum { LEN_DEF, LEN_Z, LEN_L, LEN_LL } len = LEN_DEF;
        if (*p == 'z') { len = LEN_Z; ++p; }
        else if (*p == 'l') {
            if (*(p+1) == 'l') { len = LEN_LL; p += 2; }
            else { len = LEN_L; ++p; }
        }

        /* --- 変換指定子 --- */
        char c = *p;
        switch (c) {
        case 'c': {
            int ch = va_arg(ap, int);
            putc_serial((char)ch);
            break;
        }
        case 's': {
            const char* s = va_arg(ap, const char*);
            if (!s) s = "(null)";
            for (; *s; ++s) {
                if (*s == '\n') putc_serial('\r');
                putc_serial(*s);
            }
            break;
        }
        case 'd': case 'i': {
            int64_t v;
            if (len == LEN_LL)      v = (int64_t)va_arg(ap, long long);
            else if (len == LEN_L)  v = (int64_t)va_arg(ap, long);
            else if (len == LEN_Z)  v = (int64_t)va_arg(ap, size_t);
            else                    v = (int64_t)va_arg(ap, int);
            write_int_padded(v, width, zero_pad);
            break;
        }
        case 'u': {
            uint64_t v;
            if (len == LEN_LL)      v = (uint64_t)va_arg(ap, unsigned long long);
            else if (len == LEN_L)  v = (uint64_t)va_arg(ap, unsigned long);
            else if (len == LEN_Z)  v = (uint64_t)va_arg(ap, size_t);
            else                    v = (uint64_t)va_arg(ap, unsigned int);
            write_uint_padded(v, 10, width, zero_pad, 0);
            break;
        }
        case 'x': case 'X': {
            int uppercase = (c == 'X');
            uint64_t v;
            if (len == LEN_LL)      v = (uint64_t)va_arg(ap, unsigned long long);
            else if (len == LEN_L)  v = (uint64_t)va_arg(ap, unsigned long);
            else if (len == LEN_Z)  v = (uint64_t)va_arg(ap, size_t);
            else                    v = (uint64_t)va_arg(ap, unsigned int);
            write_uint_padded(v, 16, width, zero_pad, uppercase);
            break;
        }
        case 'p': {
            uintptr_t v = (uintptr_t)va_arg(ap, void*);
            puts_serial_raw("0x");
            /* ポインタは 16桁ゼロ埋めが見やすい（x86_64） */
            write_uint_padded((uint64_t)v, 16, 16, 1, 0);
            break;
        }
        case '%':
            putc_serial('%');
            break;
        default:
            /* 未対応指定子はそのまま出力（従来動作を踏襲） */
            putc_serial('%');
            putc_serial(c);
            break;
        }
    }
}

/* ---- 公開 API ---- */
void klog_init(serial_device_t* dev, klog_options_t opt) {
    g_ser   = dev;
    g_level = opt.level;
}

void klog_set_level(klog_level_t level) { g_level = level; }

void klog_vlogf(klog_level_t level, const char* scope, const char* fmt, va_list ap) {
    if (!g_ser) return;
    if (level < g_level) return;

    /* [LEVEL] と スコープ欄（7 文字整形） */
    switch (level) {
        case KLOG_DEBUG: puts_serial_raw("[DEBUG] "); break;
        case KLOG_INFO:  puts_serial_raw("[INFO ] "); break;
        case KLOG_WARN:  puts_serial_raw("[WARN ] "); break;
        case KLOG_ERROR: puts_serial_raw("[ERROR] "); break;
        default:         puts_serial_raw("[?????] "); break;
    }
    write_scope_field(scope);

    /* 本文 */
    kvprintf(fmt, ap);

    /* 改行（\n を \r\n に） */
    puts_serial_raw("\r\n");
}

void klog_logf(klog_level_t level, const char* scope, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    klog_vlogf(level, scope, fmt, ap);
    va_end(ap);
}
