/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 * Copyright (C) IBM Corp. 2005
 *
 * Authors: Jimi Xenidis <jimix@watson.ibm.com>
 */

/*
 * Details of the 970-specific HID registers.
 */

#ifndef _ASM_HID_H_
#define _ASM_HID_H_

#include <xen/types.h>

union hid0 {
    struct hid0_bits {
        ulong   one_ppc:        1; /* One PowerPC AS insn per dispatch group */
        ulong   do_single:      1; /* Single group completion */
        ulong   isync_sc:       1; /* Disable isync scoreboard optimization */
        ulong   ser_gp:         1; /* Serial Group Dispatch */
        ulong  _reserved_04_08: 5;
        ulong   nap:            1; /* Nap */
        ulong   _reserved_10:   1;
        ulong   dpm:            1; /* Dynamic Power Management */
        ulong   _reserved_12:   1;
        ulong   tg:             1; /* Perfmon threshold granualrity control */
        ulong   hang_dis:       1; /* Disable cpu hang detection mechanism */
        ulong   nhr:            1; /* Not Hard Reset */
        ulong   inorder:        1; /* Serial Group Issue */
        ulong   _reserved17:    1;
        ulong   tb_ctrl:        1; /* Enable time base couting while stopped */
        ulong   ext_tb_enb:     1; /* timebase is linked to external clock */
        ulong   _unused_20_21:  2;
        ulong   ciabr_en:       1;  /* CIABR enable */
        ulong   hdice_en:       1;  /* HDEC enable */
        ulong   en_therm:       1;  /* Enable ext thermal ints */
        ulong   _unused_25_30:  6;
        ulong   en_attn:        1;  /* Enable attn instruction */
        ulong   en_mck:         1;  /* En external machine check interrupts */
        ulong   _unused_33_63:  31;
    } bits;
    ulong word;
};

union hid1 {
    struct hid1_bits {
        ulong bht_pm:           3; /* branch history table prediction mode */
        ulong en_ls:            1; /* enable link stack */
        ulong en_cc:            1; /* enable count cache */
        ulong en_ic:            1; /* enable inst cache */
        ulong _reserved_6:      1;
        ulong pf_mode:          2; /* prefetch mode */
        ulong en_icbi:          1; /* enable forced icbi match mode */
        ulong en_if_cach:       1; /* i-fetch cacheability control */
        ulong en_ic_rec:        1; /* i-cache parity error recovery */
        ulong en_id_rec:        1; /* i-dir parity error recovery */
        ulong en_er_rec:        1; /* i-ERAT parity error recovery */
        ulong ic_pe:            1; /* Force instruction cache parity error */
        ulong icd0_pe:          1; /* Force insn cache dir 0 parity error */
        ulong _reserved_16:     1;
        ulong ier_pe:           1; /* force i-ERAT parity error (inject) */
        ulong en_sp_itw:        1; /* En speculative tablewalks */
        ulong _reserved_19_63:  45;
    } bits;
    ulong word;
};

union hid4 {
    struct hid4_bits {
        ulong   lpes_0:         1; /* LPAR Environment Selector bit 0 */
        ulong   rmlr_1_2:       2; /* RMLR 1:2 */
        ulong   lpid_2_5:       4; /* LPAR ID bits 2:5 */
        ulong   rmor_0_15:     16; /* real mode offset region */
        ulong   rm_ci:          1; /* real mode cache-inhibit */
        ulong   force_ai:       1; /* Force alignment interrupt */
        ulong   dis_pref:       1; /* disable prefetching */
        ulong   res_pref:       1; /* reset data prefetching mechanism */
        ulong   en_sp_dtw:      1; /* enable speculative load tablewalk */
        ulong   l1dc_flsh:      1; /* L1 cache flash invalidate */
        ulong   dis_derpc:      2; /* Disable d-ERAT parity checking */
        ulong   dis_derpg:      1; /* Disable d-ERAT parity generation */
        ulong   dis_derat:      2; /* Disable d-ERAT */
        ulong   dis_dctpc:      2; /* Dis data cache tag paritiy checking */
        ulong   dis_dctpg:      1; /* Dis data cache tag paritiy generation */
        ulong   dis_dcset:      2; /* Disable data cache set */
        ulong   dis_dcpc:       2; /* Disable data cache paritiy checking */
        ulong   dis_dcpg:       1; /* Disable data cache paritiy generation */
        ulong   dis_dcrtpc:     2; /* Disable data cache real add tag parity */
        ulong   dis_tlbpc:      4; /* Disable TLB paritiy checking */
        ulong   dis_tlbpg:      1; /* Disable TLB paritiy generation */
        ulong   dis_tlbset:     4; /* Disable TLB set */
        ulong   dis_slbpc:      1; /* Disable SLB paritiy checking */
        ulong   dis_slbpg:      1; /* Disable SLB paritiy generation */
        ulong   mck_inj:        1; /* Machine check inject enable */
        ulong   dis_stfwd:      1; /* Disbale store forwarding */
        ulong   lpes_1:         1;  /* LPAR Environment Selector bit 1 */
        ulong   rmlr_0:         1;  /* RMLR 0 */
        ulong   _reserved:      1;
        ulong   dis_splarx:     1;  /* Disable spec. lwarx/ldarx */
        ulong   lg_pg_dis:      1;  /* Disable large page support */
        ulong   lpid_0_1:       2;  /* LPAR ID bits 0:1 */
    } bits;
    ulong word;
};

union hid5 {
    struct hid5_bits {
        ulong _reserved_0_31: 32;
        ulong hrmor_0_15:     16;
        ulong _reserved_48_49: 2;
        ulong DC_mck:          1; /* Machine check enabled for dcache errors */
        ulong dis_pwrsave:     1; /* Dis pwrsave on on L1 and d-ERAT */
        ulong force_G:         1; /* Force gaurded load */
        ulong DC_repl:         1; /* D-Cache replacement algo */
        ulong hwr_stms:        1; /* Number of available HW prefetch streams */
        ulong dst_noop:        1; /* D-stream Touch no-op */
        ulong DCBZ_size:       1; /* make dcbz size 32 bytes */
        ulong DCBZ32_ill:      1; /* make dzbz 32byte illeagal */
        ulong tlb_map:         1; /* TLB mapping */
        ulong lmq_port:        1; /* Demand miss (LMQ to STS) */
        ulong lmq_size_0:      1; /* number of outstanding req. to STS */
        ulong  _reserved_61:   1;
        ulong  tch_nop:        1; /* make dcbtand dcbtst ack like no-ops */
        ulong  lmq_size_1:     1; /* second bit to lmq_size_0 */
    } bits;
    ulong word;
};

#define MCK_SRR1_INSN_FETCH_UNIT    0x0000000000200000 /* 42 */
#define MCK_SRR1_LOAD_STORE         0x0000000000100000 /* 43 */
#define MCK_SRR1_CAUSE_MASK         0x00000000000c0000 /* 44:45 */
#define MCK_SRR1_CAUSE_NONE         0x0000000000000000 /* 0b00 */
#define MCK_SRR1_CAUSE_SLB_PAR      0x0000000000040000 /* 0b01 */
#define MCK_SRR1_CAUSE_TLB_PAR      0x0000000000080000 /* 0b10 */
#define MCK_SRR1_CAUSE_UE           0x00000000000c0000 /* 0b11 */
#define MCK_SRR1_RI                 MSR_RI

#define MCK_DSISR_UE                0x00008000 /* 16 */
#define MCK_DSISR_UE_TABLE_WALK     0x00004000 /* 17 */
#define MCK_DSISR_L1_DCACHE_PAR     0x00002000 /* 18 */
#define MCK_DSISR_L1_DCACHE_TAG_PAR 0x00001000 /* 19 */
#define MCK_DSISR_D_ERAT_PAR        0x00000800 /* 20 */
#define MCK_DSISR_TLB_PAR           0x00000400 /* 21 */
#define MCK_DSISR_SLB_PAR           0x00000100 /* 23 */

#define SPRN_SCOMC 276
#define SPRN_SCOMD 277

static inline void mtscomc(ulong scomc)
{
    __asm__ __volatile__ ("mtspr %1, %0" : : "r" (scomc), "i"(SPRN_SCOMC));
}

static inline ulong mfscomc(void)
{
    ulong scomc;
    __asm__ __volatile__ ("mfspr %0, %1" : "=r" (scomc): "i"(SPRN_SCOMC));
    return scomc;
}

static inline void mtscomd(ulong scomd)
{
    __asm__ __volatile__ ("mtspr %1, %0" : : "r" (scomd), "i"(SPRN_SCOMD));
}

static inline ulong mfscomd(void)
{
    ulong scomd;
    __asm__ __volatile__ ("mfspr %0, %1" : "=r" (scomd): "i"(SPRN_SCOMD));
    return scomd;
}

union scomc {
    struct scomc_bits {
        ulong _reserved_0_31: 32;
        ulong addr:           16;
        ulong RW:              1;
        ulong _reserved_49_55: 7;
        ulong _reserved_56_57: 2;
        ulong addr_error:      1;
        ulong iface_error:     1;
        ulong disabled:        1;
        ulong _reserved_61_62: 2;
        ulong failure:         1;
    } bits;
    ulong word;
};


static inline ulong read_scom(ulong addr)
{
    union scomc c;
    ulong d;

    c.word = 0;
    c.bits.addr = addr;
    c.bits.RW = 0;

    mtscomc(c.word);
    d = mfscomd();
    c.word = mfscomc();
    if (c.bits.failure)
        panic("scom status: 0x%016lx\n", c.word);

    return d;
}

static inline void write_scom(ulong addr, ulong val)
{
    union scomc c;

    c.word = 0;
    c.bits.addr = addr;
    c.bits.RW = 0;

    mtscomd(val);
    mtscomc(c.word);
    c.word = mfscomc();
    if (c.bits.failure)
        panic("scom status: 0x%016lx\n", c.word);
}

#define SCOM_AMCS_REG      0x022601
#define SCOM_AMCS_AND_MASK 0x022700
#define SCOM_AMCS_OR_MASK  0x022800
#define SCOM_CMCE          0x030901
#endif
