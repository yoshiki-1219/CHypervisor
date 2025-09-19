#pragma once
#include <stdint.h>
#include <stddef.h>

/* =========================== 概要 ===========================
 * - VMCS Region の確保・初期化（revisionID 設定 → VMCLEAR → VMPTRLD）
 * - VMREAD / VMWRITE（RFLAGS を検査して成否を返す）
 * - VM-instruction error の読み出し（VMCSフィールド 0x4400）
 * - ★ VMCS-field Encoding 生成ヘルパ（Type/Width/Access/Index から 32bit を生成）
 * =========================================================== */

/* ---- 公開 API（vmcs.c に実装済み） ---- */
int      vmcs_alloc_and_load(void** out_vmcs_va);               /* 0:ok/-1:ng */
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
