// bootloader/elf64.c
#include "elf64.h"
#include "log.h"

static BOOLEAN elf64_is_valid_magic(const Elf64_Ehdr *eh)
{
    return eh->e_ident[EI_MAG0] == ELFMAG0 &&
           eh->e_ident[EI_MAG1] == ELFMAG1 &&
           eh->e_ident[EI_MAG2] == ELFMAG2 &&
           eh->e_ident[EI_MAG3] == ELFMAG3;
}

EFI_STATUS elf64_parse_header(const VOID *buf, UINTN buf_size, Elf64_Ehdr *out)
{
    if (!buf || !out) return EFI_INVALID_PARAMETER;
    if (buf_size < sizeof(Elf64_Ehdr)) return EFI_BUFFER_TOO_SMALL;

    const Elf64_Ehdr *eh = (const Elf64_Ehdr*)buf;

    if (!elf64_is_valid_magic(eh)) {
        log_printf(LOG_ERROR, L"ELF magic mismatch.");
        return EFI_LOAD_ERROR;
    }
    if (eh->e_ident[EI_CLASS] != ELFCLASS64) {
        log_printf(LOG_ERROR, L"ELF is not 64-bit (EI_CLASS=%d).", eh->e_ident[EI_CLASS]);
        return EFI_UNSUPPORTED;
    }
    if (eh->e_machine != EM_X86_64) {
        log_printf(LOG_WARN, L"ELF e_machine != x86_64 (=%u).", eh->e_machine);
        // x86_64 以外は基本対象外。必要なら EFI_UNSUPPORTED を返す
    }

    *out = *eh;  // ヘッダをコピー
    return EFI_SUCCESS;
}

VOID elf64_log_brief_info(const Elf64_Ehdr *eh)
{
    if (!eh) return;
    log_printf(LOG_INFO, L"Parsed kernel ELF header.");
    log_printf(LOG_DEBUG, L"Kernel ELF information:");
    log_printf(LOG_DEBUG, L"  Entry Point         : 0x%lx", (UINT64)eh->e_entry);
    log_printf(LOG_DEBUG, L"  Is 64-bit           : %d", (eh->e_ident[EI_CLASS] == ELFCLASS64) ? 1 : 0);
    log_printf(LOG_DEBUG, L"  # of Program Headers: %u", eh->e_phnum);
    log_printf(LOG_DEBUG, L"  # of Section Headers: %u", eh->e_shnum);
}
