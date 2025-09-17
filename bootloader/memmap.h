// bootloader/memmap.h
#pragma once
#include <efi.h>
#include <efilib.h>

typedef struct {
    UINTN                 buffer_size;         // 準備したバッファ総サイズ（バイト）
    EFI_MEMORY_DESCRIPTOR *descriptors;        // マップを格納する先頭ポインタ
    UINTN                 map_size;            // 実際のマップのサイズ（バイト）
    UINTN                 map_key;             // ExitBootServices 用キー
    UINTN                 descriptor_size;     // 1要素のサイズ
    UINT32                descriptor_version;  // ディスクリプタのバージョン
} MEMORY_MAP;

// 初期バッファを確保（pages: 4KiB 単位のページ数。例: 4ページ）
EFI_STATUS memmap_init(MEMORY_MAP *mm, UINTN pages);

// GetMemoryMap を実行して最新に更新（必要なら再確保してリトライ）
EFI_STATUS memmap_refresh(MEMORY_MAP *mm);

// 人間向けに表示（ExitBootServices 前のみ使用可）
VOID memmap_print(const MEMORY_MAP *mm);

// ExitBootServices を map_key を使って実行（失敗時は再取得→再試行）
EFI_STATUS exit_boot_services_with_map(EFI_HANDLE ImageHandle, MEMORY_MAP *mm);

// 取得用バッファを解放（ExitBootServices の前に使うこと）
VOID memmap_destroy(MEMORY_MAP *mm);
