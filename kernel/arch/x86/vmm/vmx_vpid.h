#pragma once
#include <stdint.h>
#include "arch/x86/vmm/ept.h"
#include "arch/x86/vmm/vmcs.h"
#include "arch/x86/vmm/vmx.h"
#include "arch/x86/vmm/vcpu.h"
#include "arch/x86/msr.h"

/* Intel SDM Vol.3C 29.6: IA32_VMX_EPT_VPID_CAP[bits] を C の bitfield で再現 */
typedef union vmx_ept_vpid_cap_u {
    uint64_t u64;
    struct {
        uint64_t ept_exec_only              : 1;  /* [0]   */
        uint64_t _rsvd1                     : 5;  /* [1:5] */
        uint64_t ept_lv4                    : 1;  /* [6]   */
        uint64_t ept_lv5                    : 1;  /* [7]   */
        uint64_t ept_uc                     : 1;  /* [8]   */
        uint64_t _rsvd2                     : 5;  /* [9:13]*/
        uint64_t ept_wb                     : 1;  /* [14]  */
        uint64_t _rsvd3                     : 1;  /* [15]  */
        uint64_t ept_2m                     : 1;  /* [16]  */
        uint64_t ept_1g                     : 1;  /* [17]  */
        uint64_t _rsvd4                     : 2;  /* [18:19] */
        uint64_t invept                     : 1;  /* [20]  */
        uint64_t ept_dirty                  : 1;  /* [21]  */
        uint64_t ept_advanced_exit          : 1;  /* [22]  */
        uint64_t shadow_stack               : 1;  /* [23]  */
        uint64_t _rsvd5                     : 1;  /* [24]  */
        uint64_t invept_single              : 1;  /* [25]  */
        uint64_t invept_all                 : 1;  /* [26]  */
        uint64_t _rsvd6                     : 5;  /* [27:31] */
        uint64_t invvpid                    : 1;  /* [32]  */
        uint64_t _rsvd7                     : 7;  /* [33:39] */
        uint64_t invvpid_individual         : 1;  /* [40]  */
        uint64_t invvpid_single             : 1;  /* [41]  */
        uint64_t invvpid_all                : 1;  /* [42]  */
        uint64_t invvpid_single_globals     : 1;  /* [43]  */
        uint64_t _rsvd8                     : 4;  /* [44:47] */
        uint64_t hlat_prefix                : 6;  /* [48:53] */
        uint64_t _rsvd9                     : 10; /* [54:63] */
    } bits;
} vmx_ept_vpid_cap_t;

int vmx_is_vpid_supported(void);
int vmx_write_vpid(uint16_t vpid);