
#ifndef __X86_64_PAGE_H__
#define __X86_64_PAGE_H__

#define L1_PAGETABLE_SHIFT      12
#define L2_PAGETABLE_SHIFT      21
#define L3_PAGETABLE_SHIFT      30
#define L4_PAGETABLE_SHIFT      39
#define PAGE_SHIFT              L1_PAGETABLE_SHIFT
#define ROOT_PAGETABLE_SHIFT    L4_PAGETABLE_SHIFT

#define PAGETABLE_ORDER         9
#define L1_PAGETABLE_ENTRIES    (1<<PAGETABLE_ORDER)
#define L2_PAGETABLE_ENTRIES    (1<<PAGETABLE_ORDER)
#define L3_PAGETABLE_ENTRIES    (1<<PAGETABLE_ORDER)
#define L4_PAGETABLE_ENTRIES    (1<<PAGETABLE_ORDER)
#define ROOT_PAGETABLE_ENTRIES  L4_PAGETABLE_ENTRIES

#define __PAGE_OFFSET           (0xFFFF830000000000)

/* These are architectural limits. Current CPUs support only 40-bit phys. */
#define PADDR_BITS              52
#define VADDR_BITS              48
#define PADDR_MASK              ((1UL << PADDR_BITS)-1)
#define VADDR_MASK              ((1UL << VADDR_BITS)-1)

#ifndef __ASSEMBLY__

#include <xen/config.h>
#include <asm/types.h>

/* read access (should only be used for debug printk's) */
typedef u64 intpte_t;
#define PRIpte "016lx"

typedef struct { intpte_t l1; } l1_pgentry_t;
typedef struct { intpte_t l2; } l2_pgentry_t;
typedef struct { intpte_t l3; } l3_pgentry_t;
typedef struct { intpte_t l4; } l4_pgentry_t;
typedef l4_pgentry_t root_pgentry_t;

#endif /* !__ASSEMBLY__ */

/* Given a virtual address, get an entry offset into a linear page table. */
#define l1_linear_offset(_a) (((_a) & VADDR_MASK) >> PAGE_SHIFT)

#define is_guest_l1_slot(_s) (1)
#define is_guest_l2_slot(_t, _s) (1)
#define is_guest_l3_slot(_s) (1)
#define is_guest_l4_slot(_s)                   \
    (((_s) < ROOT_PAGETABLE_FIRST_XEN_SLOT) || \
     ((_s) > ROOT_PAGETABLE_LAST_XEN_SLOT))

#define root_get_pfn              l4e_get_pfn
#define root_get_flags            l4e_get_flags
#define root_get_intpte           l4e_get_intpte
#define root_empty                l4e_empty
#define root_from_paddr           l4e_from_paddr
#define PGT_root_page_table       PGT_l4_page_table

/*
 * PTE pfn and flags:
 *  40-bit pfn   = (pte[51:12])
 *  24-bit flags = (pte[63:52],pte[11:0])
 */

/* Extract flags into 24-bit integer, or turn 24-bit flags into a pte mask. */
#define get_pte_flags(x) (((int)((x) >> 40) & ~0xFFF) | ((int)(x) & 0xFFF))
#define put_pte_flags(x) (((intpte_t)((x) & ~0xFFF) << 40) | ((x) & 0xFFF))

/* Bit 23 of a 24-bit flag mask. This corresponds to bit 63 of a pte.*/
#define _PAGE_NX (cpu_has_nx ? (1U<<23) : 0U)

#define L1_DISALLOW_MASK (0xFFFFF180U & ~_PAGE_NX) /* PAT/GLOBAL */
#define L2_DISALLOW_MASK (0xFFFFF180U & ~_PAGE_NX) /* PSE/GLOBAL */
#define L3_DISALLOW_MASK (0xFFFFF180U & ~_PAGE_NX) /* must-be-zero */
#define L4_DISALLOW_MASK (0xFFFFF180U & ~_PAGE_NX) /* must-be-zero */

#define PAGE_HYPERVISOR         (__PAGE_HYPERVISOR         | _PAGE_GLOBAL)
#define PAGE_HYPERVISOR_NOCACHE (__PAGE_HYPERVISOR_NOCACHE | _PAGE_GLOBAL)

#endif /* __X86_64_PAGE_H__ */

/*
 * Local variables:
 * mode: C
 * c-set-style: "BSD"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
