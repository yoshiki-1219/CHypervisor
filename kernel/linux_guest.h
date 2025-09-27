// kernel/linux_guest.h
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdbool.h>

#define SECTOR_SIZE 512
#define LINUX_SETUP_HEADER_OFFSET 0x1F1

// ---------------- E820 ----------------
typedef enum {
    E820_TYPE_RAM       = 1,
    E820_TYPE_RESERVED  = 2,
    E820_TYPE_ACPI      = 3,
    E820_TYPE_NVS       = 4,
    E820_TYPE_UNUSABLE  = 5,
} E820Type;

typedef struct __attribute__((packed)) {
    uint64_t addr;
    uint64_t size;
    E820Type type;
} E820Entry;

enum { E820_MAX = 128 };

// ---------------- Setup Header ----------------
typedef struct __attribute__((packed)) {
    uint8_t   setup_sects;
    uint16_t  root_flags;
    uint32_t  syssize;
    uint16_t  ram_size;
    uint16_t  vid_mode;
    uint16_t  root_dev;
    uint16_t  boot_flag;
    uint16_t  jump;
    uint32_t  header;
    uint16_t  version;
    uint32_t  realmode_switch;
    uint16_t  start_sys_seg;
    uint16_t  kernel_version;
    uint8_t   type_of_loader;
    union {
        uint8_t raw;
        struct {
            uint8_t loaded_high   : 1;
            uint8_t kaslr_flag    : 1;
            uint8_t _unused       : 3;
            uint8_t quiet_flag    : 1;
            uint8_t keep_segments : 1;
            uint8_t can_use_heap  : 1;
        } b;
    } loadflags;
    uint16_t  setup_move_size;
    uint32_t  code32_start;
    uint32_t  ramdisk_image;   // 32-bit linear
    uint32_t  ramdisk_size;
    uint32_t  bootsect_kludge;
    uint16_t  heap_end_ptr;    // end_of_setup/heap - 0x200
    uint8_t   ext_loader_ver;
    uint8_t   ext_loader_type;
    uint32_t  cmd_line_ptr;    // 32-bit linear
    uint32_t  initrd_addr_max;
    uint32_t  kernel_alignment;
    uint8_t   relocatable_kernel;
    uint8_t   min_alignment;
    uint16_t  xloadflags;
    uint32_t  cmdline_size;
    uint32_t  hardware_subarch;
    uint64_t  hardware_subarch_data;
    uint32_t  payload_offset;
    uint32_t  payload_length;
    uint64_t  setup_data;
    uint64_t  pref_address;
    uint32_t  init_size;
    uint32_t  handover_offset;
    uint32_t  kernel_info_offset;
} SetupHeader;


// ---------------- boot_params（必要域のみ） ----------------
typedef struct __attribute__((packed)) {
    uint8_t   _screen_info[0x40];
    uint8_t   _apm_bios_info[0x14];
    uint8_t   _pad2[4];
    uint64_t  tboot_addr;
    uint8_t   ist_info[0x10];
    uint8_t   _pad3[0x10];
    uint8_t   hd0_info[0x10];
    uint8_t   hd1_info[0x10];
    uint8_t   _sys_desc_table[0x10];
    uint8_t   _olpc_ofw_header[0x10];
    uint8_t   _pad4[0x80];
    uint8_t   _edid_info[0x80];
    uint8_t   _efi_info[0x20];
    uint32_t  alt_mem_k;
    uint32_t  scratch;
    uint8_t   e820_entries;
    uint8_t   eddbuf_entries;
    uint8_t   edd_mbr_sig_buf_entries;
    uint8_t   kbd_status;
    uint8_t   _pad6[5];
    SetupHeader hdr;
    uint8_t   _pad7[0x290 - LINUX_SETUP_HEADER_OFFSET - sizeof(SetupHeader)];
    uint32_t  _edd_mbr_sig_buffer[0x10];
    E820Entry e820_map[E820_MAX];
    uint8_t   _unimplemented[0x330];
} BootParams;

// ---------------- ゲスト物理メモリの配置（レイアウト） ----------------
enum {
    // Zero page（boot_params）のゲスト物理配置
    GUEST_LINUX_BOOTPARAM_GPA = 0x00010000u,
    // コマンドライン配置（32bit 線形で参照されるため <4GiB）
    GUEST_LINUX_CMDLINE_GPA   = 0x00020000u,
    // Protected-mode kernel を載せるベース（標準は 1MiB）
    GUEST_LINUX_KERNEL_BASE   = 0x00100000u,
    // 本チャプタでは未使用だが定義のみ
    GUEST_LINUX_INITRD_GPA    = 0x06000000u,
};

// ---------------- API ----------------

size_t get_protected_code_offset(const SetupHeader *hdr);
int linux_bootparams_addE820entry(BootParams *bp, uint64_t addr, uint64_t size, E820Type type);