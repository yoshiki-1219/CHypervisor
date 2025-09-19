# ==== Arch ====
ARCH        := x86

# ==== Tools ====
QEMU        = qemu-system-x86_64
CC          = gcc
LD          = ld
OBJCOPY     = objcopy
MKDIR_P     = mkdir -p
CP          = cp

# ==== gnu-efi ====
EFIINC      = /usr/include/efi
EFIINCS     = -I$(EFIINC) -I$(EFIINC)/x86_64
GNUEFI_LIB  = /usr/lib
GNUEFI_DIR  = $(GNUEFI_LIB)

CRT0_EFI    = $(GNUEFI_DIR)/crt0-efi-x86_64.o
LDS_EFI     = $(GNUEFI_DIR)/elf_x86_64_efi.lds
LIBS_EFI    = -L$(GNUEFI_LIB) -lgnuefi -lefi

# ==== OVMF ====
OVMF_CODE   = $(PWD)/OVMF.fd

# ==== Project layout ====
UEFI_SRC_DIR      := bootloader
UEFI_ARCH_DIR     := $(UEFI_SRC_DIR)/arch/$(ARCH)

KERNEL_DIR        := kernel
KERNEL_ARCH_DIR   := $(KERNEL_DIR)/arch/$(ARCH)
KERNEL_VMM_DIR    := $(KERNEL_DIR)/arch/$(ARCH)/vmm

BUILD_DIR         := build
UEFI_BUILD        := $(BUILD_DIR)/uefi
UEFI_BUILD_ARCH   := $(UEFI_BUILD)/arch/$(ARCH)

KERNEL_BUILD      := $(BUILD_DIR)/kernel
KERNEL_BUILD_ARCH := $(KERNEL_BUILD)/arch/$(ARCH)
KERNEL_BUILD_VMM  := $(KERNEL_BUILD)/arch/$(ARCH)/vmm

IMG_DIR           := img
EFI_DIR           := $(IMG_DIR)/EFI/BOOT
EFI_NAME          := BOOTX64.EFI

# ==== UEFI naming ====
UEFI_APPNAME := bootloader
UEFI_SO      := $(UEFI_BUILD)/$(UEFI_APPNAME).so
UEFI_EFI     := $(EFI_DIR)/$(EFI_NAME)

# ==== Kernel naming ====
KERNEL_ELF_NAME := kernel.elf
KERNEL_ELF      := $(KERNEL_BUILD)/$(KERNEL_ELF_NAME)
KERNEL_LDS      := $(KERNEL_DIR)/linker.ld
KERNEL_OUT      := $(IMG_DIR)/$(KERNEL_ELF_NAME)

# ---- Sources / Objects (UEFI) ----
UEFI_COMMON_SRCS := $(wildcard $(UEFI_SRC_DIR)/*.c)
UEFI_ARCH_SRCS   := $(wildcard $(UEFI_ARCH_DIR)/*.c)
UEFI_SRCS        := $(UEFI_COMMON_SRCS) $(UEFI_ARCH_SRCS)

UEFI_COMMON_OBJS := $(patsubst $(UEFI_SRC_DIR)/%.c,$(UEFI_BUILD)/%.o,$(UEFI_COMMON_SRCS))
UEFI_ARCH_OBJS   := $(patsubst $(UEFI_ARCH_DIR)/%.c,$(UEFI_BUILD_ARCH)/%.o,$(UEFI_ARCH_SRCS))
UEFI_OBJS        := $(UEFI_COMMON_OBJS) $(UEFI_ARCH_OBJS)

# ---- Sources / Objects (Kernel) ----
KERNEL_COMMON_CSRC := $(wildcard $(KERNEL_DIR)/*.c)
KERNEL_ARCH_CSRC   := $(wildcard $(KERNEL_ARCH_DIR)/*.c)
KERNEL_VMM_CSRC    := $(wildcard $(KERNEL_VMM_DIR)/*.c)
KERNEL_ASMS_COMMON := $(wildcard $(KERNEL_DIR)/*.S)
KERNEL_ASMS_ARCH   := $(wildcard $(KERNEL_ARCH_DIR)/*.S)
KERNEL_ASMS_VMM    := $(wildcard $(KERNEL_VMM_DIR)/*.S)

KERNEL_COMMON_OBJS := $(patsubst $(KERNEL_DIR)/%.c,$(KERNEL_BUILD)/%.o,$(KERNEL_COMMON_CSRC)) \
                      $(patsubst $(KERNEL_DIR)/%.S,$(KERNEL_BUILD)/%.o,$(KERNEL_ASMS_COMMON)) \

KERNEL_ARCH_OBJS   := $(patsubst $(KERNEL_ARCH_DIR)/%.c,$(KERNEL_BUILD_ARCH)/%.o,$(KERNEL_ARCH_CSRC)) \
                      $(patsubst $(KERNEL_ARCH_DIR)/%.S,$(KERNEL_BUILD_ARCH)/%.o,$(KERNEL_ASMS_ARCH))

KERNEL_VMM_OBJS    := $(patsubst $(KERNEL_VMM_DIR)/%.c,$(KERNEL_BUILD_VMM)/%.o,$(KERNEL_VMM_CSRC)) \
                      $(patsubst $(KERNEL_VMM_DIR)/%.S,$(KERNEL_BUILD_VMM)/%.o,$(KERNEL_ASMS_VMM))

KERNEL_OBJS := $(KERNEL_COMMON_OBJS) $(KERNEL_ARCH_OBJS) $(KERNEL_VMM_OBJS)

# ==== Flags ====
CFLAGS_EFI  := $(EFIINCS) \
               -fpic -ffreestanding -fno-stack-protector -fno-stack-check \
               -fshort-wchar -mno-red-zone -maccumulate-outgoing-args \
               -fno-builtin -Wall -Wextra -O2 \
               -I$(UEFI_SRC_DIR) -I$(UEFI_ARCH_DIR)

LDFLAGS_EFI := -nostdlib -znocombreloc -shared -Bsymbolic -T $(LDS_EFI)

CFLAGS_KERNEL := -ffreestanding -fno-stack-protector -fno-omit-frame-pointer \
                 -fno-optimize-sibling-calls -fno-builtin -mno-red-zone -O2 -Wall -Wextra \
                 -I$(KERNEL_DIR) -I$(KERNEL_ARCH_DIR)
LDFLAGS_KERNEL := -nostdlib -static -z max-page-size=0x1000 -T $(KERNEL_LDS)

# ==== Default ====
all: efi kernel install_kernel

# ==== Rules: UEFI ====
efi: $(UEFI_EFI)
# UEFI 共通 .c → build/uefi/*.o
$(UEFI_BUILD)/%.o: $(UEFI_SRC_DIR)/%.c
	@$(MKDIR_P) $(dir $@)
	$(CC) $(CFLAGS_EFI) -c $< -o $@

# UEFI アーキ別 .c → build/uefi/arch/x86/*.o
$(UEFI_BUILD_ARCH)/%.o: $(UEFI_ARCH_DIR)/%.c
	@$(MKDIR_P) $(dir $@)
	$(CC) $(CFLAGS_EFI) -c $< -o $@

$(UEFI_SO): $(UEFI_OBJS)
	$(LD) $(LDFLAGS_EFI) $(CRT0_EFI) $(filter %.o,$^) -o $@ $(LIBS_EFI)


$(UEFI_EFI): $(UEFI_SO)
	@$(MKDIR_P) $(EFI_DIR)
	$(OBJCOPY) \
	  -j .text -j .sdata -j .data -j .rodata -j .dynamic -j .dynsym \
	  -j .rel -j .rela -j .rel.* -j .rela.* -j .reloc \
	  --target efi-app-x86_64 --subsystem=10 \
	  $< $@

# ==== Rules: Kernel ====
kernel: $(KERNEL_ELF)

$(KERNEL_BUILD)/%.o: $(KERNEL_DIR)/%.c
	@$(MKDIR_P) $(dir $@)
	$(CC) $(CFLAGS_KERNEL) -c $< -o $@

$(KERNEL_BUILD)/%.o: $(KERNEL_DIR)/%.S
	@$(MKDIR_P) $(dir $@)
	$(CC) $(CFLAGS_KERNEL) -c $< -o $@

$(KERNEL_BUILD_ARCH)/%.o: $(KERNEL_ARCH_DIR)/%.c
	@$(MKDIR_P) $(dir $@)
	$(CC) $(CFLAGS_KERNEL) -c $< -o $@

$(KERNEL_BUILD_ARCH)/%.o: $(KERNEL_ARCH_DIR)/%.S
	@$(MKDIR_P) $(dir $@)
	$(CC) $(CFLAGS_KERNEL) -c $< -o $@

$(KERNEL_BUILD_VMM)/%.o: $(KERNEL_VMM_DIR)/%.c
	@$(MKDIR_P) $(dir $@)
	$(CC) $(CFLAGS_KERNEL) -c $< -o $@

$(KERNEL_BUILD_VMM)/%.o: $(KERNEL_VMM_DIR)/%.S
	@$(MKDIR_P) $(dir $@)
	$(CC) $(CFLAGS_KERNEL) -c $< -o $@

$(KERNEL_ELF): $(KERNEL_OBJS) $(KERNEL_LDS)
	@$(MKDIR_P) $(KERNEL_BUILD)
	$(LD) $(LDFLAGS_KERNEL) $(KERNEL_OBJS) -o $@


# ==== Install kernel to ESP image dir ====
install_kernel: kernel
	@$(MKDIR_P) $(IMG_DIR)
	$(CP) $(KERNEL_ELF) $(KERNEL_OUT)
	@echo "Installed kernel: $(KERNEL_OUT)"

# ==== Run ====
install: efi install_kernel
	@echo "Installed: $(UEFI_EFI)"

run: install
	sudo $(QEMU) \
	  -m 512M \
	  -snapshot \
	  -bios $(OVMF_CODE) \
	  -drive file=fat:ro:$(PWD)/$(IMG_DIR),format=raw \
	  -nographic -serial mon:stdio -no-reboot \
	  -enable-kvm -cpu host \
	  -s

clean:
	rm -rf $(BUILD_DIR) $(IMG_DIR)

.PHONY: all efi kernel install_kernel install run clean
