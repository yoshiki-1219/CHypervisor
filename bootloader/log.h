#pragma once
#include <efi.h>
#include <efilib.h>

typedef enum {
    LOG_DEBUG,
    LOG_INFO,
    LOG_WARN,
    LOG_ERROR
} LOG_LEVEL;

VOID log_init(EFI_SYSTEM_TABLE *SystemTable);
VOID log_set_level(LOG_LEVEL level);
VOID log_set_scope(CONST CHAR16 *scope);

// 例: log_printf(LOG_INFO, L"Loaded %d modules", count);
VOID log_printf(LOG_LEVEL level, CONST CHAR16 *fmt, ...);
