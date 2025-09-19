#pragma once
#include <stddef.h>
#include <stdint.h>

/* 注意: memcpy はオーバーラップ未対応（必要なら memmove を実装） */
void* memcpy(void* dst, const void* src, size_t n);
int   memcmp(const void* s1, const void* s2, size_t n);
