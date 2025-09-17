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
UEFI_SRC_DIR := bootloader
KERNEL_DIR   := kernel

BUILD_DIR    := build
UEFI_BUILD   := $(BUILD_DIR)/uefi
KERNEL_BUILD := $(BUILD_DIR)/kernel

IMG_DIR      := img
EFI_DIR      := $(IMG_DIR)/EFI/BOOT
EFI_NAME     := BOOTX64.EFI

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
UEFI_SRCS := $(wildcard $(UEFI_SRC_DIR)/*.c)
UEFI_OBJS := $(patsubst $(UEFI_SRC_DIR)/%.c,$(UEFI_BUILD)/%.o,$(UEFI_SRCS))

# ---- Sources / Objects (Kernel) ----
KERNEL_CSRC := $(wildcard $(KERNEL_DIR)/*.c)
KERNEL_ASMS := $(wildcard $(KERNEL_DIR)/*.S)
KERNEL_OBJS := $(patsubst $(KERNEL_DIR)/%.c,$(KERNEL_BUILD)/%.o,$(KERNEL_CSRC)) \
               $(patsubst $(KERNEL_DIR)/%.S,$(KERNEL_BUILD)/%.o,$(KERNEL_ASMS))

# ==== Flags ====
# UEFI (gnu-efi)
CFLAGS_EFI  := $(EFIINCS) \
               -fpic -ffreestanding -fno-stack-protector -fno-stack-check \
               -fshort-wchar -mno-red-zone -maccumulate-outgoing-args \
               -fno-builtin -Wall -Wextra -O2
LDFLAGS_EFI := -nostdlib -znocombreloc -shared -Bsymbolic -T $(LDS_EFI)

# Kernel (freestanding ELF64)
CFLAGS_KERNEL := -ffreestanding -fno-stack-protector -fno-builtin -mno-red-zone -O2 -Wall -Wextra
LDFLAGS_KERNEL := -nostdlib -static -z max-page-size=0x1000 -T $(KERNEL_LDS)
# 必要なら -mcmodel=kernel を追加（高位アドレスを使う場合）
# CFLAGS_KERNEL += -mcmodel=kernel

# ==== Default ====
all: efi kernel install_kernel

# ==== UEFI build pipeline ====
efi: $(UEFI_EFI)

$(UEFI_BUILD)/%.o: $(UEFI_SRC_DIR)/%.c
	@$(MKDIR_P) $(UEFI_BUILD)
	$(CC) $(CFLAGS_EFI) -c $< -o $@

$(UEFI_SO): $(UEFI_OBJS)
	$(LD) $(LDFLAGS_EFI) $(CRT0_EFI) $^ -o $@ $(LIBS_EFI)

$(UEFI_EFI): $(UEFI_SO)
	@$(MKDIR_P) $(EFI_DIR)
	$(OBJCOPY) \
	  -j .text -j .sdata -j .data -j .rodata -j .dynamic -j .dynsym \
	  -j .rel -j .rela -j .rel.* -j .rela.* -j .reloc \
	  --target efi-app-x86_64 --subsystem=10 \
	  $< $@

# ==== Kernel build pipeline ====
kernel: $(KERNEL_ELF)

$(KERNEL_BUILD)/%.o: $(KERNEL_DIR)/%.c
	@$(MKDIR_P) $(KERNEL_BUILD)
	$(CC) $(CFLAGS_KERNEL) -c $< -o $@

$(KERNEL_BUILD)/%.o: $(KERNEL_DIR)/%.S
	@$(MKDIR_P) $(KERNEL_BUILD)
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
	  -bios $(OVMF_CODE) \
	  -drive file=fat:rw:$(PWD)/$(IMG_DIR),format=raw \
	  -nographic -serial mon:stdio -no-reboot \
	  -enable-kvm -cpu host \
	  -s

clean:
	rm -rf $(BUILD_DIR) $(IMG_DIR)

.PHONY: all efi kernel install_kernel install run clean
