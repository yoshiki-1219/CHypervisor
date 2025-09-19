// kernel/arch/x86/vmm/vcpu.c
#include <stdint.h>
#include <string.h>
#include "log.h"
#include "panic.h"
#include "common.h"
#include "arch/x86/arch_x86_low.h"
#include "arch/x86/paging.h"
#include "arch/x86/vmm/vmcs.h"
#include "arch/x86/vmm/vmx.h"
#include "arch/x86/vmm/vmx_log.h"
#include "arch/x86/vmm/vcpu.h"
#include "arch/x86/msr.h"
#include "arch/x86/gdt.h"

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

/*==========================================================
 * 1) 最小ゲスト: HLT ループ
 *   - RSP を使わないので naked でもよいが、Cでは noreturn 関数で十分
 *==========================================================*/
void vcpu_guest_hlt_loop(void)
{
    for (;;) {
        __asm__ __volatile__("hlt");
    }
}

/*==========================================================
 * 2) VM-Exit 時に最初に飛んでくる簡易ハンドラ
 *   - プロローグ/エピローグを避けるため naked で入口を作り、
 *     C 関数に tail-call します
 *==========================================================*/

__attribute__((noreturn)) void vmexit_c_handler(void)
{
    uint64_t reason = 0;
    (void)vmcs_vmread(VMCS_EXIT_REASON, &reason);
    KLOG_DEBUG("vcpu", "[VMEXIT] reason=0x%llx",
               (unsigned long long)(reason & 0xFFFFu));
    for (;;)
        __asm__ __volatile__("hlt");
}

__attribute__((naked)) void vmexit_bootstrap_handler(void)
{
    __asm__ __volatile__(
        "call vmexit_c_handler\n\t"
        "hlt\n\t"
    );
}

/* VMEXIT 用の一時スタック */
#define TMP_STACK_BYTES  4096u
static __attribute__((aligned(16))) uint8_t s_vmexit_tmp_stack[TMP_STACK_BYTES + 16];

/*==========================================================
 * 3) Allowed 0/1 settings を VMCS 値に適用するヘルパ
 *    - 下位32bit: Mandatory-1（そのビットは必ず1に）
 *    - 上位32bit: Mandatory-0（そのビットは必ず0に）
 *==========================================================*/
static inline uint32_t adjust_ctrl_u32(uint32_t val, uint64_t msr)
{
    uint32_t must_be_one = (uint32_t)(msr & 0xFFFFFFFFu);
    uint32_t must_be_zero= (uint32_t)(msr >> 32);
    val |= must_be_one;
    val &= must_be_zero;
    return val;
}

/* IA32_VMX_BASIC の true_controls 有無で参照する制御 MSR を選ぶ */
static inline uint64_t pick_ctrl_msr(uint32_t msr_true, uint32_t msr_legacy)
{
    ia32_vmx_basic_t basic; basic.u64 = rdmsr(IA32_VMX_BASIC);
    if (basic.true_controls) {
        return rdmsr(msr_true);
    } else {
        return rdmsr(msr_legacy);
    }
}

/*==========================================================
 * 4) Execution Controls（Pin / Primary-Proc）
 *==========================================================*/
static int setup_exec_controls(void)
{
    /* --- Pin-based ---（今回はデフォルト＝0から Allowed を適用） */
    uint32_t pin = 0;
    {
        uint64_t m = pick_ctrl_msr(IA32_VMX_TRUE_PINBASED_CTLS, IA32_VMX_PINBASED_CTLS);
        pin = adjust_ctrl_u32(pin, m);
        if (vmcs_vmwrite(VMCS_PIN_BASED_CTLS, pin) != 0) return -1;
    }

    /* --- Primary Processor-based --- */
    uint32_t proc = 0;
    {
        /* .hlt=0（HLTでVMEXITさせない）, .activate_secondary_controls=0 */
        const uint32_t PROC_CTL_HLT = (1u << 7);
        const uint32_t PROC_CTL_ACTIVATE_SECONDARY = (1u << 31);
        // proc &= ~PROC_CTL_HLT;
        proc |= PROC_CTL_HLT;
        proc &= ~PROC_CTL_ACTIVATE_SECONDARY;

        uint64_t m = pick_ctrl_msr(IA32_VMX_TRUE_PROCBASED_CTLS, IA32_VMX_PROCBASED_CTLS);
        proc = adjust_ctrl_u32(proc, m);
        if (vmcs_vmwrite(VMCS_PRIMARY_PROC_CTLS, proc) != 0) return -1;
    }
    return 0;
}

/*==========================================================
 * 5) Host-State
 *   - 退出後に戻ってくるホストの状態を設定
 *==========================================================*/
static int setup_host_state(void)
{
    /* Control Registers */
    if (vmcs_vmwrite(VMCS_HOST_CR0, read_cr0()) != 0) return -1;
    if (vmcs_vmwrite(VMCS_HOST_CR3, read_cr3()) != 0) return -1;
    if (vmcs_vmwrite(VMCS_HOST_CR4, read_cr4()) != 0) return -1;

    /* RSP / RIP */
    uint64_t rsp_top = (uint64_t)s_vmexit_tmp_stack + TMP_STACK_BYTES;
    if (vmcs_vmwrite(VMCS_HOST_RSP, rsp_top) != 0) return -1;
    if (vmcs_vmwrite(VMCS_HOST_RIP, (uint64_t)&vmexit_bootstrap_handler) != 0) return -1;
    /* Segment selectors */
    if (vmcs_vmwrite(VMCS_HOST_CS_SELECTOR, read_cs()) != 0) return -1;
    if (vmcs_vmwrite(VMCS_HOST_SS_SELECTOR, read_ss()) != 0) return -1;
    if (vmcs_vmwrite(VMCS_HOST_DS_SELECTOR, read_ds()) != 0) return -1;
    if (vmcs_vmwrite(VMCS_HOST_ES_SELECTOR, read_es()) != 0) return -1;
    if (vmcs_vmwrite(VMCS_HOST_FS_SELECTOR, read_fs()) != 0) return -1;
    if (vmcs_vmwrite(VMCS_HOST_GS_SELECTOR, read_gs()) != 0) return -1;
    if (vmcs_vmwrite(VMCS_HOST_TR_SELECTOR, read_tr()) != 0) return -1;

    /* Bases: FS/GS/TR/GDTR/IDTR */
    struct desc_ptr gdtr = sgdt_get();
    struct desc_ptr idtr = sidt_get();
    if (vmcs_vmwrite(VMCS_HOST_GDTR_BASE, gdtr.base) != 0) return -1;
    if (vmcs_vmwrite(VMCS_HOST_IDTR_BASE, idtr.base) != 0) return -1;

    uint16_t tr = read_tr();
    uint64_t trb = tss_base_from_gdt(tr, gdtr);
    if (vmcs_vmwrite(VMCS_HOST_TR_SELECTOR, tr) != 0) return -1;
    if (vmcs_vmwrite(VMCS_HOST_TR_BASE,     trb) != 0) return -1;

    uint64_t fsb = rdmsr(IA32_FS_BASE);
    uint64_t gsb = rdmsr(IA32_GS_BASE);
    if (vmcs_vmwrite(VMCS_HOST_FS_BASE, fsb) != 0) return -1;
    if (vmcs_vmwrite(VMCS_HOST_GS_BASE, gsb) != 0) return -1;
    if (vmcs_vmwrite(VMCS_HOST_TR_BASE, 0)   != 0) return -1; /* 未使用なら0 */

    /* Host EFER は Exit-Controls で load_ia32_efer=1 を立てる場合に必須 */
    if (vmcs_vmwrite(VMCS_HOST_IA32_EFER, rdmsr(IA32_EFER)) != 0) return -1;

    return 0;
}

/*==========================================================
 * 6) Guest-State（最小: 64bit long mode, HLT ループ実行）
 *   - 本章は “ホストとほぼ同じ環境” で Non-root へ遷移
 *==========================================================*/
static int setup_guest_state(void)
{
    /* Control Registers: ホストをそのまま */
    if (vmcs_vmwrite(VMCS_GUEST_CR0, read_cr0()) != 0) return -1;
    if (vmcs_vmwrite(VMCS_GUEST_CR3, read_cr3()) != 0) return -1;
    if (vmcs_vmwrite(VMCS_GUEST_CR4, read_cr4()) != 0) return -1;

    /* Base: 実際には使わないので 0。LDTR base はマーカー値 0xDEAD00 */
    if (vmcs_vmwrite(VMCS_GUEST_CS_BASE, 0) != 0) return -1;
    if (vmcs_vmwrite(VMCS_GUEST_SS_BASE, 0) != 0) return -1;
    if (vmcs_vmwrite(VMCS_GUEST_DS_BASE, 0) != 0) return -1;
    if (vmcs_vmwrite(VMCS_GUEST_ES_BASE, 0) != 0) return -1;
    if (vmcs_vmwrite(VMCS_GUEST_FS_BASE, 0) != 0) return -1;
    if (vmcs_vmwrite(VMCS_GUEST_GS_BASE, 0) != 0) return -1;
    if (vmcs_vmwrite(VMCS_GUEST_TR_BASE, 0) != 0) return -1;
    if (vmcs_vmwrite(VMCS_GUEST_GDTR_BASE, 0) != 0) return -1;
    if (vmcs_vmwrite(VMCS_GUEST_IDTR_BASE, 0) != 0) return -1;
    if (vmcs_vmwrite(VMCS_GUEST_LDTR_BASE, 0xDEAD00) !=0) return -1;

    /* Limit: 使わないので最大値（TR/LDTR/IDT/GDT は 0 でも可） */
    (void)vmcs_vmwrite(VMCS_GUEST_CS_LIMIT, 0xFFFFu);
    (void)vmcs_vmwrite(VMCS_GUEST_SS_LIMIT, 0xFFFFu);
    (void)vmcs_vmwrite(VMCS_GUEST_DS_LIMIT, 0xFFFFu);
    (void)vmcs_vmwrite(VMCS_GUEST_ES_LIMIT, 0xFFFFu);
    (void)vmcs_vmwrite(VMCS_GUEST_FS_LIMIT, 0xFFFFu);
    (void)vmcs_vmwrite(VMCS_GUEST_GS_LIMIT, 0xFFFFu);
    (void)vmcs_vmwrite(VMCS_GUEST_TR_LIMIT, 0);
    (void)vmcs_vmwrite(VMCS_GUEST_LDTR_LIMIT, 0);
    (void)vmcs_vmwrite(VMCS_GUEST_IDTR_LIMIT, 0);
    (void)vmcs_vmwrite(VMCS_GUEST_GDTR_LIMIT, 0);

    /* Selectors: CS のみホストと同じに。その他は 0 */
    if (vmcs_vmwrite(VMCS_GUEST_CS_SELECTOR, read_cs()) != 0) return -1;
    if (vmcs_vmwrite(VMCS_GUEST_SS_SELECTOR, 0) != 0) return -1;
    if (vmcs_vmwrite(VMCS_GUEST_DS_SELECTOR, 0) != 0) return -1;
    if (vmcs_vmwrite(VMCS_GUEST_ES_SELECTOR, 0) != 0) return -1;
    if (vmcs_vmwrite(VMCS_GUEST_FS_SELECTOR, 0) != 0) return -1;
    if (vmcs_vmwrite(VMCS_GUEST_GS_SELECTOR, 0) != 0) return -1;
    if (vmcs_vmwrite(VMCS_GUEST_TR_SELECTOR, 0) != 0) return -1;
    if (vmcs_vmwrite(VMCS_GUEST_LDTR_SELECTOR, 0) != 0) return -1;

    /* CS: 64-bit code, DPL=0, S=1, P=1, L=1, Accessed=1, Readable=1, DB=0, G=0 */
    static const uint32_t AR_CS64 =
        TYPE_CODE_ER_AC | AR_S_CODEDATA | AR_DPL(0) | AR_P | AR_L;

    /* DS/SS/ES/FS/GS: data RW, DPL=0, S=1, P=1, DB=1, G=0, Accessed=1 */
    static const uint32_t AR_DS =
        TYPE_DATA_RW_AC | AR_S_CODEDATA | AR_DPL(0) | AR_P | AR_DB;

    /* TR: Busy TSS (32/64の区別は DB/L/G で表現。ここでは Zig と同じく byte 粒度, L=0, DB=0) */
    static const uint32_t AR_TR =
        TYPE_TSS_BUSY | AR_S_SYSTEM | AR_DPL(0) | AR_P;

    /* LDTR: LDT, byte 粒度, P=1 */
    static const uint32_t AR_LDTR =
        TYPE_LDT | AR_S_SYSTEM | AR_DPL(0) | AR_P;

    (void)vmcs_vmwrite(VMCS_GUEST_CS_ACCESS_RIGHTS,   AR_CS64);
    (void)vmcs_vmwrite(VMCS_GUEST_SS_ACCESS_RIGHTS,   AR_DS);
    (void)vmcs_vmwrite(VMCS_GUEST_DS_ACCESS_RIGHTS,   AR_DS);
    (void)vmcs_vmwrite(VMCS_GUEST_ES_ACCESS_RIGHTS,   AR_DS);
    (void)vmcs_vmwrite(VMCS_GUEST_FS_ACCESS_RIGHTS,   AR_DS);
    (void)vmcs_vmwrite(VMCS_GUEST_GS_ACCESS_RIGHTS,   AR_DS);
    (void)vmcs_vmwrite(VMCS_GUEST_TR_ACCESS_RIGHTS,   AR_TR);
    (void)vmcs_vmwrite(VMCS_GUEST_LDTR_ACCESS_RIGHTS, AR_LDTR);

    /* RIP / RFLAGS / EFER */
    if (vmcs_vmwrite(VMCS_GUEST_RIP,    (uint64_t)&vcpu_guest_hlt_loop) != 0) return -1;
    if (vmcs_vmwrite(VMCS_GUEST_RFLAGS, 0x2u /*IF=0, reserved=1*/) != 0) return -1;
    if (vmcs_vmwrite(VMCS_GUEST_IA32_EFER,   rdmsr(IA32_EFER)) != 0) return -1;

    if (vmcs_vmwrite(VMCS_GUEST_VMCS_LINK_POINTER, 0xFFFFFFFFFFFFFFFFull) != 0) return -1;

    return 0;
}

/*==========================================================
 * 7) Entry / Exit Controls
 *==========================================================*/
static int setup_entry_exit_controls(void)
{
    /* ---- Entry Controls ---- */
    uint32_t entry = 0;
    {
        /* ia32e_mode_guest=1, load_ia32_efer=1 */
        const uint32_t ENTRY_IA32E_GUEST = (1u << 9);
        const uint32_t ENTRY_LOAD_EFER   = (1u << 15);
        entry |= ENTRY_IA32E_GUEST | ENTRY_LOAD_EFER;

        uint64_t m = pick_ctrl_msr(IA32_VMX_TRUE_ENTRY_CTLS, IA32_VMX_ENTRY_CTLS);
        entry = adjust_ctrl_u32(entry, m);
        if (vmcs_vmwrite(VMCS_VMENTRY_CTLS, entry) != 0) return -1;
    }

    /* ---- Exit Controls ---- */
    uint32_t exitc = 0;
    {
        /* host_addr_space_size=1, load_ia32_efer=1 */
        const uint32_t EXIT_HOST_ADDR_SPACE_SIZE = (1u << 9);
        const uint32_t EXIT_LOAD_EFER            = (1u << 21);
        exitc |= EXIT_HOST_ADDR_SPACE_SIZE | EXIT_LOAD_EFER;

        uint64_t m = pick_ctrl_msr(IA32_VMX_TRUE_EXIT_CTLS, IA32_VMX_EXIT_CTLS);
        exitc = adjust_ctrl_u32(exitc, m);
        if (vmcs_vmwrite(VMCS_VMEXIT_CTLS, exitc) != 0) return -1;
    }
    return 0;
}

/*==========================================================
 * 8) VMCS 構築 → VMLAUNCH
 *==========================================================*/
int vcpu_build_vmcs_and_launch(void)
{

    if (setup_exec_controls() != 0)         { KLOG_ERROR("vcpu","exec ctrls"); return -1; }
    if (setup_host_state() != 0)            { KLOG_ERROR("vcpu","host state"); return -1; }
    if (setup_guest_state() != 0)           { KLOG_ERROR("vcpu","guest state"); return -1; }
    if (setup_entry_exit_controls() != 0)   { KLOG_ERROR("vcpu","entry/exit"); return -1; }

    /* いよいよ VMLAUNCH */
    uint64_t rflags = 0;
    __asm__ __volatile__(
        "vmlaunch\n\t"
        "pushfq\n\t"
        "pop %0"
        : "=r"(rflags) :: "cc","memory"
    );

    /* 成否は CF/ZF で判断（VMX 命令共通の規約） */
    const int cf = (int)((rflags >> 0) & 1);
    const int zf = (int)((rflags >> 6) & 1);
    if (cf==0 && zf==0) {
        /* ここに来る場合、VMLAUNCH“直後に VMEXIT してホストに戻った”可能性が高い
           → vmexit_bootstrap_handler() に分岐していれば、そこから HLT ループ中のはず */
        KLOG_INFO("vcpu", "VMLAUNCH returned to root (VMEXIT path).");
        return 0;
    }

    /* 失敗：VM-instruction error をダンプ */
    uint32_t err = vmcs_instruction_error();
    KLOG_ERROR("vcpu", "VMLAUNCH failed: rflags=0x%llx, vmxerr=%u",
               (unsigned long long)rflags, (unsigned)err);
    return -1;
}








/* ---- VMCS Host RIP をこの関数に設定する（VMEXIT 入口） ---- */
extern void asm_vmexit(void);

/* ---- asm_vmentry: VMLAUNCH/VMRESUME を呼び、成功なら 0, 失敗なら 1 を返す ---- */
extern uint8_t asm_vmentry(Vcpu* vcpu);

/* ---- VMENTRY 毎に Host RSP を上書きする小ヘルパ（asm から呼ぶ） ---- */
static inline void set_host_rsp(uint64_t rsp) {
    (void)vmcs_vmwrite(VMCS_HOST_RSP, rsp);
}

/* asm から呼べるよう公開（シンボル名に注意） */
void set_host_rsp_thunk(uint64_t rsp) { set_host_rsp(rsp); }

/* =========================================================
 * VMEXIT で使う：Exit Reason/InstLen を読むユーティリティ
 * ========================================================= */
typedef struct ExitInfo {
    uint32_t reason;      /* VMCS_EXIT_REASON */
    uint32_t inst_len;    /* VMCS_EXIT_INSTRUCTION_LENGTH */
} ExitInfo;

static inline ExitInfo exitinfo_load(void) {
    ExitInfo ei = {0};
    uint64_t v = 0;
    if (vmcs_vmread(VMCS_EXIT_REASON, &v) == 0)     ei.reason   = (uint32_t)v;
    if (vmcs_vmread(VMCS_EXIT_INSTRUCTION_LENGTH, &v) == 0) ei.inst_len = (uint32_t)v;
    return ei;
}

/* =========================================================
 * VMEXIT → C 側ディスパッチ
 *  - 今は HLT だけ RIP を進めて続行
 * ========================================================= */
void vmexit_dispatch(Vcpu* vcpu) {
    (void)vcpu;
    ExitInfo ei = exitinfo_load();

    /* 下位 16bit が基本理由 */
    uint32_t basic = (ei.reason & 0xFFFFu);
    switch (basic) {
    case 12: /* HLT */
    {
        uint64_t rip=0;
        vmcs_vmread(VMCS_GUEST_RIP, &rip);
        rip += ei.inst_len;
        vmcs_vmwrite(VMCS_GUEST_RIP, rip);
        KLOG_DEBUG("vmexit", "HLT -> step RIP (+%u)", ei.inst_len);
        break;
    }
    default:
        KLOG_ERROR("vmexit", "Unhandled VMEXIT: reason=0x%x", basic);
        for(;;) __asm__ __volatile__("hlt");
    }
}

/* =========================================================
 * VM Entry/Exit ループ（C 側）
 *   - asm_vmentry 成功（= VMEXIT 経由で復帰）なら vmexit_dispatch()
 *   - 失敗（= VMX instruction error）ならエラーを表示して停止
 * ========================================================= */
int vcpu_loop(Vcpu* vcpu) {
    for (;;) {
        uint8_t ok = asm_vmentry(vcpu);
        if (ok == 0) {
            /* 成功: VMEXIT で戻ってきた → Exit を捌いて再度 Entry へ */
            vmexit_dispatch(vcpu);
            continue;
        }

        /* 失敗: CF/ZF set → Instruction error を読む */
        uint32_t err = vmcs_instruction_error();
        KLOG_ERROR("vmentry", "VM-entry failed: vmxerr=%u", err);
        return -1;
    }
}

/* =========================================================
 * Host RIP に asm_vmexit を設定（セットアップ時に一度呼ぶ）
 * ========================================================= */
void vcpu_setup_vmexit_rip(void) {
    vmcs_vmwrite(VMCS_HOST_RIP, (uint64_t)&asm_vmexit);
}
