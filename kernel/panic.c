// panic.c (差し替え)
#include "panic.h"
#include "log.h"
#include <stdarg.h>
#include <stdint.h>
#include <stddef.h>

/* --- arch helpers --- */
static inline void cli(void) { __asm__ __volatile__("cli"); }
static inline void hlt(void) { __asm__ __volatile__("hlt"); }
static inline uint64_t read_rbp(void) { uint64_t v; __asm__ __volatile__("mov %%rbp,%0":"=r"(v)); return v; }
static inline uint64_t read_cr2(void) { uint64_t v; __asm__ __volatile__("mov %%cr2,%0":"=r"(v)); return v; }
static inline int is_canonical(uint64_t vaddr) {
    uint64_t top = vaddr >> 47;
    return (top == 0) || (top == 0x1FFFF);
}

/* --- tiny formatter (freestanding) --- */
static size_t u64_to_hex(char *dst, uint64_t v, int upper) {
    const char *d = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    char buf[16]; size_t n=0;
    if (v==0) { dst[0]='0'; return 1; }
    while (v) { buf[n++] = d[v & 0xF]; v >>= 4; }
    for (size_t i=0;i<n;i++) dst[i]=buf[n-1-i];
    return n;
}
static size_t u_to_dec(char *dst, unsigned long long v) {
    char buf[20]; size_t n=0;
    if (v==0) { dst[0]='0'; return 1; }
    while (v) { buf[n++] = '0' + (v % 10); v/=10; }
    for (size_t i=0;i<n;i++) dst[i]=buf[n-1-i];
    return n;
}
static size_t i_to_dec(char *dst, long long v) {
    if (v<0) { dst[0]='-'; return 1 + u_to_dec(dst+1, (unsigned long long)(-v)); }
    return u_to_dec(dst, (unsigned long long)v);
}

static int mini_vsnprintf(char *out, size_t cap, const char *fmt, va_list ap) {
    size_t w=0;
    #define PUT(ch) do{ if (w+1<cap) out[w]= (ch); w++; }while(0)
    for (; *fmt; ++fmt) {
        if (*fmt!='%') { PUT(*fmt); continue; }
        ++fmt;
        int longcnt=0; while (*fmt=='l') { longcnt++; ++fmt; }
        char tmp[32]; size_t n=0;
        switch(*fmt) {
            case 's': {
                const char* s = va_arg(ap, const char*);
                if (!s) s = "(null)";
                while (*s) { PUT(*s++); }
                break;
            }
            case 'c': { int c = va_arg(ap,int); PUT((char)c); break; }
            case 'p': {
                uint64_t v = (uint64_t)(uintptr_t)va_arg(ap, void*);
                PUT('0'); PUT('x'); n = u64_to_hex(tmp, v, 0);
                for (size_t i=0;i<n;i++) PUT(tmp[i]);
                break;
            }
            case 'x': case 'X': {
                unsigned long long v =
                    (longcnt>=2)? va_arg(ap, unsigned long long) :
                    (longcnt==1)? va_arg(ap, unsigned long) :
                                  va_arg(ap, unsigned int);
                n = u64_to_hex(tmp, (uint64_t)v, *fmt=='X');
                for (size_t i=0;i<n;i++) PUT(tmp[i]);
                break;
            }
            case 'u': {
                unsigned long long v =
                    (longcnt>=2)? va_arg(ap, unsigned long long) :
                    (longcnt==1)? va_arg(ap, unsigned long) :
                                  va_arg(ap, unsigned int);
                n = u_to_dec(tmp, v);
                for (size_t i=0;i<n;i++) PUT(tmp[i]);
                break;
            }
            case 'd': {
                long long v =
                    (longcnt>=2)? va_arg(ap, long long) :
                    (longcnt==1)? va_arg(ap, long) :
                                  va_arg(ap, int);
                n = i_to_dec(tmp, v);
                for (size_t i=0;i<n;i++) PUT(tmp[i]);
                break;
            }
            case '%': PUT('%'); break;
            default:  /* 未対応指定子はそのまま出す */
                PUT('%'); PUT(*fmt ? *fmt : '?'); break;
        }
    }
    if (cap) { out[w < cap ? w : cap-1] = '\0'; }
    return (int)w;
    #undef PUT
}

/* --- panic core --- */
static volatile int g_panicked = 0;

void panic(const char* msg)
{
    cli();

    if (g_panicked) {
        KLOG_ERROR("panic", "Double panic detected. Halting.");
        for(;;) hlt();
    }
    g_panicked = 1;

    if (msg && *msg) KLOG_ERROR("panic", "%s", msg);
    else             KLOG_ERROR("panic", "(no message)");

    KLOG_ERROR("panic", "CR2 = 0x%016llX", (unsigned long long)read_cr2());

    KLOG_ERROR("panic", "=== Stack Trace ==============");
    uint64_t rbp = read_rbp();
    for (int i=0; i<64; ++i) {
        if (rbp==0 || (rbp & 0x7) || !is_canonical(rbp)) break;
        uint64_t* f = (uint64_t*)rbp;
        uint64_t next = f[0];
        uint64_t rip  = f[1];
        if (!is_canonical(rip)) break;
        KLOG_ERROR("panic", "#%02d: 0x%016llX", i, (unsigned long long)rip);
        if (next <= rbp) break;
        rbp = next;
    }

    for(;;) hlt();
}

void panicf(const char* fmt, ...)
{
    char buf[512];
    va_list ap; va_start(ap, fmt);
    mini_vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    panic(buf);
}
