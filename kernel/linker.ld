/* 仮想／物理レイアウト。仮想はそのまま、物理は安全な空き領域に置く */
KERNEL_VADDR_BASE = 0xFFFFFFFF80000000;
KERNEL_VADDR_TEXT = 0xFFFFFFFF80100000;

KERNEL_PHYS_TEXT  = 0x2000000;

STACK_SIZE = 0x5000;

ENTRY(kernelEntry)

PHDRS {
  text PT_LOAD;                 /* RX  */
  rodata PT_LOAD;               /* R   */
  data PT_LOAD;                 /* RW  */
  bss  PT_LOAD;                 /* RW  */

  __stackguard_upper PT_LOAD FLAGS(4); /* R  */
  __stack            PT_LOAD FLAGS(6); /* RW */
  __stackguard_lower PT_LOAD FLAGS(4); /* R  */
}

SECTIONS {
  . = KERNEL_VADDR_TEXT;

  /* text: 実行可能(RX) */
  .text ALIGN(4K)
    : AT (KERNEL_PHYS_TEXT + (ADDR(.text) - KERNEL_VADDR_TEXT))
  {
    *(.text .text.*)
    *(.init .init.*)
  } :text

  /* 例：GCC が生成する可能性のある ro セクションもまとめておく */
  .rodata ALIGN(4K)
    : AT (KERNEL_PHYS_TEXT + (ADDR(.rodata) - KERNEL_VADDR_TEXT))
  {
    *(.rodata .rodata.*)
    *(.eh_frame*)
    *(.note .note.* .note.gnu.*)
  } :rodata

  .data ALIGN(4K)
    : AT (KERNEL_PHYS_TEXT + (ADDR(.data) - KERNEL_VADDR_TEXT))
  {
    *(.data .data.*)
  } :data

  /* .bss は NOLOAD でも良いが、AT で“物理上の位置”は決めておく */
  .bss ALIGN(4K) (NOLOAD)
    : AT (KERNEL_PHYS_TEXT + (ADDR(.bss) - KERNEL_VADDR_TEXT))
  {
    *(COMMON)
    *(.bss .bss.*)
    . = ALIGN(16);
  } :bss

  /* スタックガード（上側）1ページ：読み取りのみ */
  __stackguard_upper ALIGN(4K) (NOLOAD)
    : AT (KERNEL_PHYS_TEXT + (ADDR(__stackguard_upper) - KERNEL_VADDR_TEXT))
  {
    __stackguard_upper = .;         /* ★シンボル定義 */
    . += 4K;
    __stackguard_upper_end = .;
  } :__stackguard_upper

  /* スタック本体（RW）。上端を __stack_top シンボルにしておくと便利 */
  __stack ALIGN(4K) (NOLOAD)
    : AT (KERNEL_PHYS_TEXT + (ADDR(__stack) - KERNEL_VADDR_TEXT))
  {
    __stack_bottom = .;
    . += STACK_SIZE;
    __stack_top = .;                /* 任意：上端シンボル */
  } :__stack

  /* スタックガード（下側）1ページ：読み取りのみ */
  __stackguard_lower ALIGN(4K) (NOLOAD)
    : AT (KERNEL_PHYS_TEXT + (ADDR(__stackguard_lower) - KERNEL_VADDR_TEXT))
  {
    __stackguard_lower = .;         /* ★ここが必須 */
    . += 4K;
    __stackguard_lower_end = .;
  } :__stackguard_lower
}
