#pragma once
#include <stdint.h>
#include <arch/x86/arch_x86_low.h>

/* ---- 使用する MSR 番号（VMX コア） ---- */
/* cf. Intel SDM Vol. 3C: 35.x (VMX MSRs) */
enum {
    IA32_FEATURE_CONTROL           = 0x0000003A,

    IA32_VMX_BASIC                 = 0x00000480,
    IA32_VMX_PINBASED_CTLS         = 0x00000481,
    IA32_VMX_PROCBASED_CTLS        = 0x00000482,
    IA32_VMX_EXIT_CTLS             = 0x00000483,
    IA32_VMX_ENTRY_CTLS            = 0x00000484,
    IA32_VMX_MISC                  = 0x00000485,
    IA32_VMX_CR0_FIXED0            = 0x00000486,
    IA32_VMX_CR0_FIXED1            = 0x00000487,
    IA32_VMX_CR4_FIXED0            = 0x00000488,
    IA32_VMX_CR4_FIXED1            = 0x00000489,
    IA32_VMX_VMCS_ENUM             = 0x0000048A, /* VMCSフィールド上限の列挙 */

    /* 追加の制御 MSR */
    IA32_VMX_PROCBASED_CTLS2       = 0x0000048B, /* Secondary Processor-Based Controls */
    IA32_VMX_EPT_VPID_CAP          = 0x0000048C, /* EPT/VPID capability bits */

    /* “TRUE” 系（ベーシスをオーバーライドできる） */
    IA32_VMX_TRUE_PINBASED_CTLS    = 0x0000048D,
    IA32_VMX_TRUE_PROCBASED_CTLS   = 0x0000048E,
    IA32_VMX_TRUE_EXIT_CTLS        = 0x0000048F,
    IA32_VMX_TRUE_ENTRY_CTLS       = 0x00000490,

    /* VMFUNC 機能ビット（EPTP switching 等） */
    IA32_VMX_VMFUNC                = 0x00000491,

    /* 近年追加の第三次プロセッサ制御（対応CPUのみ） */
    IA32_VMX_PROCBASED_CTLS3       = 0x00000492, /* Tertiary Processor-Based Controls */
};

/* IA32_FEATURE_CONTROL bit 定義 */
typedef union {
    uint64_t u64;
    struct {
        uint64_t lock            : 1;  /* bit0 */
        uint64_t vmx_in_smx      : 1;  /* bit1 */
        uint64_t vmx_outside_smx : 1;  /* bit2 */
        uint64_t reserved        : 61;
    } __attribute__((packed));
} ia32_feature_control_t;

/* IA32_VMX_BASIC 抜粋 */
typedef union {
    uint64_t u64;
    struct {
        uint32_t vmcs_revision_id : 31;
        uint32_t always0          : 1;
        uint16_t vmxon_region_size;    /* 実装上は常に 0x1000（4KiB）になることが多い */
        uint16_t _rsvd0           : 7;
        uint16_t true_controls    : 1;
        uint16_t _rsvd1           : 8;
    } __attribute__((packed));
} ia32_vmx_basic_t;

/* CR0/CR4 調整用の固定マスクを取得 */
static inline uint32_t vmx_cr0_fixed0(void){ return (uint32_t)rdmsr(IA32_VMX_CR0_FIXED0); }
static inline uint32_t vmx_cr0_fixed1(void){ return (uint32_t)rdmsr(IA32_VMX_CR0_FIXED1); }
static inline uint32_t vmx_cr4_fixed0(void){ return (uint32_t)rdmsr(IA32_VMX_CR4_FIXED0); }
static inline uint32_t vmx_cr4_fixed1(void){ return (uint32_t)rdmsr(IA32_VMX_CR4_FIXED1); }
