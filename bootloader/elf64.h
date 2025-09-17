// bootloader/elf64.h
#pragma once
#include <efi.h>
#include <efilib.h>
#include <stdint.h>

typedef struct {
    unsigned char e_ident[16]; /* 0x7F 'E' 'L' 'F', EI_CLASS 等 */
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;      /* Entry point virtual address */
    uint64_t e_phoff;      /* Program header table file offset */
    uint64_t e_shoff;      /* Section header table file offset */
    uint32_t e_flags;
    uint16_t e_ehsize;     /* ELF header size */
    uint16_t e_phentsize;  /* Program header entry size */
    uint16_t e_phnum;      /* # of program headers */
    uint16_t e_shentsize;  /* Section header entry size */
    uint16_t e_shnum;      /* # of section headers */
    uint16_t e_shstrndx;   /* Section name string table index */
} Elf64_Ehdr;

/* e_ident index */
enum { EI_MAG0=0, EI_MAG1, EI_MAG2, EI_MAG3, EI_CLASS=4, EI_DATA=5 };

/* magic */
#define ELFMAG0 0x7F
#define ELFMAG1 'E'
#define ELFMAG2 'L'
#define ELFMAG3 'F'

/* EI_CLASS */
#define ELFCLASS64 2

#define EM_X86_64 62

/* e_type */
#define ET_EXEC 2
#define ET_DYN  3

typedef struct {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
} Elf64_Phdr;

/* p_type */
#define PT_NULL    0
#define PT_LOAD    1
#define PT_DYNAMIC 2

/* p_flags */
#define PF_X 0x1
#define PF_W 0x2
#define PF_R 0x4

// elf64.h にプロトタイプを追加
EFI_STATUS elf64_parse_header(const VOID *buf, UINTN buf_size, Elf64_Ehdr *out);
VOID       elf64_log_brief_info(const Elf64_Ehdr *eh);
