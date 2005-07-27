/* -*-  Mode:C; c-basic-offset:4; tab-width:4; indent-tabs-mode:nil -*- */
/*
 * asm-xsi-offsets.c_
 * Copyright (c) 2005, Intel Corporation.
 *      Kun Tian (Kevin Tian) <kevin.tian@intel.com>
 *      Eddie Dong  <eddie.dong@intel.com>
 *      Fred Yang <fred.yang@intel.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place - Suite 330, Boston, MA 02111-1307 USA.
 *
 */

/*
 * Generate definitions needed by assembly language modules.
 * This code generates raw asm output which is post-processed
 * to extract and format the required data.
 */

#include <xen/config.h>
#include <xen/sched.h>
#include <asm/processor.h>
#include <asm/ptrace.h>
#include <public/xen.h>
#ifdef CONFIG_VTI
#include <asm/tlb.h>
#include <asm/regs.h>
#endif // CONFIG_VTI

#define task_struct vcpu

#define DEFINE(sym, val) \
        asm volatile("\n->" #sym " %0 " #val : : "i" (val))

#define BLANK() asm volatile("\n->" : : )

#define OFFSET(_sym, _str, _mem) \
    DEFINE(_sym, offsetof(_str, _mem));

#ifndef CONFIG_VTI
#define SHARED_ARCHINFO_ADDR SHAREDINFO_ADDR
#endif

void foo(void)
{

	DEFINE(XSI_BASE, SHARED_ARCHINFO_ADDR);

	DEFINE(XSI_PSR_I_OFS, offsetof(arch_vcpu_info_t, interrupt_delivery_enabled));
	DEFINE(XSI_PSR_I, (SHARED_ARCHINFO_ADDR+offsetof(arch_vcpu_info_t, interrupt_delivery_enabled)));
	DEFINE(XSI_IPSR, (SHARED_ARCHINFO_ADDR+offsetof(arch_vcpu_info_t, ipsr)));
	DEFINE(XSI_IPSR_OFS, offsetof(arch_vcpu_info_t, ipsr));
	DEFINE(XSI_IIP_OFS, offsetof(arch_vcpu_info_t, iip));
	DEFINE(XSI_IIP, (SHARED_ARCHINFO_ADDR+offsetof(arch_vcpu_info_t, iip)));
	DEFINE(XSI_IFS_OFS, offsetof(arch_vcpu_info_t, ifs));
	DEFINE(XSI_IFS, (SHARED_ARCHINFO_ADDR+offsetof(arch_vcpu_info_t, ifs)));
	DEFINE(XSI_PRECOVER_IFS_OFS, offsetof(arch_vcpu_info_t, precover_ifs));
	DEFINE(XSI_PRECOVER_IFS, (SHARED_ARCHINFO_ADDR+offsetof(arch_vcpu_info_t, precover_ifs)));
	DEFINE(XSI_ISR_OFS, offsetof(arch_vcpu_info_t, isr));
	DEFINE(XSI_ISR, (SHARED_ARCHINFO_ADDR+offsetof(arch_vcpu_info_t, isr)));
	DEFINE(XSI_IFA_OFS, offsetof(arch_vcpu_info_t, ifa));
	DEFINE(XSI_IFA, (SHARED_ARCHINFO_ADDR+offsetof(arch_vcpu_info_t, ifa)));
	DEFINE(XSI_IIPA_OFS, offsetof(arch_vcpu_info_t, iipa));
	DEFINE(XSI_IIPA, (SHARED_ARCHINFO_ADDR+offsetof(arch_vcpu_info_t, iipa)));
	DEFINE(XSI_IIM_OFS, offsetof(arch_vcpu_info_t, iim));
	DEFINE(XSI_IIM, (SHARED_ARCHINFO_ADDR+offsetof(arch_vcpu_info_t, iim)));
	DEFINE(XSI_TPR_OFS, offsetof(arch_vcpu_info_t, tpr));
	DEFINE(XSI_TPR, (SHARED_ARCHINFO_ADDR+offsetof(arch_vcpu_info_t, tpr)));
	DEFINE(XSI_IHA_OFS, offsetof(arch_vcpu_info_t, iha));
	DEFINE(XSI_IHA, (SHARED_ARCHINFO_ADDR+offsetof(arch_vcpu_info_t, iha)));
	DEFINE(XSI_ITIR_OFS, offsetof(arch_vcpu_info_t, itir));
	DEFINE(XSI_ITIR, (SHARED_ARCHINFO_ADDR+offsetof(arch_vcpu_info_t, itir)));
	DEFINE(XSI_ITV_OFS, offsetof(arch_vcpu_info_t, itv));
	DEFINE(XSI_ITV, (SHARED_ARCHINFO_ADDR+offsetof(arch_vcpu_info_t, itv)));
	DEFINE(XSI_PTA_OFS, offsetof(arch_vcpu_info_t, pta));
	DEFINE(XSI_PTA, (SHARED_ARCHINFO_ADDR+offsetof(arch_vcpu_info_t, pta)));
	DEFINE(XSI_PSR_IC_OFS, offsetof(arch_vcpu_info_t, interrupt_collection_enabled));
	DEFINE(XSI_PSR_IC, (SHARED_ARCHINFO_ADDR+offsetof(arch_vcpu_info_t, interrupt_collection_enabled)));
	DEFINE(XSI_PEND_OFS, offsetof(arch_vcpu_info_t, pending_interruption));
	DEFINE(XSI_PEND, (SHARED_ARCHINFO_ADDR+offsetof(arch_vcpu_info_t, pending_interruption)));
	DEFINE(XSI_INCOMPL_REGFR_OFS, offsetof(arch_vcpu_info_t, incomplete_regframe));
	DEFINE(XSI_INCOMPL_REGFR, (SHARED_ARCHINFO_ADDR+offsetof(arch_vcpu_info_t, incomplete_regframe)));
	DEFINE(XSI_DELIV_MASK0_OFS, offsetof(arch_vcpu_info_t, delivery_mask[0]));
	DEFINE(XSI_DELIV_MASK0, (SHARED_ARCHINFO_ADDR+offsetof(arch_vcpu_info_t, delivery_mask[0])));
	DEFINE(XSI_METAPHYS_OFS, offsetof(arch_vcpu_info_t, metaphysical_mode));
	DEFINE(XSI_METAPHYS, (SHARED_ARCHINFO_ADDR+offsetof(arch_vcpu_info_t, metaphysical_mode)));

	DEFINE(XSI_BANKNUM_OFS, offsetof(arch_vcpu_info_t, banknum));
	DEFINE(XSI_BANKNUM, (SHARED_ARCHINFO_ADDR+offsetof(arch_vcpu_info_t, banknum)));

	DEFINE(XSI_BANK0_R16_OFS, offsetof(arch_vcpu_info_t, bank0_regs[0]));
	DEFINE(XSI_BANK0_R16, (SHARED_ARCHINFO_ADDR+offsetof(arch_vcpu_info_t, bank0_regs[0])));
	DEFINE(XSI_BANK1_R16_OFS, offsetof(arch_vcpu_info_t, bank1_regs[0]));
	DEFINE(XSI_BANK1_R16, (SHARED_ARCHINFO_ADDR+offsetof(arch_vcpu_info_t, bank1_regs[0])));
	DEFINE(XSI_RR0_OFS, offsetof(arch_vcpu_info_t, rrs[0]));
	DEFINE(XSI_RR0, (SHARED_ARCHINFO_ADDR+offsetof(arch_vcpu_info_t, rrs[0])));
	DEFINE(XSI_KR0_OFS, offsetof(arch_vcpu_info_t, krs[0]));
	DEFINE(XSI_KR0, (SHARED_ARCHINFO_ADDR+offsetof(arch_vcpu_info_t, krs[0])));
	DEFINE(XSI_PKR0_OFS, offsetof(arch_vcpu_info_t, pkrs[0]));
	DEFINE(XSI_PKR0, (SHARED_ARCHINFO_ADDR+offsetof(arch_vcpu_info_t, pkrs[0])));
	DEFINE(XSI_TMP0_OFS, offsetof(arch_vcpu_info_t, tmp[0]));
	DEFINE(XSI_TMP0, (SHARED_ARCHINFO_ADDR+offsetof(arch_vcpu_info_t, tmp[0])));
	
}
