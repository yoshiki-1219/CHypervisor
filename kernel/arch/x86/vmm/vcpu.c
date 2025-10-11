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
#include "arch/x86/vmm/vmx_vpid.h"
#include "arch/x86/vmm/vcpu.h"
#include "arch/x86/vmm/vmentry_exit.h"
#include "arch/x86/msr.h"
#include "arch/x86/gdt.h"

/*==========================================================
 * 1) 最小ゲスト: HLT ループ
 *   - RSP を使わないので naked でもよいが、Cでは noreturn 関数で十分
 *==========================================================*/
__attribute__((naked, noreturn))
void blobGuest(void)
{
    __asm__ __volatile__(
        ".intel_syntax noprefix\n\t"
        "1:\n\t"
        "hlt\n\t"                    // 割り込みで再開
        "mov rcx, 0x0000000\n\t"     // 絶対アドレスをレジスタに
        "mov rax, [rcx]\n\t"         // [0x1000000] -> RAX
        "mov rcx, 0x1000000\n\t"     // 絶対アドレスをレジスタに
        "mov rbx, [rcx]\n\t"         // [0x1000000] -> RBX
        "jmp 1b\n\t"                 // 無限ループ
        ".att_syntax prefix\n\t"
    );
}



static inline void dump_guest_regs(const Vcpu* vcpu)
{
    const GuestRegisters* g = &vcpu->guest_regs;

    KLOG_ERROR("vmexit",
        "GPRs:\n"
        " RAX=%016llx RBX=%016llx RCX=%016llx RDX=%016llx\n"
        " RSI=%016llx RDI=%016llx RBP=%016llx\n"
        "  R8=%016llx  R9=%016llx R10=%016llx R11=%016llx\n"
        " R12=%016llx R13=%016llx R14=%016llx R15=%016llx",
        (unsigned long long)g->rax, (unsigned long long)g->rbx,
        (unsigned long long)g->rcx, (unsigned long long)g->rdx,
        (unsigned long long)g->rsi, (unsigned long long)g->rdi,
        (unsigned long long)g->rbp,
        (unsigned long long)g->r8,  (unsigned long long)g->r9,
        (unsigned long long)g->r10, (unsigned long long)g->r11,
        (unsigned long long)g->r12, (unsigned long long)g->r13,
        (unsigned long long)g->r14, (unsigned long long)g->r15
    );
}

/* VMEXIT 用の一時スタック */
#define TMP_STACK_BYTES  4096u
static __attribute__((aligned(16))) uint8_t s_vmexit_tmp_stack[TMP_STACK_BYTES + 16];

/*==========================================================
 * 4) Execution Controls（Pin / Primary / Secondary + VPID）
 *==========================================================*/
static int setup_exec_controls(Vcpu* vcpu)
{
    /* --- Pin-based --- */
    {
        vmx_pin_exec_ctrl_t pin;
        if (vmx_pin_exec_read(&pin) != 0) return -1;
        /* Zig 側は Pin の個別ビットは変更しない → そのまま調整書き込み */
        if (vmx_pin_exec_commit_adjusted(pin) != 0) return -1;
    }

    /* --- Primary Processor-based --- */
    {
        vmx_primary_exec_ctrl_t prim;
        if (vmx_primary_exec_read(&prim) != 0) return -1;

        /* activate_secondary_controls = true; use_tpr_shadow = false; */
        prim.raw |=  PRIM_EXEC_ACTIVATE_SECONDARY;
        prim.raw &= ~PRIM_EXEC_USE_TPR_SHADOW;
        prim.raw |=  PRIM_EXEC_HLT;

        if (vmx_primary_exec_commit_adjusted(prim) != 0) return -1;
    }

    /* --- Secondary Processor-based --- */
    {
        vmx_secondary_exec_ctrl_t sec;
        if (vmx_secondary_exec_read(&sec) != 0) return -1;

        /* unrestricted_guest = true; ept = true; vpid = isVpidSupported(); */
        sec.raw |= SEC_EXEC_UNRESTRICTED_GUEST;
        sec.raw |= SEC_EXEC_EPT;
        if (vmx_is_vpid_supported()) {
            sec.raw |= SEC_EXEC_VPID;
        }

        if (vmx_secondary_exec_commit_adjusted(sec) != 0) return -1;
    }

    /* --- VPID フィールド（Zig の最後の処理に一致） --- */
    if (vmx_is_vpid_supported()) {
        if (vmcs_vmwrite(VMCS_VPID, (uint64_t)vcpu->vpid) != 0) return -1;
    }

    return 0;
}

/*==========================================================
 * 5) Host-State
 *   - 退出後に戻ってくるホストの状態を設定
 *==========================================================*/
static int setup_host_state(Vcpu* vcpu)
{
    (void)vcpu; 
    /* Control Registers */
    if (vmcs_vmwrite(VMCS_HOST_CR0, read_cr0()) != 0) return -1;
    if (vmcs_vmwrite(VMCS_HOST_CR3, read_cr3()) != 0) return -1;
    if (vmcs_vmwrite(VMCS_HOST_CR4, read_cr4()) != 0) return -1;

    /* RSP / RIP */
    uint64_t rsp_top = (uint64_t)s_vmexit_tmp_stack + TMP_STACK_BYTES;
    if (vmcs_vmwrite(VMCS_HOST_RSP, rsp_top) != 0) return -1;
    if (vmcs_vmwrite(VMCS_HOST_RIP, (uint64_t)&asm_vmexit) != 0) return -1;
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

    uint64_t fsb = rdmsr(IA32_FS_BASE);
    uint64_t gsb = rdmsr(IA32_GS_BASE);
    if (vmcs_vmwrite(VMCS_HOST_FS_BASE, fsb) != 0) return -1;
    if (vmcs_vmwrite(VMCS_HOST_GS_BASE, gsb) != 0) return -1;
    if (vmcs_vmwrite(VMCS_HOST_TR_BASE, 0)   != 0) return -1;

    /* Host EFER は Exit-Controls で load_ia32_efer=1 を立てる場合に必須 */
    if (vmcs_vmwrite(VMCS_HOST_IA32_EFER, rdmsr(IA32_EFER)) != 0) return -1;

    return 0;
}

/*==========================================================
 * 6) Guest-State（最小: 64bit long mode, HLT ループ実行）
 *   - 本章は “ホストとほぼ同じ環境” で Non-root へ遷移
 *==========================================================*/
static int setup_guest_state(Vcpu* vcpu)
{
    uint64_t cr0 = 0;
    cr0 |= (CR0_PE | CR0_NE | CR0_ET);
    cr0 &= ~CR0_PG;
    uint64_t cr4 = read_cr4();
    cr4 |=  CR4_VMXE;
    cr4 &= ~CR4_PAE;

    if (vmcs_vmwrite(VMCS_GUEST_CR0, cr0) != 0) return -1;
    if (vmcs_vmwrite(VMCS_GUEST_CR3, read_cr3()) != 0) return -1;
    if (vmcs_vmwrite(VMCS_GUEST_CR4, cr4) != 0) return -1;

    // Base
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

    // Limit
    (void)vmcs_vmwrite(VMCS_GUEST_CS_LIMIT, 0xFFFFFu);
    (void)vmcs_vmwrite(VMCS_GUEST_SS_LIMIT, 0xFFFFFu);
    (void)vmcs_vmwrite(VMCS_GUEST_DS_LIMIT, 0xFFFFFu);
    (void)vmcs_vmwrite(VMCS_GUEST_ES_LIMIT, 0xFFFFFu);
    (void)vmcs_vmwrite(VMCS_GUEST_FS_LIMIT, 0xFFFFFu);
    (void)vmcs_vmwrite(VMCS_GUEST_GS_LIMIT, 0xFFFFFu);
    (void)vmcs_vmwrite(VMCS_GUEST_TR_LIMIT, 0);
    (void)vmcs_vmwrite(VMCS_GUEST_LDTR_LIMIT, 0);
    (void)vmcs_vmwrite(VMCS_GUEST_IDTR_LIMIT, 0);
    (void)vmcs_vmwrite(VMCS_GUEST_GDTR_LIMIT, 0);

    static const SegmentRights CS_RIGHT = {
        .accessed    = 1,              
        .rw          = 1,    
        .dc          = 0,        
        .executable  = 1,      
        .desc_type   = DESCRIPTOR_CODE_DATA,
        .dpl         = 0,
        .present     = 1,    
        .reserved1   = 0,
        .avl         = 0,
        .long_mode   = 0,    
        .db          = 1, 
        .granularity = GRANULARITY_KBYTE,
        .unusable    = 0,
        .reserved2   = 0,
    };

    // ds_right
    static const SegmentRights DS_RIGHT = {
        .accessed    = 1,
        .rw          = 1,
        .dc          = 0,
        .executable  = 0, 
        .desc_type   = DESCRIPTOR_CODE_DATA,
        .dpl         = 0,
        .present     = 1,
        .reserved1   = 0,
        .avl         = 0,
        .long_mode   = 0,
        .db          = 1,
        .granularity = GRANULARITY_KBYTE,
        .unusable    = 0,
        .reserved2   = 0,
    };

    // tr_right
    static const SegmentRights TR_RIGHT = {
        .accessed    = 1,
        .rw          = 1,
        .dc          = 0,
        .executable  = 1,
        .desc_type   = DESCRIPTOR_SYSTEM,
        .dpl         = 0,
        .present     = 1,
        .reserved1   = 0,
        .avl         = 0,
        .long_mode   = 0,
        .db          = 0,
        .granularity = GRANULARITY_BYTE,
        .unusable    = 0,
        .reserved2   = 0,
    };

    // ldtr_right
    static const SegmentRights LDTR_RIGHT = {
        .accessed    = 0,  
        .rw          = 1,
        .dc          = 0,
        .executable  = 0,
        .desc_type   = DESCRIPTOR_SYSTEM,
        .dpl         = 0,
        .present     = 1,
        .reserved1   = 0,
        .avl         = 0,
        .long_mode   = 0,
        .db          = 0,
        .granularity = GRANULARITY_BYTE,
        .unusable    = 0,
        .reserved2   = 0,
    };

    // // CS (Code Segment)
    // static const SegmentRights CS_RIGHT = {
    //     .accessed    = 1,
    //     .rw          = 1,
    //     .dc          = 0,
    //     .executable  = 1,
    //     .desc_type   = DESCRIPTOR_CODE_DATA,
    //     .dpl         = 0,
    //     .present     = 1,
    //     .reserved1   = 0,
    //     .avl         = 0,
    //     .long_mode   = 1,              // Zig: .long = true
    //     .db          = 0,              // Zig: .db = 0
    //     .granularity = GRANULARITY_KBYTE,
    //     .unusable    = 0,
    //     .reserved2   = 0,
    // };

    // // DS (Data Segment)
    // static const SegmentRights DS_RIGHT = {
    //     .accessed    = 1,
    //     .rw          = 1,
    //     .dc          = 0,
    //     .executable  = 0,
    //     .desc_type   = DESCRIPTOR_CODE_DATA,
    //     .dpl         = 0,
    //     .present     = 1,
    //     .reserved1   = 0,
    //     .avl         = 0,
    //     .long_mode   = 0,
    //     .db          = 1,
    //     .granularity = GRANULARITY_KBYTE,
    //     .unusable    = 0,
    //     .reserved2   = 0,
    // };

    // // TR (Task Register, TSS descriptor)
    // static const SegmentRights TR_RIGHT = {
    //     .accessed    = 1,
    //     .rw          = 1,
    //     .dc          = 0,
    //     .executable  = 1,
    //     .desc_type   = DESCRIPTOR_SYSTEM,
    //     .dpl         = 0,
    //     .present     = 1,
    //     .reserved1   = 0,
    //     .avl         = 0,
    //     .long_mode   = 0,
    //     .db          = 0,
    //     .granularity = GRANULARITY_BYTE,
    //     .unusable    = 0,
    //     .reserved2   = 0,
    // };

    // // LDTR (Local Descriptor Table Register)
    // static const SegmentRights LDTR_RIGHT = {
    //     .accessed    = 0,              // Zig: accessed = false
    //     .rw          = 1,
    //     .dc          = 0,
    //     .executable  = 0,
    //     .desc_type   = DESCRIPTOR_SYSTEM,
    //     .dpl         = 0,
    //     .present     = 1,
    //     .reserved1   = 0,
    //     .avl         = 0,
    //     .long_mode   = 0,
    //     .db          = 0,
    //     .granularity = GRANULARITY_BYTE,
    //     .unusable    = 0,
    //     .reserved2   = 0,
    // };

    (void)vmcs_vmwrite(VMCS_GUEST_CS_ACCESS_RIGHTS,   segment_rights_to_u32(CS_RIGHT));
    (void)vmcs_vmwrite(VMCS_GUEST_SS_ACCESS_RIGHTS,   segment_rights_to_u32(DS_RIGHT));
    (void)vmcs_vmwrite(VMCS_GUEST_DS_ACCESS_RIGHTS,   segment_rights_to_u32(DS_RIGHT));
    (void)vmcs_vmwrite(VMCS_GUEST_ES_ACCESS_RIGHTS,   segment_rights_to_u32(DS_RIGHT));
    (void)vmcs_vmwrite(VMCS_GUEST_FS_ACCESS_RIGHTS,   segment_rights_to_u32(DS_RIGHT));
    (void)vmcs_vmwrite(VMCS_GUEST_GS_ACCESS_RIGHTS,   segment_rights_to_u32(DS_RIGHT));
    (void)vmcs_vmwrite(VMCS_GUEST_TR_ACCESS_RIGHTS,   segment_rights_to_u32(TR_RIGHT));
    (void)vmcs_vmwrite(VMCS_GUEST_LDTR_ACCESS_RIGHTS, segment_rights_to_u32(LDTR_RIGHT));

    // Selector
    if (vmcs_vmwrite(VMCS_GUEST_CS_SELECTOR, 0) != 0) return -1;
    if (vmcs_vmwrite(VMCS_GUEST_SS_SELECTOR, 0) != 0) return -1;
    if (vmcs_vmwrite(VMCS_GUEST_DS_SELECTOR, 0) != 0) return -1;
    if (vmcs_vmwrite(VMCS_GUEST_ES_SELECTOR, 0) != 0) return -1;
    if (vmcs_vmwrite(VMCS_GUEST_FS_SELECTOR, 0) != 0) return -1;
    if (vmcs_vmwrite(VMCS_GUEST_GS_SELECTOR, 0) != 0) return -1;
    if (vmcs_vmwrite(VMCS_GUEST_TR_SELECTOR, 0) != 0) return -1;
    if (vmcs_vmwrite(VMCS_GUEST_LDTR_SELECTOR, 0) != 0) return -1;

    // FS/GS base
    if (vmcs_vmwrite(VMCS_GUEST_FS_BASE, 0) != 0) return -1;
    if (vmcs_vmwrite(VMCS_GUEST_GS_BASE, 0) != 0) return -1;

    //MSR
    //if (vmcs_vmwrite(VMCS_GUEST_IA32_EFER,   rdmsr(IA32_EFER)) != 0) return -1;
    if (vmcs_vmwrite(VMCS_GUEST_IA32_EFER,   0) != 0) return -1;

    // General registers.
    if (vmcs_vmwrite(VMCS_GUEST_RFLAGS, 0x2u /*IF=0, reserved=1*/) != 0) return -1;

    // Other crucial fields.
    if (vmcs_vmwrite(VMCS_GUEST_RIP,    (uint64_t)0x00000) != 0) return -1;
    if (vmcs_vmwrite(VMCS_GUEST_VMCS_LINK_POINTER, 0xFFFFFFFFFFFFFFFFull) != 0) return -1;
    vcpu->guest_regs.rsi = 0x00010000;

    return 0;
}


/*==========================================================
 * 7) Entry / Exit Controls
 *==========================================================*/
static int setup_entry_exit_controls(Vcpu* vcpu)
{
    (void)vcpu; 

    /* ---- VM-Entry Controls ---- */
    {
        vmx_entry_ctrl_t ent = { .raw = 0 };

        ent.raw &= ~ENTRY_CTRL_IA32E_MODE_GUEST;
        ent.raw |=  ENTRY_CTRL_LOAD_EFER;

        if (vmx_entry_commit_adjusted(ent) != 0)
            return -1;
    }

    /* ---- VM-Exit Controls (Primary) ---- */
    {
        vmx_primary_exit_ctrl_t ext = { .raw = 0 };

        ext.raw |= EXIT_CTRL_HOST_ADDR_SPACE_SIZE;
        ext.raw |= EXIT_CTRL_LOAD_EFER;
        ext.raw |= EXIT_CTRL_SAVE_EFER ;

        if (vmx_primary_exit_commit_adjusted(ext) != 0)
            return -1;
    }

    return 0;
}


/*==========================================================
 * 8) VMCS 構築
 *==========================================================*/
int vcpu_build_vmcs(Vcpu* vcpu)
{
    if (setup_exec_controls(vcpu) != 0)         { KLOG_ERROR("vcpu","exec ctrls"); return -1; }
    if (setup_host_state(vcpu) != 0)            { KLOG_ERROR("vcpu","host state"); return -1; }
    if (setup_guest_state(vcpu) != 0)           { KLOG_ERROR("vcpu","guest state"); return -1; }
    if (setup_entry_exit_controls(vcpu) != 0)   { KLOG_ERROR("vcpu","entry/exit"); return -1; }
    return 0;
}


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
            KLOG_DEBUG("vmexit", "HLT -> step RIP (0x%llx)", rip);
            break;
        }
        default:
        {
            uint64_t rip=0, rsp=0, rflags=0;
            vmcs_vmread(VMCS_GUEST_RIP,    &rip);
            vmcs_vmread(VMCS_GUEST_RSP,    &rsp);
            vmcs_vmread(VMCS_GUEST_RFLAGS, &rflags);

            KLOG_ERROR("vmexit",
                "Unhandled VMEXIT: reason=0x%x at RIP=0x%llx\n"
                " RSP=%016llx RFLAGS=%016llx",
                basic,
                (unsigned long long)rip,
                (unsigned long long)rsp,
                (unsigned long long)rflags
            );

            /* 汎用レジスタを追加でダンプ */
            dump_guest_regs(vcpu);

            /* 必要なら CR0/CR3/CR4 なども併せて出力 */
            uint64_t cr0=0, cr3=0, cr4=0;
            vmcs_vmread(VMCS_GUEST_CR0, &cr0);
            vmcs_vmread(VMCS_GUEST_CR3, &cr3);
            vmcs_vmread(VMCS_GUEST_CR4, &cr4);
            KLOG_ERROR("vmexit",
                " CRs: CR0=%016llx CR3=%016llx CR4=%016llx",
                (unsigned long long)cr0,
                (unsigned long long)cr3,
                (unsigned long long)cr4
            );

            for(;;) __asm__ __volatile__("hlt");
        }
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
            if(!vcpu->launch_done) vcpu->launch_done = 1;
            vmexit_dispatch(vcpu);
            continue;
        }

        /* 失敗: CF/ZF set → Instruction error を読む */
        uint32_t err = vmcs_instruction_error();
        KLOG_ERROR("vmentry", "VM-entry failed: vmxerr=%u", err);
        return -1;
    }
}