#include <stdint.h>
#include <stddef.h>
#include <common.h>
#include <bootinfo.h>
#include <linux_guest.h>
#include <vm.h>
#include <log.h>
#include "bin_alloc.h"

int loadImage(void *memory, uint64_t memory_size, void *image, uint64_t image_size, uint64_t addr)
{
    if (memory_size < addr + image_size) {
        return -1;
    }

    memcpy(memory + addr, image, image_size);
    return 0;
}

int loadKernel(Vm *vm, GUEST_INFO* guest_info)
{
    uint64_t guest_mem_size = vm->guest_mem_size;
    void    *guest_mem      = vm->guest_mem;
    uint64_t bzImage_len    = guest_info->guest_size;
    void    *bzImage        = guest_info->guest_image;

    if (bzImage_len >= guest_mem_size) {
        KLOG_ERROR("vm", "guest memory is too small");
        return -1;
    }
    BootParams *bp = kmalloc(sizeof(BootParams), 0);
    KLOG_INFO("vm", "0x%llx", bp);
    KLOG_INFO("vm", "0x%llx", bzImage);
    memcpy(bp, bzImage, sizeof(BootParams));

    bp->e820_entries = 0;

    bp->hdr.setup_sects = bp->hdr.setup_sects ? bp->hdr.setup_sects : 4u;

    // Setup necessary fields
    bp->hdr.type_of_loader = 0xFF;
    bp->hdr.ext_loader_ver = 0;
    bp->hdr.loadflags.b.loaded_high = 1; // load kernel at 0x10_0000
    bp->hdr.loadflags.b.can_use_heap = 1; // use memory 0..BOOTPARAM as heap
    bp->hdr.heap_end_ptr = GUEST_LINUX_BOOTPARAM_GPA - 0x200;
    bp->hdr.loadflags.b.keep_segments = true; // we set CS/DS/SS/ES to flag segments with a base of 0.
    bp->hdr.cmd_line_ptr = GUEST_LINUX_CMDLINE_GPA;
    bp->hdr.vid_mode = 0xFFFF; // VGA (normal)
    // Setup E820 map
    linux_bootparams_addE820entry(bp, 0, GUEST_LINUX_KERNEL_BASE, E820_TYPE_RAM);
    linux_bootparams_addE820entry(
        bp,
        GUEST_LINUX_KERNEL_BASE,
        guest_mem_size - GUEST_LINUX_KERNEL_BASE,
        E820_TYPE_RAM
    );

    // Setup cmdline
    uint32_t   cmdline_max_size = (bp->hdr.cmdline_size < 256) ? bp->hdr.cmdline_size : 256;
    void      *cmdline_addr     = guest_mem + GUEST_LINUX_CMDLINE_GPA;
    char      *cmdline          = "console=ttyS0 earlyprintk=serial nokaslr";
    int        cmdline_len      = strlen(cmdline);
    memset(cmdline_addr, 0, cmdline_max_size);
    memcpy(cmdline_addr, cmdline, cmdline_len);

    // Load initrd (TODO)
    bp->hdr.ramdisk_image = 0xDEADBEEF;
    bp->hdr.ramdisk_size = 0;

    // Copy boot_params
    if (loadImage(guest_mem, guest_mem_size, bp, sizeof(BootParams), GUEST_LINUX_BOOTPARAM_GPA) != 0){
        KLOG_ERROR("vm", "Copy boot_params failed");
    };

    // Load protected-mode kernel code
    const uint64_t code_offset = get_protected_code_offset(&bp->hdr);
    const uint64_t code_size = bzImage_len - code_offset;
    if (loadImage(guest_mem, guest_mem_size, bzImage + code_offset, code_size, GUEST_LINUX_KERNEL_BASE) != 0){
        KLOG_ERROR("vm", "Load protected-mode kernel code failed");
    };

    KLOG_INFO("vm", "Guest memory region: 0x%llx - 0x%llx", 0, guest_mem_size);
    KLOG_INFO("vm", "Guest kernel code offset: 0x%llx", code_offset);
    return 0;
}