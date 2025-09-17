// bootloader/log.c
#include "log.h"

static SIMPLE_TEXT_OUTPUT_INTERFACE *g_con = NULL;
static LOG_LEVEL g_level = LOG_INFO;
static CONST CHAR16 *g_scope = L"default";

static CONST CHAR16* level_str(LOG_LEVEL lv) {
    switch (lv) {
        case LOG_DEBUG: return L"[DEBUG]";
        case LOG_INFO:  return L"[INFO ]";
        case LOG_WARN:  return L"[WARN ]";
        case LOG_ERROR: return L"[ERROR]";
        default:        return L"[INFO ]";
    }
}

VOID log_init(EFI_SYSTEM_TABLE *SystemTable) {
    g_con = (SystemTable && SystemTable->ConOut) ? SystemTable->ConOut : NULL;
}

VOID log_set_level(LOG_LEVEL level) { g_level = level; }
VOID log_set_scope(CONST CHAR16 *scope) { g_scope = (scope && scope[0]) ? scope : L"default"; }

VOID log_printf(LOG_LEVEL level, CONST CHAR16 *fmt, ...)
{
    if (level < g_level) return;

    Print(L"%s (%s): ", level_str(level), g_scope);

    va_list args;
    va_start(args, fmt);
    VPrint(fmt, args);
    va_end(args);

    Print(L"\r\n");
}
