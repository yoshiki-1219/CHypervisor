#pragma once
#include <efi.h>
#include <efilib.h>
#include <stdint.h>

EFI_STATUS load_kernel_elf(CONST CHAR16 *filename, UINT64 *out_entry);
