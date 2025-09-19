#include "common.h"

void* memcpy(void* dst, const void* src, size_t n)
{
    unsigned char*       d = (unsigned char*)dst;
    const unsigned char* s = (const unsigned char*)src;

    while (n--) {
        *d++ = *s++;
    }
    return dst;
}

int memcmp(const void* s1, const void* s2, size_t n)
{
    const unsigned char* a = (const unsigned char*)s1;
    const unsigned char* b = (const unsigned char*)s2;

    while (n--) {
        if (*a != *b) {
            return (int)*a - (int)*b;  /* unsigned 差 */
        }
        ++a;
        ++b;
    }
    return 0;
}
