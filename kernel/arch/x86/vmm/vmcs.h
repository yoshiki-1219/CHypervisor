#pragma once
#include <stdint.h>
#include <stddef.h>
#include "arch/x86/msr.h"        /* IA32_VMX_* MSR ids, rdmsr() */
#include "arch/x86/arch_x86_low.h"
#include "arch/x86/vmm/vcpu.h"

/* =========================== 概要 ===========================
 * - VMCS Region の確保・初期化（revisionID 設定 → VMCLEAR → VMPTRLD）
 * - VMREAD / VMWRITE（RFLAGS を検査して成否を返す）
 * - VM-instruction error の読み出し（VMCSフィールド 0x4400）
 * - ★ VMCS-field Encoding 生成ヘルパ（Type/Width/Access/Index から 32bit を生成）
 * =========================================================== */

/* ---- 公開 API（vmcs.c に実装済み） ---- */
int      vmcs_alloc_and_load(Vcpu* vcpu);               /* 0:ok/-1:ng */
int      vmcs_vmwrite(uint64_t field, uint64_t value);          /* 0:ok/-1:ng */
int      vmcs_vmread(uint64_t field, uint64_t* out_value);      /* 0:ok/-1:ng */

/* ===========================================================
 * VMCS-field Encoding
 *   参考: Intel SDM Vol.3C 25.11.2 Table 25-21
 *
 *  [0]     Access Type: 0=full, 1=high(64bitフィールド上位32bit)
 *  [9:1]   Index (0..511)
 *  [11:10] Field Type: 0=control, 1=vmexit(read-only), 2=guest, 3=host
 *  [13:12] Width: 0=16bit(word), 1=64bit(qword), 2=32bit(dword), 3=natural
 *  [31:14] reserved=0
 * =========================================================== */

/* Access */
#define VMCS_ACCESS_FULL   0u
#define VMCS_ACCESS_HIGH   1u

/* Width */
#define VMCS_WIDTH_WORD     0u  /* 16-bit   */
#define VMCS_WIDTH_QWORD    1u  /* 64-bit   */
#define VMCS_WIDTH_DWORD    2u  /* 32-bit   */
#define VMCS_WIDTH_NATURAL  3u  /* 32/64    */

/* Type */
#define VMCS_TYPE_CONTROL    0u
#define VMCS_TYPE_VMEXIT_RO  1u  /* read-only */
#define VMCS_TYPE_GUEST      2u
#define VMCS_TYPE_HOST       3u

/* 32bit encoding layout:
 *  bit0      : access (0=full,1=high)
 *  bits9..1  : index (9 bits)
 *  bits11..10: type (2 bits)
 *  bits13..12: width (2 bits)
 *  bits31..14: 0
 */

#define VMCS_ENCODE(type, index, access, width) \
    ( ((uint32_t)(access) & 0x1u)                            | \
      ((((uint32_t)(index)) & 0x1FFu) << 1)                  | \
      ((((uint32_t)(type))  & 0x3u)   << 10)                 | \
      (0u /* reserved bit12 = 0 */)                          | \
      ((((uint32_t)(width)) & 0x3u)   << 13) )

/* Zig の eg/eh/ec/er 相当のショートカット */
#define VMCS_EG(idx, acc, w) VMCS_ENCODE(VMCS_TYPE_GUEST,     (idx), (acc), (w))
#define VMCS_EH(idx, acc, w) VMCS_ENCODE(VMCS_TYPE_HOST,      (idx), (acc), (w))
#define VMCS_EC(idx, acc, w) VMCS_ENCODE(VMCS_TYPE_CONTROL,   (idx), (acc), (w))
#define VMCS_ER(idx, acc, w) VMCS_ENCODE(VMCS_TYPE_VMEXIT_RO, (idx), (acc), (w))

/* 書きやすい別名（任意） */
#define VMCS_FULL  VMCS_ACCESS_FULL
#define VMCS_HIGH  VMCS_ACCESS_HIGH
#define VMCS_W     VMCS_WIDTH_WORD
#define VMCS_Q     VMCS_WIDTH_QWORD
#define VMCS_D     VMCS_WIDTH_DWORD
#define VMCS_N     VMCS_WIDTH_NATURAL

/* ===========================================================
 * Controls (完全版)
 *   cf. SDM Vol.3C 25.4, Appendix B
 * =========================================================== */

/* Natural width */
enum {
    VMCS_CR0_MASK                        = VMCS_EC(0,  VMCS_FULL, VMCS_N), /* 0x6000 */
    VMCS_CR4_MASK                        = VMCS_EC(1,  VMCS_FULL, VMCS_N), /* 0x6002 */
    VMCS_CR0_READ_SHADOW                 = VMCS_EC(2,  VMCS_FULL, VMCS_N), /* 0x6004 */
    VMCS_CR4_READ_SHADOW                 = VMCS_EC(3,  VMCS_FULL, VMCS_N), /* 0x6006 */
    VMCS_CR3_TARGET_VALUE0               = VMCS_EC(4,  VMCS_FULL, VMCS_N), /* 0x6008 */
    VMCS_CR3_TARGET_VALUE1               = VMCS_EC(5,  VMCS_FULL, VMCS_N), /* 0x600A */
    VMCS_CR3_TARGET_VALUE2               = VMCS_EC(6,  VMCS_FULL, VMCS_N), /* 0x600C */
    VMCS_CR3_TARGET_VALUE3               = VMCS_EC(7,  VMCS_FULL, VMCS_N), /* 0x600E */
};

/* 16-bit */
enum {
    VMCS_VPID                            = VMCS_EC(0, VMCS_FULL, VMCS_W), /* 0x0000 */
    VMCS_POSTED_INTR_NOTIFICATION_VECTOR = VMCS_EC(1, VMCS_FULL, VMCS_W), /* 0x0002 */
    VMCS_EPTP_INDEX                      = VMCS_EC(2, VMCS_FULL, VMCS_W), /* 0x0004 */
    VMCS_HLAT_PREFIX_SIZE                = VMCS_EC(3, VMCS_FULL, VMCS_W), /* 0x0006 */
    VMCS_PID_POINTER_INDEX               = VMCS_EC(4, VMCS_FULL, VMCS_W), /* 0x0008 */
};

/* 32-bit */
enum {
    VMCS_PIN_BASED_CTLS                  = VMCS_EC(0,  VMCS_FULL, VMCS_D), /* 0x4000 */
    VMCS_PRIMARY_PROC_CTLS               = VMCS_EC(1,  VMCS_FULL, VMCS_D), /* 0x4002 */
    VMCS_EXCEPTION_BITMAP                = VMCS_EC(2,  VMCS_FULL, VMCS_D), /* 0x4004 */
    VMCS_PAGE_FAULT_ERROR_CODE_MASK      = VMCS_EC(3,  VMCS_FULL, VMCS_D), /* 0x4006 */
    VMCS_PAGE_FAULT_ERROR_CODE_MATCH     = VMCS_EC(4,  VMCS_FULL, VMCS_D), /* 0x4008 */
    VMCS_CR3_TARGET_COUNT                = VMCS_EC(5,  VMCS_FULL, VMCS_D), /* 0x400A */
    VMCS_VMEXIT_CTLS                     = VMCS_EC(6,  VMCS_FULL, VMCS_D), /* 0x400C */
    VMCS_VMEXIT_MSR_STORE_COUNT          = VMCS_EC(7,  VMCS_FULL, VMCS_D), /* 0x400E */
    VMCS_VMEXIT_MSR_LOAD_COUNT           = VMCS_EC(8,  VMCS_FULL, VMCS_D), /* 0x4010 */
    VMCS_VMENTRY_CTLS                    = VMCS_EC(9,  VMCS_FULL, VMCS_D), /* 0x4012 */
    VMCS_VMENTRY_MSR_LOAD_COUNT          = VMCS_EC(10, VMCS_FULL, VMCS_D), /* 0x4014 */
    VMCS_VMENTRY_INTERRUPT_INFO          = VMCS_EC(11, VMCS_FULL, VMCS_D), /* 0x4016 */
    VMCS_VMENTRY_EXCEPTION_ERROR_CODE    = VMCS_EC(12, VMCS_FULL, VMCS_D), /* 0x4018 */
    VMCS_VMENTRY_INSTRUCTION_LENGTH      = VMCS_EC(13, VMCS_FULL, VMCS_D), /* 0x401A */
    VMCS_TPR_THRESHOLD                   = VMCS_EC(14, VMCS_FULL, VMCS_D), /* 0x401C */
    VMCS_SECONDARY_PROC_CTLS             = VMCS_EC(15, VMCS_FULL, VMCS_D), /* 0x401E */
    VMCS_PLE_GAP                         = VMCS_EC(16, VMCS_FULL, VMCS_D), /* 0x4020 */
    VMCS_PLE_WINDOW                      = VMCS_EC(17, VMCS_FULL, VMCS_D), /* 0x4022 */
    VMCS_INSTRUCTION_TIMEOUTS            = VMCS_EC(18, VMCS_FULL, VMCS_D), /* 0x4024 */
};

/* 64-bit */
enum {
    VMCS_IO_BITMAP_A                     = VMCS_EC(0,  VMCS_FULL, VMCS_Q), /* 0x2000 */
    VMCS_IO_BITMAP_B                     = VMCS_EC(1,  VMCS_FULL, VMCS_Q), /* 0x2002 */
    VMCS_MSR_BITMAPS                     = VMCS_EC(2,  VMCS_FULL, VMCS_Q), /* 0x2004 */
    VMCS_VMEXIT_MSR_STORE_ADDR           = VMCS_EC(3,  VMCS_FULL, VMCS_Q), /* 0x2006 */
    VMCS_VMEXIT_MSR_LOAD_ADDR            = VMCS_EC(4,  VMCS_FULL, VMCS_Q), /* 0x2008 */
    VMCS_VMENTRY_MSR_LOAD_ADDR           = VMCS_EC(5,  VMCS_FULL, VMCS_Q), /* 0x200A */
    VMCS_EXECUTIVE_VMCS_POINTER          = VMCS_EC(6,  VMCS_FULL, VMCS_Q), /* 0x200C */
    VMCS_PML_ADDRESS                     = VMCS_EC(7,  VMCS_FULL, VMCS_Q), /* 0x200E */
    VMCS_TSC_OFFSET                      = VMCS_EC(8,  VMCS_FULL, VMCS_Q), /* 0x2010 */
    VMCS_VIRTUAL_APIC_ADDRESS            = VMCS_EC(9,  VMCS_FULL, VMCS_Q), /* 0x2012 */
    VMCS_APIC_ACCESS_ADDRESS             = VMCS_EC(10, VMCS_FULL, VMCS_Q), /* 0x2014 */
    VMCS_POSTED_INTR_DESC_ADDR           = VMCS_EC(11, VMCS_FULL, VMCS_Q), /* 0x2016 */
    VMCS_VM_FUNCTION_CTRL                = VMCS_EC(12, VMCS_FULL, VMCS_Q), /* 0x2018 */
    VMCS_EPTP                            = VMCS_EC(13, VMCS_FULL, VMCS_Q), /* 0x201A */
    VMCS_EOI_EXIT_BITMAP0                = VMCS_EC(14, VMCS_FULL, VMCS_Q), /* 0x201C */
    VMCS_EOI_EXIT_BITMAP1                = VMCS_EC(15, VMCS_FULL, VMCS_Q), /* 0x201E */
    VMCS_EOI_EXIT_BITMAP2                = VMCS_EC(16, VMCS_FULL, VMCS_Q), /* 0x2020 */
    VMCS_EOI_EXIT_BITMAP3                = VMCS_EC(17, VMCS_FULL, VMCS_Q), /* 0x2022 */
    VMCS_EPTP_LIST_ADDRESS               = VMCS_EC(18, VMCS_FULL, VMCS_Q), /* 0x2024 */
    VMCS_VMREAD_BITMAP                   = VMCS_EC(19, VMCS_FULL, VMCS_Q), /* 0x2026 */
    VMCS_VMWRITE_BITMAP                  = VMCS_EC(20, VMCS_FULL, VMCS_Q), /* 0x2028 */
    VMCS_VEXCEPTION_INFO_ADDR            = VMCS_EC(21, VMCS_FULL, VMCS_Q), /* 0x202A */
    VMCS_XSS_EXITING_BITMAP              = VMCS_EC(22, VMCS_FULL, VMCS_Q), /* 0x202C */
    VMCS_ENCLS_EXITING_BITMAP            = VMCS_EC(23, VMCS_FULL, VMCS_Q), /* 0x202E */
    VMCS_SUBPAGE_PERM_TABLE_PTR          = VMCS_EC(24, VMCS_FULL, VMCS_Q), /* 0x2030 */
    VMCS_TSC_MULTIPLIER                  = VMCS_EC(25, VMCS_FULL, VMCS_Q), /* 0x2032 */
    VMCS_TERTIARY_PROC_CTLS              = VMCS_EC(26, VMCS_FULL, VMCS_Q), /* 0x2034 */
    VMCS_ENCLV_EXITING_BITMAP            = VMCS_EC(27, VMCS_FULL, VMCS_Q), /* 0x2036 */
    VMCS_LOW_PASID_DIRECTORY             = VMCS_EC(28, VMCS_FULL, VMCS_Q), /* 0x2038 */
    VMCS_HIGH_PASID_DIRECTORY            = VMCS_EC(29, VMCS_FULL, VMCS_Q), /* 0x203A */
    VMCS_SHARED_EPTP                     = VMCS_EC(30, VMCS_FULL, VMCS_Q), /* 0x203C */
    VMCS_PCONFIG_EXITING_BITMAP          = VMCS_EC(31, VMCS_FULL, VMCS_Q), /* 0x203E */
    VMCS_HLATP                           = VMCS_EC(32, VMCS_FULL, VMCS_Q), /* 0x2040 */
    VMCS_PID_POINTER_TABLE               = VMCS_EC(33, VMCS_FULL, VMCS_Q), /* 0x2042 */
    VMCS_SECONDARY_VMEXIT_CTLS           = VMCS_EC(34, VMCS_FULL, VMCS_Q), /* 0x2044 */
    /* 35,36 は予約の版あり */
    VMCS_SPEC_CTRL_MASK                  = VMCS_EC(37, VMCS_FULL, VMCS_Q), /* 0x204A */
    VMCS_SPEC_CTRL_SHADOW                = VMCS_EC(38, VMCS_FULL, VMCS_Q), /* 0x204C */
};

/* ===========================================================
 * Read-only (VM-exit information) 完全版
 *   cf. SDM Vol.3C 25.4, Appendix B
 * =========================================================== */

/* Natural width */
enum {
    VMCS_EXIT_QUALIFICATION              = VMCS_ER(0,  VMCS_FULL, VMCS_N), /* 0x6400 */
    VMCS_IO_RCX                          = VMCS_ER(1,  VMCS_FULL, VMCS_N), /* 0x6402 */
    VMCS_IO_RSI                          = VMCS_ER(2,  VMCS_FULL, VMCS_N), /* 0x6404 */
    VMCS_IO_RDI                          = VMCS_ER(3,  VMCS_FULL, VMCS_N), /* 0x6406 */
    VMCS_IO_RIP                          = VMCS_ER(4,  VMCS_FULL, VMCS_N), /* 0x6408 */
    VMCS_GUEST_LINEAR_ADDRESS            = VMCS_ER(5,  VMCS_FULL, VMCS_N), /* 0x640A */
};

/* 32-bit */
enum {
    VMCS_VM_INSTRUCTION_ERROR            = VMCS_ER(0,  VMCS_FULL, VMCS_D), /* 0x4400 */
    VMCS_EXIT_REASON                     = VMCS_ER(1,  VMCS_FULL, VMCS_D), /* 0x4402 */
    VMCS_EXIT_INTERRUPT_INFO             = VMCS_ER(2,  VMCS_FULL, VMCS_D), /* 0x4404 */
    VMCS_EXIT_INTERRUPT_ERROR_CODE       = VMCS_ER(3,  VMCS_FULL, VMCS_D), /* 0x4406 */
    VMCS_IDT_VECTORING_INFO              = VMCS_ER(4,  VMCS_FULL, VMCS_D), /* 0x4408 */
    VMCS_IDT_VECTORING_ERROR_CODE        = VMCS_ER(5,  VMCS_FULL, VMCS_D), /* 0x440A */
    VMCS_EXIT_INSTRUCTION_LENGTH         = VMCS_ER(6,  VMCS_FULL, VMCS_D), /* 0x440C */
    VMCS_VMX_INSTRUCTION_INFO            = VMCS_ER(7,  VMCS_FULL, VMCS_D), /* 0x440E */
};

/* 64-bit */
enum {
    VMCS_GUEST_PHYSICAL_ADDRESS          = VMCS_ER(0,  VMCS_FULL, VMCS_Q), /* 0x2400 */
};

/* ===========================================================
 * Guest state (完全版)
 *   cf. SDM Vol.3C 25.4, Appendix B
 * =========================================================== */

#define AR_TYPE(x)   ((uint32_t)((x) & 0xF))    /* bits 0-3 */
#define AR_S_CODEDATA (1u<<4)                   /* S=1 */
#define AR_S_SYSTEM   (0u<<4)                   /* S=0 */
#define AR_DPL(n)    ((uint32_t)(((n)&0x3)<<5)) /* DPL bits 5-6 */
#define AR_P         (1u<<7)                    /* Present */
#define AR_AVL       (1u<<12)
#define AR_L         (1u<<13)                   /* 64-bit */
#define AR_DB        (1u<<14)                   /* D/B */
#define AR_G         (1u<<15)                   /* Granularity(4KB) */
#define AR_UNUSABLE  (1u<<16)

/* Type（下位4bit）：コード/データは “Accessed, RW, DC/ED, Exec” の並び */
#define TYPE_CODE_ER_AC   AR_TYPE(0xB) /* 1011: Code Execute/Read, Accessed=1 */
#define TYPE_DATA_RW_AC   AR_TYPE(0x3) /* 0011: Data Read/Write, Accessed=1  */

/* System Type（下位4bitは固定値） */
#define TYPE_LDT          AR_TYPE(0x2)
#define TYPE_TSS_BUSY     AR_TYPE(0xB)

/* Natural width */
enum {
    VMCS_GUEST_CR0                      = VMCS_EG(0,  VMCS_FULL, VMCS_N), /* 0x6800 */
    VMCS_GUEST_CR3                      = VMCS_EG(1,  VMCS_FULL, VMCS_N), /* 0x6802 */
    VMCS_GUEST_CR4                      = VMCS_EG(2,  VMCS_FULL, VMCS_N), /* 0x6804 */
    VMCS_GUEST_ES_BASE                  = VMCS_EG(3,  VMCS_FULL, VMCS_N), /* 0x6806 */
    VMCS_GUEST_CS_BASE                  = VMCS_EG(4,  VMCS_FULL, VMCS_N), /* 0x6808 */
    VMCS_GUEST_SS_BASE                  = VMCS_EG(5,  VMCS_FULL, VMCS_N), /* 0x680A */
    VMCS_GUEST_DS_BASE                  = VMCS_EG(6,  VMCS_FULL, VMCS_N), /* 0x680C */
    VMCS_GUEST_FS_BASE                  = VMCS_EG(7,  VMCS_FULL, VMCS_N), /* 0x680E */
    VMCS_GUEST_GS_BASE                  = VMCS_EG(8,  VMCS_FULL, VMCS_N), /* 0x6810 */
    VMCS_GUEST_LDTR_BASE                = VMCS_EG(9,  VMCS_FULL, VMCS_N), /* 0x6812 */
    VMCS_GUEST_TR_BASE                  = VMCS_EG(10, VMCS_FULL, VMCS_N), /* 0x6814 */
    VMCS_GUEST_GDTR_BASE                = VMCS_EG(11, VMCS_FULL, VMCS_N), /* 0x6816 */
    VMCS_GUEST_IDTR_BASE                = VMCS_EG(12, VMCS_FULL, VMCS_N), /* 0x6818 */
    VMCS_GUEST_DR7                      = VMCS_EG(13, VMCS_FULL, VMCS_N), /* 0x681A */
    VMCS_GUEST_RSP                      = VMCS_EG(14, VMCS_FULL, VMCS_N), /* 0x681C */
    VMCS_GUEST_RIP                      = VMCS_EG(15, VMCS_FULL, VMCS_N), /* 0x681E */
    VMCS_GUEST_RFLAGS                   = VMCS_EG(16, VMCS_FULL, VMCS_N), /* 0x6820 */
    VMCS_GUEST_PENDING_DBG_EXC          = VMCS_EG(17, VMCS_FULL, VMCS_N), /* 0x6822 */
    VMCS_GUEST_SYSENTER_ESP             = VMCS_EG(18, VMCS_FULL, VMCS_N), /* 0x6824 */
    VMCS_GUEST_SYSENTER_EIP             = VMCS_EG(19, VMCS_FULL, VMCS_N), /* 0x6826 */
    VMCS_GUEST_S_CET                    = VMCS_EG(20, VMCS_FULL, VMCS_N), /* 0x6828 */
    VMCS_GUEST_SSP                      = VMCS_EG(21, VMCS_FULL, VMCS_N), /* 0x682A */
    VMCS_GUEST_INTR_SSP_TABLE_ADDR      = VMCS_EG(22, VMCS_FULL, VMCS_N), /* 0x682C */
};

/* 16-bit selectors/その他16-bit */
enum {
    VMCS_GUEST_ES_SELECTOR              = VMCS_EG(0,  VMCS_FULL, VMCS_W), /* 0x0800 */
    VMCS_GUEST_CS_SELECTOR              = VMCS_EG(1,  VMCS_FULL, VMCS_W), /* 0x0802 */
    VMCS_GUEST_SS_SELECTOR              = VMCS_EG(2,  VMCS_FULL, VMCS_W), /* 0x0804 */
    VMCS_GUEST_DS_SELECTOR              = VMCS_EG(3,  VMCS_FULL, VMCS_W), /* 0x0806 */
    VMCS_GUEST_FS_SELECTOR              = VMCS_EG(4,  VMCS_FULL, VMCS_W), /* 0x0808 */
    VMCS_GUEST_GS_SELECTOR              = VMCS_EG(5,  VMCS_FULL, VMCS_W), /* 0x080A */
    VMCS_GUEST_LDTR_SELECTOR            = VMCS_EG(6,  VMCS_FULL, VMCS_W), /* 0x080C */
    VMCS_GUEST_TR_SELECTOR              = VMCS_EG(7,  VMCS_FULL, VMCS_W), /* 0x080E */
    VMCS_GUEST_INTERRUPT_STATUS         = VMCS_EG(8,  VMCS_FULL, VMCS_W), /* 0x0810 */
    VMCS_GUEST_PML_INDEX                = VMCS_EG(9,  VMCS_FULL, VMCS_W), /* 0x0812 */
    VMCS_GUEST_UINV                     = VMCS_EG(10, VMCS_FULL, VMCS_W), /* 0x0814 */
};

/* 32-bit limits / rights / 状態 */
enum {
    /* Limits */
    VMCS_GUEST_ES_LIMIT                 = VMCS_EG(0,  VMCS_FULL, VMCS_D), /* 0x4800 */
    VMCS_GUEST_CS_LIMIT                 = VMCS_EG(1,  VMCS_FULL, VMCS_D), /* 0x4802 */
    VMCS_GUEST_SS_LIMIT                 = VMCS_EG(2,  VMCS_FULL, VMCS_D), /* 0x4804 */
    VMCS_GUEST_DS_LIMIT                 = VMCS_EG(3,  VMCS_FULL, VMCS_D), /* 0x4806 */
    VMCS_GUEST_FS_LIMIT                 = VMCS_EG(4,  VMCS_FULL, VMCS_D), /* 0x4808 */
    VMCS_GUEST_GS_LIMIT                 = VMCS_EG(5,  VMCS_FULL, VMCS_D), /* 0x480A */
    VMCS_GUEST_LDTR_LIMIT               = VMCS_EG(6,  VMCS_FULL, VMCS_D), /* 0x480C */
    VMCS_GUEST_TR_LIMIT                 = VMCS_EG(7,  VMCS_FULL, VMCS_D), /* 0x480E */
    VMCS_GUEST_GDTR_LIMIT               = VMCS_EG(8,  VMCS_FULL, VMCS_D), /* 0x4810 */
    VMCS_GUEST_IDTR_LIMIT               = VMCS_EG(9,  VMCS_FULL, VMCS_D), /* 0x4812 */

    /* Access rights bytes */
    VMCS_GUEST_ES_ACCESS_RIGHTS         = VMCS_EG(10, VMCS_FULL, VMCS_D), /* 0x4814 */
    VMCS_GUEST_CS_ACCESS_RIGHTS         = VMCS_EG(11, VMCS_FULL, VMCS_D), /* 0x4816 */
    VMCS_GUEST_SS_ACCESS_RIGHTS         = VMCS_EG(12, VMCS_FULL, VMCS_D), /* 0x4818 */
    VMCS_GUEST_DS_ACCESS_RIGHTS         = VMCS_EG(13, VMCS_FULL, VMCS_D), /* 0x481A */
    VMCS_GUEST_FS_ACCESS_RIGHTS         = VMCS_EG(14, VMCS_FULL, VMCS_D), /* 0x481C */
    VMCS_GUEST_GS_ACCESS_RIGHTS         = VMCS_EG(15, VMCS_FULL, VMCS_D), /* 0x481E */
    VMCS_GUEST_LDTR_ACCESS_RIGHTS       = VMCS_EG(16, VMCS_FULL, VMCS_D), /* 0x4820 */
    VMCS_GUEST_TR_ACCESS_RIGHTS         = VMCS_EG(17, VMCS_FULL, VMCS_D), /* 0x4822 */

    /* 状態 */
    VMCS_GUEST_INTERRUPTIBILITY_STATE   = VMCS_EG(18, VMCS_FULL, VMCS_D), /* 0x4824 */
    VMCS_GUEST_ACTIVITY_STATE           = VMCS_EG(19, VMCS_FULL, VMCS_D), /* 0x4826 */
    VMCS_GUEST_SMBASE                   = VMCS_EG(20, VMCS_FULL, VMCS_D), /* 0x4828 */
    VMCS_GUEST_SYSENTER_CS              = VMCS_EG(21, VMCS_FULL, VMCS_D), /* 0x482A */
    VMCS_GUEST_PREEMPTION_TIMER         = VMCS_EG(22, VMCS_FULL, VMCS_D), /* 0x482C */
};

/* 64-bit */
enum {
    VMCS_GUEST_VMCS_LINK_POINTER        = VMCS_EG(0,  VMCS_FULL, VMCS_Q), /* 0x2800 */
    VMCS_GUEST_IA32_DEBUGCTL            = VMCS_EG(1,  VMCS_FULL, VMCS_Q), /* 0x2802 */
    VMCS_GUEST_IA32_PAT                 = VMCS_EG(2,  VMCS_FULL, VMCS_Q), /* 0x2804 */
    VMCS_GUEST_IA32_EFER                = VMCS_EG(3,  VMCS_FULL, VMCS_Q), /* 0x2806 */
    VMCS_GUEST_IA32_PERF_GLOBAL_CTRL    = VMCS_EG(4,  VMCS_FULL, VMCS_Q), /* 0x2808 */
    VMCS_GUEST_PDPTE0                   = VMCS_EG(5,  VMCS_FULL, VMCS_Q), /* 0x280A */
    VMCS_GUEST_PDPTE1                   = VMCS_EG(6,  VMCS_FULL, VMCS_Q), /* 0x280C */
    VMCS_GUEST_PDPTE2                   = VMCS_EG(7,  VMCS_FULL, VMCS_Q), /* 0x280E */
    VMCS_GUEST_PDPTE3                   = VMCS_EG(8,  VMCS_FULL, VMCS_Q), /* 0x2810 */
    VMCS_GUEST_IA32_BNDCFGS             = VMCS_EG(9,  VMCS_FULL, VMCS_Q), /* 0x2812 */
    VMCS_GUEST_IA32_RTIT_CTL            = VMCS_EG(10, VMCS_FULL, VMCS_Q), /* 0x2814 */
    VMCS_GUEST_IA32_LBR_CTL             = VMCS_EG(11, VMCS_FULL, VMCS_Q), /* 0x2816 */
    VMCS_GUEST_IA32_PKRS                = VMCS_EG(12, VMCS_FULL, VMCS_Q), /* 0x2818 */
};

/* ===========================================================
 * Host state (完全版)
 *   cf. SDM Vol.3C 25.4, Appendix B
 * =========================================================== */

/* Natural width */
enum {
    VMCS_HOST_CR0                        = VMCS_EH(0,  VMCS_FULL, VMCS_N), /* 0x6C00 */
    VMCS_HOST_CR3                        = VMCS_EH(1,  VMCS_FULL, VMCS_N), /* 0x6C02 */
    VMCS_HOST_CR4                        = VMCS_EH(2,  VMCS_FULL, VMCS_N), /* 0x6C04 */
    VMCS_HOST_FS_BASE                    = VMCS_EH(3,  VMCS_FULL, VMCS_N), /* 0x6C06 */
    VMCS_HOST_GS_BASE                    = VMCS_EH(4,  VMCS_FULL, VMCS_N), /* 0x6C08 */
    VMCS_HOST_TR_BASE                    = VMCS_EH(5,  VMCS_FULL, VMCS_N), /* 0x6C0A */
    VMCS_HOST_GDTR_BASE                  = VMCS_EH(6,  VMCS_FULL, VMCS_N), /* 0x6C0C */
    VMCS_HOST_IDTR_BASE                  = VMCS_EH(7,  VMCS_FULL, VMCS_N), /* 0x6C0E */
    VMCS_HOST_SYSENTER_ESP               = VMCS_EH(8,  VMCS_FULL, VMCS_N), /* 0x6C10 */
    VMCS_HOST_SYSENTER_EIP               = VMCS_EH(9,  VMCS_FULL, VMCS_N), /* 0x6C12 */
    VMCS_HOST_RSP                        = VMCS_EH(10, VMCS_FULL, VMCS_N), /* 0x6C14 */
    VMCS_HOST_RIP                        = VMCS_EH(11, VMCS_FULL, VMCS_N), /* 0x6C16 */
    VMCS_HOST_S_CET                      = VMCS_EH(12, VMCS_FULL, VMCS_N), /* 0x6C18 */
    VMCS_HOST_SSP                        = VMCS_EH(13, VMCS_FULL, VMCS_N), /* 0x6C1A */
    VMCS_HOST_INTR_SSP_TABLE_ADDR        = VMCS_EH(14, VMCS_FULL, VMCS_N), /* 0x6C1C */
};

/* 16-bit selectors */
enum {
    VMCS_HOST_ES_SELECTOR                = VMCS_EH(0, VMCS_FULL, VMCS_W), /* 0x0C00 */
    VMCS_HOST_CS_SELECTOR                = VMCS_EH(1, VMCS_FULL, VMCS_W), /* 0x0C02 */
    VMCS_HOST_SS_SELECTOR                = VMCS_EH(2, VMCS_FULL, VMCS_W), /* 0x0C04 */
    VMCS_HOST_DS_SELECTOR                = VMCS_EH(3, VMCS_FULL, VMCS_W), /* 0x0C06 */
    VMCS_HOST_FS_SELECTOR                = VMCS_EH(4, VMCS_FULL, VMCS_W), /* 0x0C08 */
    VMCS_HOST_GS_SELECTOR                = VMCS_EH(5, VMCS_FULL, VMCS_W), /* 0x0C0A */
    VMCS_HOST_TR_SELECTOR                = VMCS_EH(6, VMCS_FULL, VMCS_W), /* 0x0C0C */
};

/* 32-bit */
enum {
    VMCS_HOST_SYSENTER_CS                = VMCS_EH(0, VMCS_FULL, VMCS_D), /* 0x4C00 */
};

/* 64-bit */
enum {
    VMCS_HOST_IA32_PAT                   = VMCS_EH(0, VMCS_FULL, VMCS_Q), /* 0x2C00 */
    VMCS_HOST_IA32_EFER                  = VMCS_EH(1, VMCS_FULL, VMCS_Q), /* 0x2C02 */
    VMCS_HOST_IA32_PERF_GLOBAL_CTRL      = VMCS_EH(2, VMCS_FULL, VMCS_Q), /* 0x2C04 */
    VMCS_HOST_IA32_PKRS                  = VMCS_EH(3, VMCS_FULL, VMCS_Q), /* 0x2C06 */
};

/* ===========================================================
 * VMX Execution Controls
 *   - 安全のため bitfield は使わず、raw + マスクで操作
 *   - 調整(Allowed-0/1)は helper で自動適用
 * =========================================================== */

/* ---------- helpers (same semantics as your .c) ---------- */

static inline uint32_t vmx_adjust_ctrl_u32(uint32_t val, uint64_t msr)
{
    uint32_t must_be_one = (uint32_t)(msr & 0xFFFFFFFFu);
    uint32_t must_be_zero= (uint32_t)(msr >> 32);
    val |= must_be_one;
    val &= must_be_zero;
    return val;
}

/* IA32_VMX_BASIC の true_controls 有無で参照する制御 MSR を選ぶ */
static inline uint64_t vmx_pick_ctrl_msr(uint32_t msr_true, uint32_t msr_legacy)
{
    ia32_vmx_basic_t basic; basic.u64 = rdmsr(IA32_VMX_BASIC);
    if (basic.true_controls) {
        return rdmsr(msr_true);
    } else {
        return rdmsr(msr_legacy);
    }
}


/* ---------- Pin-based Execution Controls ---------- */

typedef struct { uint32_t raw; } vmx_pin_exec_ctrl_t;

/* Bits (SDM Vol.3C 25.6 “Pin-Based VM-Execution Controls”) */
#define PIN_EXEC_EXT_INT                 (1u << 0)
#define PIN_EXEC_NMI                     (1u << 3)
#define PIN_EXEC_VIRTUAL_NMI             (1u << 5)
#define PIN_EXEC_VMX_PREEMPT_TIMER       (1u << 6)
#define PIN_EXEC_POSTED_INTERRUPTS       (1u << 7)

static inline int vmx_pin_exec_read(vmx_pin_exec_ctrl_t *out)
{
    uint64_t v; int rc = vmcs_vmread(VMCS_PIN_BASED_CTLS, &v);
    if (rc==0) out->raw = (uint32_t)v;
    return rc;
}

static inline int vmx_pin_exec_write(vmx_pin_exec_ctrl_t v)
{
    return vmcs_vmwrite(VMCS_PIN_BASED_CTLS, v.raw);
}

static inline int vmx_pin_exec_commit_adjusted(vmx_pin_exec_ctrl_t v)
{
    uint64_t msr = vmx_pick_ctrl_msr(IA32_VMX_TRUE_PINBASED_CTLS, IA32_VMX_PINBASED_CTLS);
    uint32_t adj = vmx_adjust_ctrl_u32(v.raw, msr);
    return vmcs_vmwrite(VMCS_PIN_BASED_CTLS, adj);
}

/* ---------- Primary Processor-Based Execution Controls ---------- */

typedef struct { uint32_t raw; } vmx_primary_exec_ctrl_t;

/* Bits (SDM Vol.3C 25.6 “Primary Processor-Based VM-Execution Controls”) */
#define PRIM_EXEC_INTR_WINDOW            (1u << 2)
#define PRIM_EXEC_TSC_OFFSET             (1u << 3)
#define PRIM_EXEC_HLT                    (1u << 7)
#define PRIM_EXEC_INVLPG                 (1u << 9)
#define PRIM_EXEC_MWAIT                  (1u << 10)
#define PRIM_EXEC_RDPMC                  (1u << 11)
#define PRIM_EXEC_RDTSC                  (1u << 12)
#define PRIM_EXEC_CR3_LOAD               (1u << 15)
#define PRIM_EXEC_CR3_STORE              (1u << 16)
/* Note: “activate tertiary controls” は CPUにより予約/未使用のことがある */
#define PRIM_EXEC_ACTIVATE_TERTIARY      (1u << 17)  /* if supported */
#define PRIM_EXEC_CR8_LOAD               (1u << 19)
#define PRIM_EXEC_CR8_STORE              (1u << 20)
#define PRIM_EXEC_USE_TPR_SHADOW         (1u << 21)
#define PRIM_EXEC_NMI_WINDOW             (1u << 22)
#define PRIM_EXEC_MOV_DR                 (1u << 23)
#define PRIM_EXEC_UNCOND_IO              (1u << 24)
#define PRIM_EXEC_USE_IO_BITMAPS         (1u << 25)
#define PRIM_EXEC_MONITOR_TRAP_FLAG      (1u << 27)
#define PRIM_EXEC_USE_MSR_BITMAPS        (1u << 28)
#define PRIM_EXEC_MONITOR                (1u << 29)
#define PRIM_EXEC_PAUSE                  (1u << 30)
#define PRIM_EXEC_ACTIVATE_SECONDARY     (1u << 31)

static inline int vmx_primary_exec_read(vmx_primary_exec_ctrl_t *out)
{
    uint64_t v; int rc = vmcs_vmread(VMCS_PRIMARY_PROC_CTLS, &v);
    if (rc==0) out->raw = (uint32_t)v;
    return rc;
}

static inline int vmx_primary_exec_write(vmx_primary_exec_ctrl_t v)
{
    return vmcs_vmwrite(VMCS_PRIMARY_PROC_CTLS, v.raw);
}

static inline int vmx_primary_exec_commit_adjusted(vmx_primary_exec_ctrl_t v)
{
    uint64_t msr = vmx_pick_ctrl_msr(IA32_VMX_TRUE_PROCBASED_CTLS, IA32_VMX_PROCBASED_CTLS);
    uint32_t adj = vmx_adjust_ctrl_u32(v.raw, msr);
    return vmcs_vmwrite(VMCS_PRIMARY_PROC_CTLS, adj);
}

/* ---------- Secondary Processor-Based Execution Controls ---------- */

typedef struct { uint32_t raw; } vmx_secondary_exec_ctrl_t;

/* Bits (SDM Vol.3C 25.6.2 “Secondary Processor-Based Controls”) */
#define SEC_EXEC_VIRT_APIC_ACCESSES      (1u << 0)
#define SEC_EXEC_EPT                     (1u << 1)
#define SEC_EXEC_DESC_TABLE              (1u << 2)
#define SEC_EXEC_RDTSCP                  (1u << 3)
#define SEC_EXEC_VIRT_X2APIC             (1u << 4)
#define SEC_EXEC_VPID                    (1u << 5)
#define SEC_EXEC_WBINVD                  (1u << 6)
#define SEC_EXEC_UNRESTRICTED_GUEST      (1u << 7)
#define SEC_EXEC_APIC_REG_VIRT           (1u << 8)
#define SEC_EXEC_VIRT_INTR_DELIVERY      (1u << 9)
#define SEC_EXEC_PAUSE_LOOP_EXIT         (1u << 10)
#define SEC_EXEC_RDRAND                  (1u << 11)
#define SEC_EXEC_INVPCID                 (1u << 12)
#define SEC_EXEC_VMFUNC                  (1u << 13)
#define SEC_EXEC_VMCS_SHADOWING          (1u << 14)
#define SEC_EXEC_ENCLS                   (1u << 15)
#define SEC_EXEC_RDSEED                  (1u << 16)
#define SEC_EXEC_PML                     (1u << 17)
#define SEC_EXEC_EPT_VIOLATION_VE        (1u << 18)
#define SEC_EXEC_CONCEAL_VMX_FROM_PT     (1u << 19)
#define SEC_EXEC_XSAVES_XRSTORS          (1u << 20)
#define SEC_EXEC_PASID_TRANSLATION       (1u << 21)
#define SEC_EXEC_MODE_BASED_EPT          (1u << 22)
#define SEC_EXEC_SUBPAGE_WRITE_EPTP      (1u << 23)
#define SEC_EXEC_PT_GUEST_PA             (1u << 24)
#define SEC_EXEC_TSC_SCALING             (1u << 25)
#define SEC_EXEC_USER_WAIT_PAUSE         (1u << 26)
#define SEC_EXEC_PCONFIG                 (1u << 27)
#define SEC_EXEC_ENCLV                   (1u << 28)
#define SEC_EXEC_VMM_BUS_LOCK_DETECT     (1u << 30)
#define SEC_EXEC_INSTRUCTION_TIMEOUT     (1u << 31)

static inline int vmx_secondary_exec_read(vmx_secondary_exec_ctrl_t *out)
{
    uint64_t v; int rc = vmcs_vmread(VMCS_SECONDARY_PROC_CTLS, &v);
    if (rc==0) out->raw = (uint32_t)v;
    return rc;
}

static inline int vmx_secondary_exec_write(vmx_secondary_exec_ctrl_t v)
{
    return vmcs_vmwrite(VMCS_SECONDARY_PROC_CTLS, v.raw);
}

static inline int vmx_secondary_exec_commit_adjusted(vmx_secondary_exec_ctrl_t v)
{
    /* Secondary は TRUE/LEGACY の区別がない（IA32_VMX_PROCBASED_CTLS2 のみ） */
    uint64_t msr = rdmsr(IA32_VMX_PROCBASED_CTLS2);
    uint32_t adj = vmx_adjust_ctrl_u32(v.raw, msr);
    return vmcs_vmwrite(VMCS_SECONDARY_PROC_CTLS, adj);
}


/* ---------- Exception Bitmap (SDM Vol.3C 25.6.3) ---------- */

typedef struct { uint32_t raw; } vmx_exception_bitmap_t;

/* よく使う例外のビット定義（必要に応じて追加） */
#define EXC_BITMAP_DE                    (1u << 0)   /* divide-by-zero (#DE) */
#define EXC_BITMAP_DB                    (1u << 1)   /* debug (#DB) */
#define EXC_BITMAP_NMI                   (1u << 2)   /* NMI */
#define EXC_BITMAP_BP                    (1u << 3)   /* breakpoint (#BP) */
#define EXC_BITMAP_OF                    (1u << 4)   /* overflow (#OF) */
#define EXC_BITMAP_BR                    (1u << 5)   /* bound range exceeded (#BR) */
#define EXC_BITMAP_UD                    (1u << 6)   /* invalid opcode (#UD) */
#define EXC_BITMAP_NM                    (1u << 7)   /* device not available (#NM) */
#define EXC_BITMAP_DF                    (1u << 8)   /* double fault (#DF) */
#define EXC_BITMAP_CO_SEG_OVR            (1u << 9)   /* coprocessor segment overrun (legacy) */
#define EXC_BITMAP_TS                    (1u << 10)  /* invalid TSS (#TS) */
#define EXC_BITMAP_NP                    (1u << 11)  /* segment not present (#NP) */
#define EXC_BITMAP_SS                    (1u << 12)  /* stack-segment fault (#SS) */
#define EXC_BITMAP_GP                    (1u << 13)  /* general protection (#GP) */
#define EXC_BITMAP_PF                    (1u << 14)  /* page fault (#PF) */
#define EXC_BITMAP_MF                    (1u << 16)  /* x87 FP (#MF) */
#define EXC_BITMAP_AC                    (1u << 17)  /* alignment check (#AC) */
#define EXC_BITMAP_MC                    (1u << 18)  /* machine check (#MC) */
#define EXC_BITMAP_XM                    (1u << 19)  /* SIMD FP (#XM/#XF) */
#define EXC_BITMAP_VE                    (1u << 20)  /* virtualization (#VE) */
#define EXC_BITMAP_CP                    (1u << 21)  /* control-protection (#CP) */

static inline int vmx_exception_bitmap_read(vmx_exception_bitmap_t *out)
{
    uint64_t v; int rc = vmcs_vmread(VMCS_EXCEPTION_BITMAP, &v);
    if (rc == 0) out->raw = (uint32_t)v;
    return rc;
}

static inline int vmx_exception_bitmap_write(vmx_exception_bitmap_t v)
{
    return vmcs_vmwrite(VMCS_EXCEPTION_BITMAP, (uint64_t)v.raw);
}

/* Page-fault の詳細制御（必要なら併用） */
static inline int vmx_pf_err_mask_write(uint32_t mask)
{
    return vmcs_vmwrite(VMCS_PAGE_FAULT_ERROR_CODE_MASK, (uint64_t)mask);
}
static inline int vmx_pf_err_match_write(uint32_t match)
{
    return vmcs_vmwrite(VMCS_PAGE_FAULT_ERROR_CODE_MATCH, (uint64_t)match);
}

/* ---------- Primary VM-Exit Controls (SDM Vol.3C 25.8) ---------- */

typedef struct { uint32_t raw; } vmx_primary_exit_ctrl_t;

/* Primary VM-Exit Controls (VMCS_PRIMARY_EXIT_CTLS) — full bit map */
/* [0:1]  Reserved */
#define EXIT_CTRL_SAVE_DEBUG             (1u << 2)   /* DR7/IA32_DEBUGCTL を保存 */
/* [3:8]  Reserved */
#define EXIT_CTRL_HOST_ADDR_SPACE_SIZE   (1u << 9)   /* Host を 64bit モードにする */
/* [10:11] Reserved */
#define EXIT_CTRL_LOAD_PERF_GLOBAL_CTRL  (1u << 12)  /* IA32_PERF_GLOBAL_CTRL をロード */
/* [13:14] Reserved */
#define EXIT_CTRL_ACK_INTR_ON_EXIT       (1u << 15)  /* 外部割込みをACKしてベクタ取得 */
/* [16:17] Reserved */
#define EXIT_CTRL_SAVE_PAT               (1u << 18)  /* IA32_PAT を保存 */
#define EXIT_CTRL_LOAD_PAT               (1u << 19)  /* IA32_PAT をロード */
#define EXIT_CTRL_SAVE_EFER              (1u << 20)  /* IA32_EFER を保存 */
#define EXIT_CTRL_LOAD_EFER              (1u << 21)  /* IA32_EFER をロード */
#define EXIT_CTRL_SAVE_VMX_PREEMPT       (1u << 22)  /* VMX プリエンプションタイマ値を保存 */
#define EXIT_CTRL_CLEAR_BNDCFGS          (1u << 23)  /* IA32_BNDCFGS をクリア */
#define EXIT_CTRL_CONCEAL_VMX_FROM_PT    (1u << 24)  /* Processor Trace から VMX を秘匿 */
#define EXIT_CTRL_CLEAR_IA32_RTIT_CTL    (1u << 25)  /* IA32_RTIT_CTL をクリア */
#define EXIT_CTRL_CLEAR_IA32_LBR_CTL     (1u << 26)  /* IA32_LBR_CTL をクリア */
#define EXIT_CTRL_CLEAR_UINV             (1u << 27)  /* UINV をクリア（ユーザ割込み通知無効化） */
#define EXIT_CTRL_LOAD_CET_STATE         (1u << 28)  /* CET 関連 MSR/SSP をロード */
#define EXIT_CTRL_LOAD_PKRS              (1u << 29)  /* IA32_PKRS をロード */
#define EXIT_CTRL_SAVE_PERF_GLOBAL_CTL   (1u << 30)  /* IA32_PERF_GLOBAL_CTL を保存 */
#define EXIT_CTRL_ACTIVATE_SECONDARY     (1u << 31)  /* Secondary VM-Exit Controls を有効化 */


static inline int vmx_primary_exit_read(vmx_primary_exit_ctrl_t *out)
{
    uint64_t v; int rc = vmcs_vmread(VMCS_VMEXIT_CTLS, &v);
    if (rc == 0) out->raw = (uint32_t)v;
    return rc;
}

static inline int vmx_primary_exit_write(vmx_primary_exit_ctrl_t v)
{
    return vmcs_vmwrite(VMCS_VMEXIT_CTLS, (uint64_t)v.raw);
}

static inline int vmx_primary_exit_commit_adjusted(vmx_primary_exit_ctrl_t v)
{
    uint64_t msr = vmx_pick_ctrl_msr(IA32_VMX_TRUE_EXIT_CTLS, IA32_VMX_EXIT_CTLS);
    uint32_t adj = vmx_adjust_ctrl_u32(v.raw, msr);
    return vmcs_vmwrite(VMCS_VMEXIT_CTLS, (uint64_t)adj);
}

// /* ---------- Secondary VM-Exit Controls (u64, IA32_VMX_EXIT_CTLS2) ---------- */

// typedef struct { uint64_t raw; } vmx_secondary_exit_ctrl_t;

// /* 既知のビット例（“prematurely busy shadow stack”） */
// #define SEC_EXIT_PREMATURELY_BUSY_SHSTK  (1ull << 3)

// static inline int vmx_secondary_exit_read(vmx_secondary_exit_ctrl_t *out)
// {
//     uint64_t v; int rc = vmcs_vmread(VMCS_SECONDARY_EXIT_CTLS, &v);
//     if (rc == 0) out->raw = v;
//     return rc;
// }

// static inline int vmx_secondary_exit_write(vmx_secondary_exit_ctrl_t v)
// {
//     return vmcs_vmwrite(VMCS_SECONDARY_EXIT_CTLS, v.raw);
// }

// static inline int vmx_secondary_exit_commit_adjusted(vmx_secondary_exit_ctrl_t v)
// {
//     uint64_t msr = rdmsr(IA32_VMX_EXIT_CTLS2);
//     uint64_t adj = vmx_adjust_ctrl_u64(v.raw, msr);
//     return vmcs_vmwrite(VMCS_SECONDARY_EXIT_CTLS, adj);
// }

/* ---------- VM-Entry Controls (SDM Vol.3C 25.7) ---------- */

typedef struct { uint32_t raw; } vmx_entry_ctrl_t;

/* VM-Entry Controls (VMCS_ENTRY_CTLS) — full bit map per SDM Table 25-15 */

/* [0:1]   Reserved */
#define ENTRY_CTRL_LOAD_DEBUG            (1u << 2)   /* DR7/IA32_DEBUGCTL を VM-entry でロード */ 
#define ENTRY_CTRL_IA32E_MODE_GUEST      (1u << 9)   /* VM-entry 後に IA-32e モード（EFER.LMA） */ 
#define ENTRY_CTRL_ENTRY_TO_SMM          (1u << 10)  /* VM-entry 後に SMM */ 
#define ENTRY_CTRL_DEACTIVATE_DUALMON    (1u << 11)  /* SMM 二重モニタ無効化（非 SMM からは 0 必須） */ 
 /* [12]   Reserved （※以前の定義と違い、ここは予約） */
#define ENTRY_CTRL_LOAD_PERF_GLOBAL_CTRL (1u << 13)  /* IA32_PERF_GLOBAL_CTRL をロード */
#define ENTRY_CTRL_LOAD_PAT              (1u << 14)  /* IA32_PAT をロード */ 
#define ENTRY_CTRL_LOAD_EFER             (1u << 15)  /* IA32_EFER をロード */      
#define ENTRY_CTRL_LOAD_BNDCFGS          (1u << 16)  /* IA32_BNDCFGS をロード */               
#define ENTRY_CTRL_CONCEAL_VMX_FROM_PT   (1u << 17)  /* VMX を PT から秘匿（PIP/VMCS pkt 抑止） */   
#define ENTRY_CTRL_LOAD_RTIT_CTL         (1u << 18)  /* IA32_RTIT_CTL をロード（PT 制御） */    
#define ENTRY_CTRL_LOAD_UINV             (1u << 19)  /* UINV をロード */    
#define ENTRY_CTRL_LOAD_CET_STATE        (1u << 20)  /* CET 関連 MSR/SSP をロード */  
#define ENTRY_CTRL_LOAD_GUEST_LBR_CTL    (1u << 21)  /* IA32_LBR_CTL をロード（ゲスト側） */  
#define ENTRY_CTRL_LOAD_PKRS             (1u << 22)  /* IA32_PKRS をロード */ 
/* [23:31] Reserved */


static inline int vmx_entry_read(vmx_entry_ctrl_t *out)
{
    uint64_t v; int rc = vmcs_vmread(VMCS_VMENTRY_CTLS, &v);
    if (rc == 0) out->raw = (uint32_t)v;
    return rc;
}

static inline int vmx_entry_write(vmx_entry_ctrl_t v)
{
    return vmcs_vmwrite(VMCS_VMENTRY_CTLS, (uint64_t)v.raw);
}

static inline int vmx_entry_commit_adjusted(vmx_entry_ctrl_t v)
{
    uint64_t msr = vmx_pick_ctrl_msr(IA32_VMX_TRUE_ENTRY_CTLS, IA32_VMX_ENTRY_CTLS);
    uint32_t adj = vmx_adjust_ctrl_u32(v.raw, msr);
    return vmcs_vmwrite(VMCS_VMENTRY_CTLS, (uint64_t)adj);
}