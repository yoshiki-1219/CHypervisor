// kernel/bootinfo.h
#pragma once
#include <stdint.h>

#if INTPTR_MAX == INT64_MAX
typedef uint64_t UINTN;
#else
#error "This kernel assumes x86_64."
#endif

typedef struct {
    UINTN   buffer_size;
    void   *descriptors;
    UINTN   map_size;
    UINTN   map_key;
    UINTN   descriptor_size;
    uint32_t descriptor_version;
} MEMORY_MAP;

#define BOOTINFO_MAGIC 0xDEADBEEFCAFEBABEull

typedef struct {
    uint64_t  magic;
    MEMORY_MAP memory_map;
} BOOT_INFO;
