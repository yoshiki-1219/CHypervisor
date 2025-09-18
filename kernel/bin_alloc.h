#pragma once
#include <stddef.h>
#include <stdint.h>

/* 公開 API */
void  bin_alloc_init(void);

/* 要求サイズ n とアライン align を満たすメモリを返す（align==0 は無視） */
void* kmalloc(size_t n, size_t align);
/* 確保時のサイズ n を渡して解放（Bin はサイズから判断できる） */
void  kfree(void* p, size_t n);
