#include <efi.h>
#include <efilib.h>
#include <stdint.h>
#include "log.h"
#include "elf64.h"
#include "file.h"
#include "arch_x86_page.h"

#define PAGE_SIZE_4K  4096ull
static inline uint64_t align_up(uint64_t x, uint64_t a)   { return (x + a - 1) & ~(a - 1); }
static inline uint64_t align_down(uint64_t x, uint64_t a) { return x & ~(a - 1); }

static EFI_STATUS file_read_at(EFI_FILE_HANDLE File, UINT64 offset, VOID *buf, UINTN size)
{
    EFI_STATUS st = uefi_call_wrapper(File->SetPosition, 2, File, offset);
    if (EFI_ERROR(st)) return st;

    UINTN done = 0;
    while (done < size) {
        UINTN chunk = size - done;
        st = uefi_call_wrapper(File->Read, 3, File, &chunk, (UINT8*)buf + done);
        if (EFI_ERROR(st)) return st;
        if (chunk == 0) break; // EOF
        done += chunk;
    }
    return (done == size) ? EFI_SUCCESS : EFI_END_OF_FILE;
}

static PageAttr attr_from_phdr(const Elf64_Phdr *p) {
    bool x = (p->p_flags & PF_X) != 0;
    bool w = (p->p_flags & PF_W) != 0;
    if (x && w) return PAGE_RWX; // 実行+書き込み
    if (x)      return PAGE_RX;  // 実行のみ
    if (w)      return PAGE_RW;  // 書き込み（NX）
    return PAGE_RO;              // 読み取りのみ（NX）
}

static PageAttr temp_attr_for_load(const Elf64_Phdr *p) {
    return (p->p_flags & PF_X) ? PAGE_RWX : PAGE_RW;
}
static EFI_STATUS map_segment_for_load(const Elf64_Phdr *p) {
    uint64_t v = align_down(p->p_vaddr,  PAGE_SIZE_4K);
    uint64_t pa= align_down(p->p_paddr,  PAGE_SIZE_4K);
    uint64_t sz= align_up(p->p_memsz + (p->p_vaddr - v), PAGE_SIZE_4K);
    PageAttr  a= temp_attr_for_load(p);

    for (uint64_t off = 0; off < sz; off += PAGE_SIZE_4K) {
        EFI_STATUS st = page_map4k_to(v + off, pa + off, a);
        if (EFI_ERROR(st)) return st;
    }
    return EFI_SUCCESS;
}

static EFI_STATUS finalize_segment_protection(const Elf64_Phdr *p) {
    uint64_t v = align_down(p->p_vaddr,  PAGE_SIZE_4K);
    uint64_t sz= align_up(p->p_memsz + (p->p_vaddr - v), PAGE_SIZE_4K);
    return page_apply_attr_range(v, sz, attr_from_phdr(p));
}

EFI_STATUS load_kernel_elf(CONST CHAR16 *filename, UINT64 *out_entry)
{
    if (!Root || !filename || !filename[0]) return EFI_INVALID_PARAMETER;

    EFI_STATUS st;
    EFI_FILE_HANDLE file = NULL;

    // 0) 事前準備：Lv4 を writable に
    st = page_set_lv4_writable();
    if (EFI_ERROR(st)) {
        log_printf(LOG_ERROR, L"setLv4Writable failed: %r", st);
        return st;
    }
    log_printf(LOG_DEBUG, L"Set page table writable.");

    // 1) カーネルファイルを開く
    st = uefi_call_wrapper(Root->Open, 5, Root, &file,
                           (CHAR16*)filename, EFI_FILE_MODE_READ, 0);
    if (EFI_ERROR(st) || !file) {
        log_printf(LOG_ERROR, L"Open(%s) failed: %r", filename, st);
        return st ? st : EFI_NOT_FOUND;
    }
    log_printf(LOG_INFO, L"Opened kernel file.");

    // 2) ELF ヘッダを読む・検証
    Elf64_Ehdr eh;
    st = file_read_at(file, 0, &eh, sizeof(eh));
    if (EFI_ERROR(st)) { log_printf(LOG_ERROR, L"Read ELF header failed: %r", st); goto cleanup; }

    if (!(eh.e_ident[EI_MAG0]==0x7F && eh.e_ident[EI_MAG1]=='E' &&
          eh.e_ident[EI_MAG2]=='L'  && eh.e_ident[EI_MAG3]=='F')) {
        log_printf(LOG_ERROR, L"ELF magic mismatch");
        st = EFI_LOAD_ERROR; goto cleanup;
    }
    if (eh.e_ident[EI_CLASS] != ELFCLASS64) {
        log_printf(LOG_ERROR, L"ELF is not 64-bit");
        st = EFI_UNSUPPORTED; goto cleanup;
    }

    log_printf(LOG_INFO,  L"Parsed kernel ELF header.");
    log_printf(LOG_DEBUG, L"Kernel ELF information:");
    log_printf(LOG_DEBUG, L"  Entry Point         : 0x%lx", (UINT64)eh.e_entry);
    log_printf(LOG_DEBUG, L"  # of Program Headers: %u", eh.e_phnum);

    // 3) 全 Program Header を読む
    const UINT64 ph_size = (UINT64)eh.e_phentsize * (UINT64)eh.e_phnum;
    Elf64_Phdr *phdrs = NULL;
    st = uefi_call_wrapper(BS->AllocatePool, 3, EfiLoaderData, ph_size, (VOID**)&phdrs);
    if (EFI_ERROR(st) || !phdrs) { st = st ? st : EFI_OUT_OF_RESOURCES; goto cleanup; }

    st = file_read_at(file, eh.e_phoff, phdrs, (UINTN)ph_size);
    if (EFI_ERROR(st)) { log_printf(LOG_ERROR, L"Read PHDRs failed: %r", st); goto cleanup_ph; }

    // 4) 必要な物理/仮想の範囲を計算（p_memsz==0 は無視）
    UINT64 k_phys_start = ~0ull;
    UINT64 k_phys_end   = 0ull;
    UINT64 k_virt_start = ~0ull;

    for (UINTN i = 0; i < eh.e_phnum; ++i) {
        Elf64_Phdr *p = &phdrs[i];
        if (p->p_type != PT_LOAD) continue;
        if (p->p_memsz == 0)      continue;  // ★重要：空の LOAD を無視

        UINT64 seg_phys_lo = p->p_paddr & ~(PAGE_SIZE_4K - 1); // 4KiB 丸め下げ
        UINT64 seg_phys_hi = (p->p_paddr + p->p_memsz + PAGE_SIZE_4K - 1) & ~(PAGE_SIZE_4K - 1); // 丸め上げ
        UINT64 seg_virt_lo = p->p_vaddr & ~(PAGE_SIZE_4K - 1);

        if (seg_phys_lo < k_phys_start) k_phys_start = seg_phys_lo;
        if (seg_phys_hi > k_phys_end)   k_phys_end   = seg_phys_hi;
        if (seg_virt_lo < k_virt_start) k_virt_start = seg_virt_lo;
    }

    if (k_phys_start == ~0ull) { st = EFI_LOAD_ERROR; goto cleanup_ph; }

    // 5) 連続で確保（AllocateAddress）
    UINT64 bytes = k_phys_end - k_phys_start;
    UINTN  pages = (UINTN)(bytes / PAGE_SIZE_4K);

    EFI_PHYSICAL_ADDRESS alloc_addr = (EFI_PHYSICAL_ADDRESS)k_phys_start;
    st = uefi_call_wrapper(
            BS->AllocatePages, 4,
            AllocateAddress,            // 指定アドレスちょうどで
            EfiLoaderData,              // タイプはローダ用データ
            pages,
            &alloc_addr
    );
    if (EFI_ERROR(st) || alloc_addr != (EFI_PHYSICAL_ADDRESS)k_phys_start) {
        log_printf(LOG_ERROR, L"AllocatePages(AllocateAddress) failed: %r", st);
        goto cleanup_ph;
    }
    log_printf(LOG_INFO, L"Allocated kernel image pages: start=0x%lx pages=%lu",
            k_phys_start, (UINT64)pages);

    // 6) 仮想アドレスへマップ（PT_LOADごと、p_memsz==0 は無視）
    for (UINTN i = 0; i < eh.e_phnum; ++i) {
        Elf64_Phdr *p = &phdrs[i];
        if (p->p_type != PT_LOAD) continue;
        if (p->p_memsz == 0)      continue;
        EFI_STATUS st = map_segment_for_load(p);
        if (EFI_ERROR(st)) {
            log_printf(LOG_ERROR, L"map_segment_for_load failed: %r", st);
            goto cleanup_ph;
        }
    }
    log_printf(LOG_INFO, L"Mapped memory for kernel image.");

    // 7) 各 PT_LOAD をファイルからロード + .bss を 0 埋め
    log_printf(LOG_INFO, L"Loading kernel image...");
    for (UINTN i = 0; i < eh.e_phnum; ++i) {
        Elf64_Phdr *p = &phdrs[i];
        if (p->p_type != PT_LOAD) continue;
        if (p->p_memsz == 0)      continue;

        if (p->p_filesz > 0) {
            st = file_read_at(file, p->p_offset,
                            (VOID*)(uintptr_t)p->p_paddr,
                            (UINTN)p->p_filesz);
            if (EFI_ERROR(st)) {
                log_printf(LOG_ERROR, L"Read segment failed: %r", st);
                goto cleanup_ph;
            }
        }
        // BSS を 0 埋め（p_paddr + filesz から）
        if (p->p_memsz > p->p_filesz) {
            uefi_call_wrapper(BS->SetMem, 3,
                (VOID*)(uintptr_t)(p->p_paddr + p->p_filesz),
                (UINTN)(p->p_memsz - p->p_filesz),
                0);
        }
        log_printf(LOG_INFO, L"  Seg @ 0x%lx - 0x%lx",
                (UINT64)p->p_vaddr, (UINT64)(p->p_vaddr + p->p_memsz));
    }

    for (UINTN i = 0; i < eh.e_phnum; ++i) {
        Elf64_Phdr *p = &phdrs[i];
        if (p->p_type != PT_LOAD) continue;
        EFI_STATUS st = finalize_segment_protection(p);
        if (EFI_ERROR(st)) {
            log_printf(LOG_ERROR, L"finalize_segment_protection failed: %r", st);
            goto cleanup_ph;
        }
    }


    if (out_entry) *out_entry = eh.e_entry;
    st = EFI_SUCCESS;

cleanup_ph:
    if (phdrs) uefi_call_wrapper(BS->FreePool, 1, phdrs);
cleanup:
    if (file)  uefi_call_wrapper(file->Close, 1, file);
    return st;
}
