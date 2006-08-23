/******************************************************************************
 * include/asm-x86/shadow2-types.h
 * 
 * Parts of this code are Copyright (c) 2006 by XenSource Inc.
 * Parts of this code are Copyright (c) 2006 by Michael A Fetterman
 * Parts based on earlier work by Michael A Fetterman, Ian Pratt et al.
 * 
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef _XEN_SHADOW2_TYPES_H
#define _XEN_SHADOW2_TYPES_H

// Map a shadow page
static inline void *
map_shadow_page(mfn_t smfn)
{
    // XXX -- Possible optimization/measurement question for 32-bit and PAE
    //        hypervisors:
    //        How often is this smfn already available in the shadow linear
    //        table?  Might it be worth checking that table first,
    //        presumably using the reverse map hint in the page_info of this
    //        smfn, rather than calling map_domain_page()?
    //
    return sh2_map_domain_page(smfn);
}

// matching unmap for map_shadow_page()
static inline void
unmap_shadow_page(void *p)
{
    sh2_unmap_domain_page(p);
}

/* 
 * Define various types for handling pagetabels, based on these options:
 * SHADOW_PAGING_LEVELS : Number of levels of shadow pagetables
 * GUEST_PAGING_LEVELS  : Number of levels of guest pagetables
 */

#if (CONFIG_PAGING_LEVELS < SHADOW_PAGING_LEVELS) 
#error Cannot have more levels of shadow pagetables than host pagetables
#endif

#if (SHADOW_PAGING_LEVELS < GUEST_PAGING_LEVELS) 
#error Cannot have more levels of guest pagetables than shadow pagetables
#endif

#if SHADOW_PAGING_LEVELS == 2
#define SHADOW_L1_PAGETABLE_ENTRIES    1024
#define SHADOW_L2_PAGETABLE_ENTRIES    1024
#define SHADOW_L1_PAGETABLE_SHIFT        12
#define SHADOW_L2_PAGETABLE_SHIFT        22
#endif

#if SHADOW_PAGING_LEVELS == 3
#define SHADOW_L1_PAGETABLE_ENTRIES     512
#define SHADOW_L2_PAGETABLE_ENTRIES     512
#define SHADOW_L3_PAGETABLE_ENTRIES       4
#define SHADOW_L1_PAGETABLE_SHIFT        12
#define SHADOW_L2_PAGETABLE_SHIFT        21
#define SHADOW_L3_PAGETABLE_SHIFT        30
#endif

#if SHADOW_PAGING_LEVELS == 4
#define SHADOW_L1_PAGETABLE_ENTRIES     512
#define SHADOW_L2_PAGETABLE_ENTRIES     512
#define SHADOW_L3_PAGETABLE_ENTRIES     512
#define SHADOW_L4_PAGETABLE_ENTRIES     512
#define SHADOW_L1_PAGETABLE_SHIFT        12
#define SHADOW_L2_PAGETABLE_SHIFT        21
#define SHADOW_L3_PAGETABLE_SHIFT        30
#define SHADOW_L4_PAGETABLE_SHIFT        39
#endif

/* Types of the shadow page tables */
typedef l1_pgentry_t shadow_l1e_t;
typedef l2_pgentry_t shadow_l2e_t;
#if SHADOW_PAGING_LEVELS >= 3
typedef l3_pgentry_t shadow_l3e_t;
#if SHADOW_PAGING_LEVELS >= 4
typedef l4_pgentry_t shadow_l4e_t;
#endif
#endif

/* Access functions for them */
static inline paddr_t shadow_l1e_get_paddr(shadow_l1e_t sl1e)
{ return l1e_get_paddr(sl1e); }
static inline paddr_t shadow_l2e_get_paddr(shadow_l2e_t sl2e)
{ return l2e_get_paddr(sl2e); }
#if SHADOW_PAGING_LEVELS >= 3
static inline paddr_t shadow_l3e_get_paddr(shadow_l3e_t sl3e)
{ return l3e_get_paddr(sl3e); }
#if SHADOW_PAGING_LEVELS >= 4
static inline paddr_t shadow_l4e_get_paddr(shadow_l4e_t sl4e)
{ return l4e_get_paddr(sl4e); }
#endif
#endif

static inline mfn_t shadow_l1e_get_mfn(shadow_l1e_t sl1e)
{ return _mfn(l1e_get_pfn(sl1e)); }
static inline mfn_t shadow_l2e_get_mfn(shadow_l2e_t sl2e)
{ return _mfn(l2e_get_pfn(sl2e)); }
#if SHADOW_PAGING_LEVELS >= 3
static inline mfn_t shadow_l3e_get_mfn(shadow_l3e_t sl3e)
{ return _mfn(l3e_get_pfn(sl3e)); }
#if SHADOW_PAGING_LEVELS >= 4
static inline mfn_t shadow_l4e_get_mfn(shadow_l4e_t sl4e)
{ return _mfn(l4e_get_pfn(sl4e)); }
#endif
#endif

static inline u32 shadow_l1e_get_flags(shadow_l1e_t sl1e)
{ return l1e_get_flags(sl1e); }
static inline u32 shadow_l2e_get_flags(shadow_l2e_t sl2e)
{ return l2e_get_flags(sl2e); }
#if SHADOW_PAGING_LEVELS >= 3
static inline u32 shadow_l3e_get_flags(shadow_l3e_t sl3e)
{ return l3e_get_flags(sl3e); }
#if SHADOW_PAGING_LEVELS >= 4
static inline u32 shadow_l4e_get_flags(shadow_l4e_t sl4e)
{ return l4e_get_flags(sl4e); }
#endif
#endif

static inline shadow_l1e_t
shadow_l1e_remove_flags(shadow_l1e_t sl1e, u32 flags)
{ l1e_remove_flags(sl1e, flags); return sl1e; }

static inline shadow_l1e_t shadow_l1e_empty(void) 
{ return l1e_empty(); }
static inline shadow_l2e_t shadow_l2e_empty(void) 
{ return l2e_empty(); }
#if SHADOW_PAGING_LEVELS >= 3
static inline shadow_l3e_t shadow_l3e_empty(void) 
{ return l3e_empty(); }
#if SHADOW_PAGING_LEVELS >= 4
static inline shadow_l4e_t shadow_l4e_empty(void) 
{ return l4e_empty(); }
#endif
#endif

static inline shadow_l1e_t shadow_l1e_from_mfn(mfn_t mfn, u32 flags)
{ return l1e_from_pfn(mfn_x(mfn), flags); }
static inline shadow_l2e_t shadow_l2e_from_mfn(mfn_t mfn, u32 flags)
{ return l2e_from_pfn(mfn_x(mfn), flags); }
#if SHADOW_PAGING_LEVELS >= 3
static inline shadow_l3e_t shadow_l3e_from_mfn(mfn_t mfn, u32 flags)
{ return l3e_from_pfn(mfn_x(mfn), flags); }
#if SHADOW_PAGING_LEVELS >= 4
static inline shadow_l4e_t shadow_l4e_from_mfn(mfn_t mfn, u32 flags)
{ return l4e_from_pfn(mfn_x(mfn), flags); }
#endif
#endif

#define shadow_l1_table_offset(a) l1_table_offset(a)
#define shadow_l2_table_offset(a) l2_table_offset(a)
#define shadow_l3_table_offset(a) l3_table_offset(a)
#define shadow_l4_table_offset(a) l4_table_offset(a)

/**************************************************************************/
/* Access to the linear mapping of shadow page tables. */

/* Offsets into each level of the linear mapping for a virtual address. */
#define shadow_l1_linear_offset(_a)                                           \
        (((_a) & VADDR_MASK) >> SHADOW_L1_PAGETABLE_SHIFT)
#define shadow_l2_linear_offset(_a)                                           \
        (((_a) & VADDR_MASK) >> SHADOW_L2_PAGETABLE_SHIFT)
#define shadow_l3_linear_offset(_a)                                           \
        (((_a) & VADDR_MASK) >> SHADOW_L3_PAGETABLE_SHIFT)
#define shadow_l4_linear_offset(_a)                                           \
        (((_a) & VADDR_MASK) >> SHADOW_L4_PAGETABLE_SHIFT)

/* Where to find each level of the linear mapping.  For PV guests, we use 
 * the shadow linear-map self-entry as many times as we need.  For HVM 
 * guests, the shadow doesn't have a linear-map self-entry so we must use 
 * the monitor-table's linear-map entry N-1 times and then the shadow-map 
 * entry once. */
#define __sh2_linear_l1_table ((shadow_l1e_t *)(SH_LINEAR_PT_VIRT_START))
#define __sh2_linear_l2_table ((shadow_l2e_t *)                               \
    (__sh2_linear_l1_table + shadow_l1_linear_offset(SH_LINEAR_PT_VIRT_START)))

// shadow linear L3 and L4 tables only exist in 4 level paging...
#if SHADOW_PAGING_LEVELS == 4
#define __sh2_linear_l3_table ((shadow_l3e_t *)                               \
    (__sh2_linear_l2_table + shadow_l2_linear_offset(SH_LINEAR_PT_VIRT_START)))
#define __sh2_linear_l4_table ((shadow_l4e_t *)                               \
    (__sh2_linear_l3_table + shadow_l3_linear_offset(SH_LINEAR_PT_VIRT_START)))
#endif

#define sh2_linear_l1_table(v) ({ \
    ASSERT(current == (v)); \
    __sh2_linear_l1_table; \
})

#define sh2_linear_l2_table(v) ({ \
    ASSERT(current == (v)); \
    ((shadow_l2e_t *) \
     (hvm_guest(v) ? __linear_l1_table : __sh2_linear_l1_table) + \
     shadow_l1_linear_offset(SH_LINEAR_PT_VIRT_START)); \
})

// shadow linear L3 and L4 tables only exist in 4 level paging...
#if SHADOW_PAGING_LEVELS == 4
#define sh2_linear_l3_table(v) ({ \
    ASSERT(current == (v)); \
    ((shadow_l3e_t *) \
     (hvm_guest(v) ? __linear_l2_table : __sh2_linear_l2_table) + \
      shadow_l2_linear_offset(SH_LINEAR_PT_VIRT_START)); \
})

// we use l4_pgentry_t instead of shadow_l4e_t below because shadow_l4e_t is
// not defined for when xen_levels==4 & shadow_levels==3...
#define sh2_linear_l4_table(v) ({ \
    ASSERT(current == (v)); \
    ((l4_pgentry_t *) \
     (hvm_guest(v) ? __linear_l3_table : __sh2_linear_l3_table) + \
      shadow_l3_linear_offset(SH_LINEAR_PT_VIRT_START)); \
})
#endif

#if GUEST_PAGING_LEVELS == 2

#include <asm/page-guest32.h>

#define GUEST_L1_PAGETABLE_ENTRIES     1024
#define GUEST_L2_PAGETABLE_ENTRIES     1024
#define GUEST_L1_PAGETABLE_SHIFT         12
#define GUEST_L2_PAGETABLE_SHIFT         22

/* Type of the guest's frame numbers */
TYPE_SAFE(u32,gfn)
#define INVALID_GFN ((u32)(-1u))
#define SH2_PRI_gfn "05x"

/* Types of the guest's page tables */
typedef l1_pgentry_32_t guest_l1e_t;
typedef l2_pgentry_32_t guest_l2e_t;

/* Access functions for them */
static inline paddr_t guest_l1e_get_paddr(guest_l1e_t gl1e)
{ return l1e_get_paddr_32(gl1e); }
static inline paddr_t guest_l2e_get_paddr(guest_l2e_t gl2e)
{ return l2e_get_paddr_32(gl2e); }

static inline gfn_t guest_l1e_get_gfn(guest_l1e_t gl1e)
{ return _gfn(l1e_get_paddr_32(gl1e) >> PAGE_SHIFT); }
static inline gfn_t guest_l2e_get_gfn(guest_l2e_t gl2e)
{ return _gfn(l2e_get_paddr_32(gl2e) >> PAGE_SHIFT); }

static inline u32 guest_l1e_get_flags(guest_l1e_t gl1e)
{ return l1e_get_flags_32(gl1e); }
static inline u32 guest_l2e_get_flags(guest_l2e_t gl2e)
{ return l2e_get_flags_32(gl2e); }

static inline guest_l1e_t guest_l1e_add_flags(guest_l1e_t gl1e, u32 flags)
{ l1e_add_flags_32(gl1e, flags); return gl1e; }
static inline guest_l2e_t guest_l2e_add_flags(guest_l2e_t gl2e, u32 flags)
{ l2e_add_flags_32(gl2e, flags); return gl2e; }

static inline guest_l1e_t guest_l1e_from_gfn(gfn_t gfn, u32 flags)
{ return l1e_from_pfn_32(gfn_x(gfn), flags); }
static inline guest_l2e_t guest_l2e_from_gfn(gfn_t gfn, u32 flags)
{ return l2e_from_pfn_32(gfn_x(gfn), flags); }

#define guest_l1_table_offset(a) l1_table_offset_32(a)
#define guest_l2_table_offset(a) l2_table_offset_32(a)

/* The shadow types needed for the various levels. */
#define PGC_SH2_l1_shadow  PGC_SH2_l1_32_shadow
#define PGC_SH2_l2_shadow  PGC_SH2_l2_32_shadow
#define PGC_SH2_fl1_shadow PGC_SH2_fl1_32_shadow

#else /* GUEST_PAGING_LEVELS != 2 */

#if GUEST_PAGING_LEVELS == 3
#define GUEST_L1_PAGETABLE_ENTRIES      512
#define GUEST_L2_PAGETABLE_ENTRIES      512
#define GUEST_L3_PAGETABLE_ENTRIES        4
#define GUEST_L1_PAGETABLE_SHIFT         12
#define GUEST_L2_PAGETABLE_SHIFT         21
#define GUEST_L3_PAGETABLE_SHIFT         30
#else /* GUEST_PAGING_LEVELS == 4 */
#define GUEST_L1_PAGETABLE_ENTRIES      512
#define GUEST_L2_PAGETABLE_ENTRIES      512
#define GUEST_L3_PAGETABLE_ENTRIES      512
#define GUEST_L4_PAGETABLE_ENTRIES      512
#define GUEST_L1_PAGETABLE_SHIFT         12
#define GUEST_L2_PAGETABLE_SHIFT         21
#define GUEST_L3_PAGETABLE_SHIFT         30
#define GUEST_L4_PAGETABLE_SHIFT         39
#endif

/* Type of the guest's frame numbers */
TYPE_SAFE(unsigned long,gfn)
#define INVALID_GFN ((unsigned long)(-1ul))
#define SH2_PRI_gfn "05lx"

/* Types of the guest's page tables */
typedef l1_pgentry_t guest_l1e_t;
typedef l2_pgentry_t guest_l2e_t;
typedef l3_pgentry_t guest_l3e_t;
#if GUEST_PAGING_LEVELS >= 4
typedef l4_pgentry_t guest_l4e_t;
#endif

/* Access functions for them */
static inline paddr_t guest_l1e_get_paddr(guest_l1e_t gl1e)
{ return l1e_get_paddr(gl1e); }
static inline paddr_t guest_l2e_get_paddr(guest_l2e_t gl2e)
{ return l2e_get_paddr(gl2e); }
static inline paddr_t guest_l3e_get_paddr(guest_l3e_t gl3e)
{ return l3e_get_paddr(gl3e); }
#if GUEST_PAGING_LEVELS >= 4
static inline paddr_t guest_l4e_get_paddr(guest_l4e_t gl4e)
{ return l4e_get_paddr(gl4e); }
#endif

static inline gfn_t guest_l1e_get_gfn(guest_l1e_t gl1e)
{ return _gfn(l1e_get_paddr(gl1e) >> PAGE_SHIFT); }
static inline gfn_t guest_l2e_get_gfn(guest_l2e_t gl2e)
{ return _gfn(l2e_get_paddr(gl2e) >> PAGE_SHIFT); }
static inline gfn_t guest_l3e_get_gfn(guest_l3e_t gl3e)
{ return _gfn(l3e_get_paddr(gl3e) >> PAGE_SHIFT); }
#if GUEST_PAGING_LEVELS >= 4
static inline gfn_t guest_l4e_get_gfn(guest_l4e_t gl4e)
{ return _gfn(l4e_get_paddr(gl4e) >> PAGE_SHIFT); }
#endif

static inline u32 guest_l1e_get_flags(guest_l1e_t gl1e)
{ return l1e_get_flags(gl1e); }
static inline u32 guest_l2e_get_flags(guest_l2e_t gl2e)
{ return l2e_get_flags(gl2e); }
static inline u32 guest_l3e_get_flags(guest_l3e_t gl3e)
{ return l3e_get_flags(gl3e); }
#if GUEST_PAGING_LEVELS >= 4
static inline u32 guest_l4e_get_flags(guest_l4e_t gl4e)
{ return l4e_get_flags(gl4e); }
#endif

static inline guest_l1e_t guest_l1e_add_flags(guest_l1e_t gl1e, u32 flags)
{ l1e_add_flags(gl1e, flags); return gl1e; }
static inline guest_l2e_t guest_l2e_add_flags(guest_l2e_t gl2e, u32 flags)
{ l2e_add_flags(gl2e, flags); return gl2e; }
static inline guest_l3e_t guest_l3e_add_flags(guest_l3e_t gl3e, u32 flags)
{ l3e_add_flags(gl3e, flags); return gl3e; }
#if GUEST_PAGING_LEVELS >= 4
static inline guest_l4e_t guest_l4e_add_flags(guest_l4e_t gl4e, u32 flags)
{ l4e_add_flags(gl4e, flags); return gl4e; }
#endif

static inline guest_l1e_t guest_l1e_from_gfn(gfn_t gfn, u32 flags)
{ return l1e_from_pfn(gfn_x(gfn), flags); }
static inline guest_l2e_t guest_l2e_from_gfn(gfn_t gfn, u32 flags)
{ return l2e_from_pfn(gfn_x(gfn), flags); }
static inline guest_l3e_t guest_l3e_from_gfn(gfn_t gfn, u32 flags)
{ return l3e_from_pfn(gfn_x(gfn), flags); }
#if GUEST_PAGING_LEVELS >= 4
static inline guest_l4e_t guest_l4e_from_gfn(gfn_t gfn, u32 flags)
{ return l4e_from_pfn(gfn_x(gfn), flags); }
#endif

#define guest_l1_table_offset(a) l1_table_offset(a)
#define guest_l2_table_offset(a) l2_table_offset(a)
#define guest_l3_table_offset(a) l3_table_offset(a)
#define guest_l4_table_offset(a) l4_table_offset(a)

/* The shadow types needed for the various levels. */
#if GUEST_PAGING_LEVELS == 3
#define PGC_SH2_l1_shadow  PGC_SH2_l1_pae_shadow
#define PGC_SH2_fl1_shadow PGC_SH2_fl1_pae_shadow
#define PGC_SH2_l2_shadow  PGC_SH2_l2_pae_shadow
#define PGC_SH2_l2h_shadow PGC_SH2_l2h_pae_shadow
#define PGC_SH2_l3_shadow  PGC_SH2_l3_pae_shadow
#else
#define PGC_SH2_l1_shadow  PGC_SH2_l1_64_shadow
#define PGC_SH2_fl1_shadow PGC_SH2_fl1_64_shadow
#define PGC_SH2_l2_shadow  PGC_SH2_l2_64_shadow
#define PGC_SH2_l3_shadow  PGC_SH2_l3_64_shadow
#define PGC_SH2_l4_shadow  PGC_SH2_l4_64_shadow
#endif

#endif /* GUEST_PAGING_LEVELS != 2 */

#define VALID_GFN(m) (m != INVALID_GFN)

static inline int
valid_gfn(gfn_t m)
{
    return VALID_GFN(gfn_x(m));
}

#if GUEST_PAGING_LEVELS == 2
#define PGC_SH2_guest_root_type PGC_SH2_l2_32_shadow
#elif GUEST_PAGING_LEVELS == 3
#define PGC_SH2_guest_root_type PGC_SH2_l3_pae_shadow
#else
#define PGC_SH2_guest_root_type PGC_SH2_l4_64_shadow
#endif

/* Translation between mfns and gfns */
static inline mfn_t
vcpu_gfn_to_mfn(struct vcpu *v, gfn_t gfn)
{
    return sh2_vcpu_gfn_to_mfn(v, gfn_x(gfn));
} 

static inline gfn_t
mfn_to_gfn(struct domain *d, mfn_t mfn)
{
    return _gfn(sh2_mfn_to_gfn(d, mfn));
}

static inline paddr_t
gfn_to_paddr(gfn_t gfn)
{
    return ((paddr_t)gfn_x(gfn)) << PAGE_SHIFT;
}

/* Type used for recording a walk through guest pagetables.  It is
 * filled in by the pagetable walk function, and also used as a cache
 * for later walks.  
 * Any non-null pointer in this structure represents a mapping of guest
 * memory.  We must always call walk_init() before using a walk_t, and 
 * call walk_unmap() when we're done. 
 * The "Effective l1e" field is used when there isn't an l1e to point to, 
 * but we have fabricated an l1e for propagation to the shadow (e.g., 
 * for splintering guest superpages into many shadow l1 entries).  */
typedef struct shadow2_walk_t walk_t;
struct shadow2_walk_t 
{
    unsigned long va;           /* Address we were looking for */
#if GUEST_PAGING_LEVELS >= 3
#if GUEST_PAGING_LEVELS >= 4
    guest_l4e_t *l4e;           /* Pointer to guest's level 4 entry */
#endif
    guest_l3e_t *l3e;           /* Pointer to guest's level 3 entry */
#endif
    guest_l2e_t *l2e;           /* Pointer to guest's level 2 entry */
    guest_l1e_t *l1e;           /* Pointer to guest's level 1 entry */
    guest_l1e_t eff_l1e;        /* Effective level 1 entry */
#if GUEST_PAGING_LEVELS >= 3
#if GUEST_PAGING_LEVELS >= 4
    mfn_t l4mfn;                /* MFN that the level 4 entry is in */
#endif
    mfn_t l3mfn;                /* MFN that the level 3 entry is in */
#endif
    mfn_t l2mfn;                /* MFN that the level 2 entry is in */
    mfn_t l1mfn;                /* MFN that the level 1 entry is in */
};


/* X86 error code bits:
 * These bits certainly ought to be defined somewhere other than here,
 * but until that place is determined, here they sit.
 *
 * "PFEC" == "Page Fault Error Code"
 */
#define X86_PFEC_PRESENT            1  /* 0 == page was not present */
#define X86_PFEC_WRITE_FAULT        2  /* 0 == reading, 1 == writing */
#define X86_PFEC_SUPERVISOR_FAULT   4  /* 0 == supervisor-mode, 1 == user */
#define X86_PFEC_RESERVED_BIT_FAULT 8  /* 1 == reserved bits set in pte */
#define X86_PFEC_INSN_FETCH_FAULT  16  /* 0 == normal, 1 == instr'n fetch */

/* macros for dealing with the naming of the internal function names of the
 * shadow code's external entry points.
 */
#define INTERNAL_NAME(name) \
    SHADOW2_INTERNAL_NAME(name, SHADOW_PAGING_LEVELS, GUEST_PAGING_LEVELS)

/* macros for renaming the primary entry points, so that they are more
 * easily distinguished from a debugger
 */
#define sh2_page_fault              INTERNAL_NAME(sh2_page_fault)
#define sh2_invlpg                  INTERNAL_NAME(sh2_invlpg)
#define sh2_gva_to_gpa              INTERNAL_NAME(sh2_gva_to_gpa)
#define sh2_gva_to_gfn              INTERNAL_NAME(sh2_gva_to_gfn)
#define sh2_update_cr3              INTERNAL_NAME(sh2_update_cr3)
#define sh2_remove_write_access     INTERNAL_NAME(sh2_remove_write_access)
#define sh2_remove_all_mappings     INTERNAL_NAME(sh2_remove_all_mappings)
#define sh2_remove_l1_shadow        INTERNAL_NAME(sh2_remove_l1_shadow)
#define sh2_remove_l2_shadow        INTERNAL_NAME(sh2_remove_l2_shadow)
#define sh2_remove_l3_shadow        INTERNAL_NAME(sh2_remove_l3_shadow)
#define sh2_map_and_validate_gl4e   INTERNAL_NAME(sh2_map_and_validate_gl4e)
#define sh2_map_and_validate_gl3e   INTERNAL_NAME(sh2_map_and_validate_gl3e)
#define sh2_map_and_validate_gl2e   INTERNAL_NAME(sh2_map_and_validate_gl2e)
#define sh2_map_and_validate_gl2he  INTERNAL_NAME(sh2_map_and_validate_gl2he)
#define sh2_map_and_validate_gl1e   INTERNAL_NAME(sh2_map_and_validate_gl1e)
#define sh2_destroy_l4_shadow       INTERNAL_NAME(sh2_destroy_l4_shadow)
#define sh2_destroy_l3_shadow       INTERNAL_NAME(sh2_destroy_l3_shadow)
#define sh2_destroy_l3_subshadow    INTERNAL_NAME(sh2_destroy_l3_subshadow)
#define sh2_unpin_all_l3_subshadows INTERNAL_NAME(sh2_unpin_all_l3_subshadows)
#define sh2_destroy_l2_shadow       INTERNAL_NAME(sh2_destroy_l2_shadow)
#define sh2_destroy_l1_shadow       INTERNAL_NAME(sh2_destroy_l1_shadow)
#define sh2_unhook_32b_mappings     INTERNAL_NAME(sh2_unhook_32b_mappings)
#define sh2_unhook_pae_mappings     INTERNAL_NAME(sh2_unhook_pae_mappings)
#define sh2_unhook_64b_mappings     INTERNAL_NAME(sh2_unhook_64b_mappings)
#define sh2_paging_mode             INTERNAL_NAME(sh2_paging_mode)
#define sh2_detach_old_tables       INTERNAL_NAME(sh2_detach_old_tables)
#define sh2_x86_emulate_write       INTERNAL_NAME(sh2_x86_emulate_write)
#define sh2_x86_emulate_cmpxchg     INTERNAL_NAME(sh2_x86_emulate_cmpxchg)
#define sh2_x86_emulate_cmpxchg8b   INTERNAL_NAME(sh2_x86_emulate_cmpxchg8b)
#define sh2_audit_l1_table          INTERNAL_NAME(sh2_audit_l1_table)
#define sh2_audit_fl1_table         INTERNAL_NAME(sh2_audit_fl1_table)
#define sh2_audit_l2_table          INTERNAL_NAME(sh2_audit_l2_table)
#define sh2_audit_l3_table          INTERNAL_NAME(sh2_audit_l3_table)
#define sh2_audit_l4_table          INTERNAL_NAME(sh2_audit_l4_table)
#define sh2_guess_wrmap             INTERNAL_NAME(sh2_guess_wrmap)
#define sh2_clear_shadow_entry      INTERNAL_NAME(sh2_clear_shadow_entry)

/* sh2_make_monitor_table only depends on the number of shadow levels */
#define sh2_make_monitor_table                          \
        SHADOW2_INTERNAL_NAME(sh2_make_monitor_table,   \
                              SHADOW_PAGING_LEVELS,     \
                              SHADOW_PAGING_LEVELS)
#define sh2_destroy_monitor_table                               \
        SHADOW2_INTERNAL_NAME(sh2_destroy_monitor_table,        \
                              SHADOW_PAGING_LEVELS,             \
                              SHADOW_PAGING_LEVELS)


#if GUEST_PAGING_LEVELS == 3
/*
 * Accounting information stored in the shadow of PAE Guest L3 pages.
 * Because these "L3 pages" are only 32-bytes, it is inconvenient to keep
 * various refcounts, etc., on the page_info of their page.  We provide extra
 * bookkeeping space in the shadow itself, and this is the structure
 * definition for that bookkeeping information.
 */
struct pae_l3_bookkeeping {
    u32 vcpus;                  /* bitmap of which vcpus are currently storing
                                 * copies of this 32-byte page */
    u32 refcount;               /* refcount for this 32-byte page */
    u8 pinned;                  /* is this 32-byte page pinned or not? */
};

// Convert a shadow entry pointer into a pae_l3_bookkeeping pointer.
#define sl3p_to_info(_ptr) ((struct pae_l3_bookkeeping *)         \
                            (((unsigned long)(_ptr) & ~31) + 32))

static void sh2_destroy_l3_subshadow(struct vcpu *v, 
                                     shadow_l3e_t *sl3e);

/* Increment a subshadow ref
 * Called with a pointer to the subshadow, and the mfn of the
 * *first* page of the overall shadow. */
static inline void sh2_get_ref_l3_subshadow(shadow_l3e_t *sl3e, mfn_t smfn)
{
    struct pae_l3_bookkeeping *bk = sl3p_to_info(sl3e);

    /* First ref to the subshadow takes a ref to the full shadow */
    if ( bk->refcount == 0 ) 
        sh2_get_ref(smfn, 0);
    if ( unlikely(++(bk->refcount) == 0) )
    {
        SHADOW2_PRINTK("shadow l3 subshadow ref overflow, smfn=%" SH2_PRI_mfn " sh=%p\n", 
                       mfn_x(smfn), sl3e);
        domain_crash_synchronous();
    }
}

/* Decrement a subshadow ref.
 * Called with a pointer to the subshadow, and the mfn of the
 * *first* page of the overall shadow.  Calling this may cause the 
 * entire shadow to disappear, so the caller must immediately unmap 
 * the pointer after calling. */ 
static inline void sh2_put_ref_l3_subshadow(struct vcpu *v, 
                                            shadow_l3e_t *sl3e,
                                            mfn_t smfn)
{
    struct pae_l3_bookkeeping *bk;

    bk = sl3p_to_info(sl3e);

    ASSERT(bk->refcount > 0);
    if ( --(bk->refcount) == 0 )
    {
        /* Need to destroy this subshadow */
        sh2_destroy_l3_subshadow(v, sl3e);
        /* Last ref to the subshadow had a ref to the full shadow */
        sh2_put_ref(v, smfn, 0);
    }
}

/* Pin a subshadow 
 * Called with a pointer to the subshadow, and the mfn of the
 * *first* page of the overall shadow. */
static inline void sh2_pin_l3_subshadow(shadow_l3e_t *sl3e, mfn_t smfn)
{
    struct pae_l3_bookkeeping *bk = sl3p_to_info(sl3e);

#if 0
    debugtrace_printk("%s smfn=%05lx offset=%ld\n",
                      __func__, mfn_x(smfn),
                      ((unsigned long)sl3e & ~PAGE_MASK) / 64);
#endif

    if ( !bk->pinned )
    {
        bk->pinned = 1;
        sh2_get_ref_l3_subshadow(sl3e, smfn);
    }
}

/* Unpin a sub-shadow. 
 * Called with a pointer to the subshadow, and the mfn of the
 * *first* page of the overall shadow.  Calling this may cause the 
 * entire shadow to disappear, so the caller must immediately unmap 
 * the pointer after calling. */ 
static inline void sh2_unpin_l3_subshadow(struct vcpu *v, 
                                          shadow_l3e_t *sl3e,
                                          mfn_t smfn)
{
    struct pae_l3_bookkeeping *bk = sl3p_to_info(sl3e);

#if 0
    debugtrace_printk("%s smfn=%05lx offset=%ld\n",
                      __func__, mfn_x(smfn),
                      ((unsigned long)sl3e & ~PAGE_MASK) / 64);
#endif

    if ( bk->pinned )
    {
        bk->pinned = 0;
        sh2_put_ref_l3_subshadow(v, sl3e, smfn);
    }
}

#endif /* GUEST_PAGING_LEVELS == 3 */

#if SHADOW_PAGING_LEVELS == 3
#define MFN_FITS_IN_HVM_CR3(_MFN) !(mfn_x(_MFN) >> 20)
#endif

#if SHADOW_PAGING_LEVELS == 2
#define SH2_PRI_pte "08x"
#else /* SHADOW_PAGING_LEVELS >= 3 */
#ifndef __x86_64__
#define SH2_PRI_pte "016llx"
#else
#define SH2_PRI_pte "016lx"
#endif
#endif /* SHADOW_PAGING_LEVELS >= 3 */

#if GUEST_PAGING_LEVELS == 2
#define SH2_PRI_gpte "08x"
#else /* GUEST_PAGING_LEVELS >= 3 */
#ifndef __x86_64__
#define SH2_PRI_gpte "016llx"
#else
#define SH2_PRI_gpte "016lx"
#endif
#endif /* GUEST_PAGING_LEVELS >= 3 */

static inline u32
accumulate_guest_flags(walk_t *gw)
{
    u32 accumulated_flags;

    // We accumulate the permission flags with bitwise ANDing.
    // This works for the PRESENT bit, RW bit, and USER bit.
    // For the NX bit, however, the polarity is wrong, so we accumulate the
    // inverse of the NX bit.
    //
    accumulated_flags =  guest_l1e_get_flags(gw->eff_l1e) ^ _PAGE_NX_BIT;
    accumulated_flags &= guest_l2e_get_flags(*gw->l2e) ^ _PAGE_NX_BIT;

    // Note that PAE guests do not have USER or RW or NX bits in their L3s.
    //
#if GUEST_PAGING_LEVELS == 3
    accumulated_flags &=
        ~_PAGE_PRESENT | (guest_l3e_get_flags(*gw->l3e) & _PAGE_PRESENT);
#elif GUEST_PAGING_LEVELS >= 4
    accumulated_flags &= guest_l3e_get_flags(*gw->l3e) ^ _PAGE_NX_BIT;
    accumulated_flags &= guest_l4e_get_flags(*gw->l4e) ^ _PAGE_NX_BIT;
#endif

    // Finally, revert the NX bit back to its original polarity
    accumulated_flags ^= _PAGE_NX_BIT;

    return accumulated_flags;
}

#endif /* _XEN_SHADOW2_TYPES_H */

/*
 * Local variables:
 * mode: C
 * c-set-style: "BSD"
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
