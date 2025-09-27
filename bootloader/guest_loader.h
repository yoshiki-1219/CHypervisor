// bootloader/guest_loader.h
#pragma once
#include <efi.h>
#include <efilib.h>

// bzImage をファイルから読み込み、EfiLoaderData で確保した物理メモリに配置する。
// out_start: 物理アドレス（AllocatePages の戻り）
// out_size : 実際に読み込んだバイト数
EFI_STATUS load_guest_bzimage(CONST CHAR16 *filename,
                              EFI_PHYSICAL_ADDRESS *out_start,
                              UINTN *out_size);
