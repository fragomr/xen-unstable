/*
 * Copyright (c) 2006, Intel Corporation.
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
 * Copyright (C) Ashok Raj <ashok.raj@intel.com>
 * Copyright (C) Shaohua Li <shaohua.li@intel.com>
 * Copyright (C) Allen Kay <allen.m.kay@intel.com> - adapted to xen
 */

#include <xen/irq.h>
#include <xen/sched.h>
#include <xen/xmalloc.h>
#include <xen/domain_page.h>
#include <xen/iommu.h>
#include <asm/hvm/iommu.h>
#include <xen/numa.h>
#include <xen/time.h>
#include <xen/pci.h>
#include <xen/pci_regs.h>
#include <xen/keyhandler.h>
#include <asm/msi.h>
#include "iommu.h"
#include "dmar.h"
#include "extern.h"
#include "vtd.h"

#define domain_iommu_domid(d) ((d)->arch.hvm_domain.hvm_iommu.iommu_domid)

int nr_iommus;
static spinlock_t domid_bitmap_lock;    /* protect domain id bitmap */
static int domid_bitmap_size;           /* domain id bitmap size in bits */
static unsigned long *domid_bitmap;     /* iommu domain id bitmap */
static bool_t rwbf_quirk;

static void setup_dom0_devices(struct domain *d);
static void setup_dom0_rmrr(struct domain *d);

#define DID_FIELD_WIDTH 16
#define DID_HIGH_OFFSET 8
static void context_set_domain_id(struct context_entry *context,
                                  struct domain *d)
{
    domid_t iommu_domid = domain_iommu_domid(d);

    if ( iommu_domid == 0 )
    {
        spin_lock(&domid_bitmap_lock);
        iommu_domid = find_first_zero_bit(domid_bitmap, domid_bitmap_size);
        set_bit(iommu_domid, domid_bitmap);
        spin_unlock(&domid_bitmap_lock);
        d->arch.hvm_domain.hvm_iommu.iommu_domid = iommu_domid;
    }

    context->hi &= (1 << DID_HIGH_OFFSET) - 1;
    context->hi |= iommu_domid << DID_HIGH_OFFSET;
}

static void iommu_domid_release(struct domain *d)
{
    domid_t iommu_domid = domain_iommu_domid(d);

    if ( iommu_domid != 0 )
    {
        d->arch.hvm_domain.hvm_iommu.iommu_domid = 0;
        clear_bit(iommu_domid, domid_bitmap);
    }
}

static struct intel_iommu *alloc_intel_iommu(void)
{
    struct intel_iommu *intel;

    intel = xmalloc(struct intel_iommu);
    if ( intel == NULL )
        return NULL;
    memset(intel, 0, sizeof(struct intel_iommu));

    spin_lock_init(&intel->qi_ctrl.qinval_lock);
    spin_lock_init(&intel->qi_ctrl.qinval_poll_lock);
    spin_lock_init(&intel->ir_ctrl.iremap_lock);

    return intel;
}

static void free_intel_iommu(struct intel_iommu *intel)
{
    xfree(intel);
}

struct qi_ctrl *iommu_qi_ctrl(struct iommu *iommu)
{
    return iommu ? &iommu->intel->qi_ctrl : NULL;
}

struct ir_ctrl *iommu_ir_ctrl(struct iommu *iommu)
{
    return iommu ? &iommu->intel->ir_ctrl : NULL;
}

struct iommu_flush *iommu_get_flush(struct iommu *iommu)
{
    return iommu ? &iommu->intel->flush : NULL;
}

static unsigned int clflush_size;
static int iommus_incoherent;
static void __iommu_flush_cache(void *addr, int size)
{
    int i;

    if ( !iommus_incoherent )
        return;

    for ( i = 0; i < size; i += clflush_size )
        cacheline_flush((char *)addr + i);
}

void iommu_flush_cache_entry(void *addr)
{
    __iommu_flush_cache(addr, 8);
}

void iommu_flush_cache_page(void *addr, unsigned long npages)
{
    __iommu_flush_cache(addr, PAGE_SIZE_4K * npages);
}

/* Allocate page table, return its machine address */
u64 alloc_pgtable_maddr(struct acpi_drhd_unit *drhd, unsigned long npages)
{
    struct acpi_rhsa_unit *rhsa;
    struct page_info *pg;
    u64 *vaddr;
    int node = -1;

    rhsa = drhd_to_rhsa(drhd);
    if ( rhsa )
        node =  pxm_to_node(rhsa->proximity_domain);

    pg = alloc_domheap_pages(NULL, get_order_from_pages(npages),
                             (node == -1 ) ? 0 : MEMF_node(node));
    if ( !pg )
        return 0;
    vaddr = map_domain_page(page_to_mfn(pg));
    if ( !vaddr )
        return 0;
    memset(vaddr, 0, PAGE_SIZE * npages);

    iommu_flush_cache_page(vaddr, npages);
    unmap_domain_page(vaddr);

    return page_to_maddr(pg);
}

/* context entry handling */
static u64 bus_to_context_maddr(struct iommu *iommu, u8 bus)
{
    struct acpi_drhd_unit *drhd;
    struct root_entry *root, *root_entries;
    u64 maddr;

    ASSERT(spin_is_locked(&iommu->lock));
    root_entries = (struct root_entry *)map_vtd_domain_page(iommu->root_maddr);
    root = &root_entries[bus];
    if ( !root_present(*root) )
    {
        drhd = iommu_to_drhd(iommu);
        maddr = alloc_pgtable_maddr(drhd, 1);
        if ( maddr == 0 )
        {
            unmap_vtd_domain_page(root_entries);
            return 0;
        }
        set_root_value(*root, maddr);
        set_root_present(*root);
        iommu_flush_cache_entry(root);
    }
    maddr = (u64) get_context_addr(*root);
    unmap_vtd_domain_page(root_entries);
    return maddr;
}

static u64 addr_to_dma_page_maddr(struct domain *domain, u64 addr, int alloc)
{
    struct acpi_drhd_unit *drhd;
    struct pci_dev *pdev;
    struct hvm_iommu *hd = domain_hvm_iommu(domain);
    int addr_width = agaw_to_width(hd->agaw);
    struct dma_pte *parent, *pte = NULL;
    int level = agaw_to_level(hd->agaw);
    int offset;
    u64 pte_maddr = 0, maddr;
    u64 *vaddr = NULL;

    addr &= (((u64)1) << addr_width) - 1;
    ASSERT(spin_is_locked(&hd->mapping_lock));
    if ( hd->pgd_maddr == 0 )
    {
        /*
         * just get any passthrough device in the domainr - assume user
         * assigns only devices from same node to a given guest.
         */
        pdev = pci_get_pdev_by_domain(domain, -1, -1);
        drhd = acpi_find_matched_drhd_unit(pdev);
        if ( !alloc || ((hd->pgd_maddr = alloc_pgtable_maddr(drhd, 1)) == 0) )
            goto out;
    }

    parent = (struct dma_pte *)map_vtd_domain_page(hd->pgd_maddr);
    while ( level > 1 )
    {
        offset = address_level_offset(addr, level);
        pte = &parent[offset];

        if ( dma_pte_addr(*pte) == 0 )
        {
            if ( !alloc )
                break;

            pdev = pci_get_pdev_by_domain(domain, -1, -1);
            drhd = acpi_find_matched_drhd_unit(pdev);
            maddr = alloc_pgtable_maddr(drhd, 1);
            if ( !maddr )
                break;

            dma_set_pte_addr(*pte, maddr);
            vaddr = map_vtd_domain_page(maddr);

            /*
             * high level table always sets r/w, last level
             * page table control read/write
             */
            dma_set_pte_readable(*pte);
            dma_set_pte_writable(*pte);
            iommu_flush_cache_entry(pte);
        }
        else
        {
            vaddr = map_vtd_domain_page(pte->val);
        }

        if ( level == 2 )
        {
            pte_maddr = pte->val & PAGE_MASK_4K;
            unmap_vtd_domain_page(vaddr);
            break;
        }

        unmap_vtd_domain_page(parent);
        parent = (struct dma_pte *)vaddr;
        vaddr = NULL;
        level--;
    }

    unmap_vtd_domain_page(parent);
 out:
    return pte_maddr;
}

static void iommu_flush_write_buffer(struct iommu *iommu)
{
    u32 val;
    unsigned long flags;

    if ( !rwbf_quirk && !cap_rwbf(iommu->cap) )
        return;

    spin_lock_irqsave(&iommu->register_lock, flags);
    val = dmar_readl(iommu->reg, DMAR_GSTS_REG);
    dmar_writel(iommu->reg, DMAR_GCMD_REG, val | DMA_GCMD_WBF);

    /* Make sure hardware complete it */
    IOMMU_WAIT_OP(iommu, DMAR_GSTS_REG, dmar_readl,
                  !(val & DMA_GSTS_WBFS), val);

    spin_unlock_irqrestore(&iommu->register_lock, flags);
}

/* return value determine if we need a write buffer flush */
static int flush_context_reg(
    void *_iommu,
    u16 did, u16 source_id, u8 function_mask, u64 type,
    int flush_non_present_entry)
{
    struct iommu *iommu = (struct iommu *) _iommu;
    u64 val = 0;
    unsigned long flags;

    /*
     * In the non-present entry flush case, if hardware doesn't cache
     * non-present entry we do nothing and if hardware cache non-present
     * entry, we flush entries of domain 0 (the domain id is used to cache
     * any non-present entries)
     */
    if ( flush_non_present_entry )
    {
        if ( !cap_caching_mode(iommu->cap) )
            return 1;
        else
            did = 0;
    }

    /* use register invalidation */
    switch ( type )
    {
    case DMA_CCMD_GLOBAL_INVL:
        val = DMA_CCMD_GLOBAL_INVL;
        break;
    case DMA_CCMD_DOMAIN_INVL:
        val = DMA_CCMD_DOMAIN_INVL|DMA_CCMD_DID(did);
        break;
    case DMA_CCMD_DEVICE_INVL:
        val = DMA_CCMD_DEVICE_INVL|DMA_CCMD_DID(did)
            |DMA_CCMD_SID(source_id)|DMA_CCMD_FM(function_mask);
        break;
    default:
        BUG();
    }
    val |= DMA_CCMD_ICC;

    spin_lock_irqsave(&iommu->register_lock, flags);
    dmar_writeq(iommu->reg, DMAR_CCMD_REG, val);

    /* Make sure hardware complete it */
    IOMMU_WAIT_OP(iommu, DMAR_CCMD_REG, dmar_readq,
                  !(val & DMA_CCMD_ICC), val);

    spin_unlock_irqrestore(&iommu->register_lock, flags);
    /* flush context entry will implicitly flush write buffer */
    return 0;
}

static int inline iommu_flush_context_global(
    struct iommu *iommu, int flush_non_present_entry)
{
    struct iommu_flush *flush = iommu_get_flush(iommu);
    return flush->context(iommu, 0, 0, 0, DMA_CCMD_GLOBAL_INVL,
                                 flush_non_present_entry);
}

static int inline iommu_flush_context_domain(
    struct iommu *iommu, u16 did, int flush_non_present_entry)
{
    struct iommu_flush *flush = iommu_get_flush(iommu);
    return flush->context(iommu, did, 0, 0, DMA_CCMD_DOMAIN_INVL,
                                 flush_non_present_entry);
}

static int inline iommu_flush_context_device(
    struct iommu *iommu, u16 did, u16 source_id,
    u8 function_mask, int flush_non_present_entry)
{
    struct iommu_flush *flush = iommu_get_flush(iommu);
    return flush->context(iommu, did, source_id, function_mask,
                                 DMA_CCMD_DEVICE_INVL,
                                 flush_non_present_entry);
}

/* return value determine if we need a write buffer flush */
static int flush_iotlb_reg(void *_iommu, u16 did,
                           u64 addr, unsigned int size_order, u64 type,
                           int flush_non_present_entry, int flush_dev_iotlb)
{
    struct iommu *iommu = (struct iommu *) _iommu;
    int tlb_offset = ecap_iotlb_offset(iommu->ecap);
    u64 val = 0, val_iva = 0;
    unsigned long flags;

    /*
     * In the non-present entry flush case, if hardware doesn't cache
     * non-present entry we do nothing and if hardware cache non-present
     * entry, we flush entries of domain 0 (the domain id is used to cache
     * any non-present entries)
     */
    if ( flush_non_present_entry )
    {
        if ( !cap_caching_mode(iommu->cap) )
            return 1;
        else
            did = 0;
    }

    /* use register invalidation */
    switch ( type )
    {
    case DMA_TLB_GLOBAL_FLUSH:
        /* global flush doesn't need set IVA_REG */
        val = DMA_TLB_GLOBAL_FLUSH|DMA_TLB_IVT;
        break;
    case DMA_TLB_DSI_FLUSH:
        val = DMA_TLB_DSI_FLUSH|DMA_TLB_IVT|DMA_TLB_DID(did);
        break;
    case DMA_TLB_PSI_FLUSH:
        val = DMA_TLB_PSI_FLUSH|DMA_TLB_IVT|DMA_TLB_DID(did);
        /* Note: always flush non-leaf currently */
        val_iva = size_order | addr;
        break;
    default:
        BUG();
    }
    /* Note: set drain read/write */
    if ( cap_read_drain(iommu->cap) )
        val |= DMA_TLB_READ_DRAIN;
    if ( cap_write_drain(iommu->cap) )
        val |= DMA_TLB_WRITE_DRAIN;

    spin_lock_irqsave(&iommu->register_lock, flags);
    /* Note: Only uses first TLB reg currently */
    if ( val_iva )
        dmar_writeq(iommu->reg, tlb_offset, val_iva);
    dmar_writeq(iommu->reg, tlb_offset + 8, val);

    /* Make sure hardware complete it */
    IOMMU_WAIT_OP(iommu, (tlb_offset + 8), dmar_readq,
                  !(val & DMA_TLB_IVT), val);
    spin_unlock_irqrestore(&iommu->register_lock, flags);

    /* check IOTLB invalidation granularity */
    if ( DMA_TLB_IAIG(val) == 0 )
        dprintk(XENLOG_ERR VTDPREFIX, "IOMMU: flush IOTLB failed\n");

    /* flush iotlb entry will implicitly flush write buffer */
    return 0;
}

static int inline iommu_flush_iotlb_global(struct iommu *iommu,
    int flush_non_present_entry, int flush_dev_iotlb)
{
    struct iommu_flush *flush = iommu_get_flush(iommu);
    return flush->iotlb(iommu, 0, 0, 0, DMA_TLB_GLOBAL_FLUSH,
                        flush_non_present_entry, flush_dev_iotlb);
}

static int inline iommu_flush_iotlb_dsi(struct iommu *iommu, u16 did,
    int flush_non_present_entry, int flush_dev_iotlb)
{
    struct iommu_flush *flush = iommu_get_flush(iommu);
    return flush->iotlb(iommu, did, 0, 0, DMA_TLB_DSI_FLUSH,
                        flush_non_present_entry, flush_dev_iotlb);
}

static int inline get_alignment(u64 base, unsigned int size)
{
    int t = 0;
    u64 end;

    end = base + size - 1;
    while ( base != end )
    {
        t++;
        base >>= 1;
        end >>= 1;
    }
    return t;
}

static int inline iommu_flush_iotlb_psi(
    struct iommu *iommu, u16 did, u64 addr, unsigned int pages,
    int flush_non_present_entry, int flush_dev_iotlb)
{
    unsigned int align;
    struct iommu_flush *flush = iommu_get_flush(iommu);

    ASSERT(!(addr & (~PAGE_MASK_4K)));
    ASSERT(pages > 0);

    /* Fallback to domain selective flush if no PSI support */
    if ( !cap_pgsel_inv(iommu->cap) )
        return iommu_flush_iotlb_dsi(iommu, did, flush_non_present_entry, flush_dev_iotlb);

    /*
     * PSI requires page size is 2 ^ x, and the base address is naturally
     * aligned to the size
     */
    align = get_alignment(addr >> PAGE_SHIFT_4K, pages);
    /* Fallback to domain selective flush if size is too big */
    if ( align > cap_max_amask_val(iommu->cap) )
        return iommu_flush_iotlb_dsi(iommu, did, flush_non_present_entry, flush_dev_iotlb);

    addr >>= PAGE_SHIFT_4K + align;
    addr <<= PAGE_SHIFT_4K + align;

    return flush->iotlb(iommu, did, addr, align, DMA_TLB_PSI_FLUSH,
                        flush_non_present_entry, flush_dev_iotlb);
}

void iommu_flush_all(void)
{
    struct acpi_drhd_unit *drhd;
    struct iommu *iommu;
    int flush_dev_iotlb;

    flush_all_cache();
    for_each_drhd_unit ( drhd )
    {
        iommu = drhd->iommu;
        iommu_flush_context_global(iommu, 0);
        flush_dev_iotlb = find_ats_dev_drhd(iommu) ? 1 : 0;
        iommu_flush_iotlb_global(iommu, 0, flush_dev_iotlb);
    }
}

/* clear one page's page table */
static void dma_pte_clear_one(struct domain *domain, u64 addr)
{
    struct hvm_iommu *hd = domain_hvm_iommu(domain);
    struct acpi_drhd_unit *drhd;
    struct iommu *iommu;
    struct dma_pte *page = NULL, *pte = NULL;
    u64 pg_maddr;
    int flush_dev_iotlb;

    spin_lock(&hd->mapping_lock);
    /* get last level pte */
    pg_maddr = addr_to_dma_page_maddr(domain, addr, 0);
    if ( pg_maddr == 0 )
    {
        spin_unlock(&hd->mapping_lock);
        return;
    }

    page = (struct dma_pte *)map_vtd_domain_page(pg_maddr);
    pte = page + address_level_offset(addr, 1);

    if ( !dma_pte_present(*pte) )
    {
        spin_unlock(&hd->mapping_lock);
        unmap_vtd_domain_page(page);
        return;
    }

    dma_clear_pte(*pte); 
    spin_unlock(&hd->mapping_lock);
    iommu_flush_cache_entry(pte);

    /* No need pcidevs_lock here since do that on assign/deassign device*/
    for_each_drhd_unit ( drhd )
    {
        iommu = drhd->iommu;
        if ( test_bit(iommu->index, &hd->iommu_bitmap) )
        {
            flush_dev_iotlb = find_ats_dev_drhd(iommu) ? 1 : 0;
            if ( iommu_flush_iotlb_psi(iommu, domain_iommu_domid(domain),
                                       addr, 1, 0, flush_dev_iotlb) )
                iommu_flush_write_buffer(iommu);
        }
    }

    unmap_vtd_domain_page(page);
}

static void iommu_free_pagetable(u64 pt_maddr, int level)
{
    int i;
    struct dma_pte *pt_vaddr, *pte;
    int next_level = level - 1;

    if ( pt_maddr == 0 )
        return;

    pt_vaddr = (struct dma_pte *)map_vtd_domain_page(pt_maddr);

    for ( i = 0; i < PTE_NUM; i++ )
    {
        pte = &pt_vaddr[i];
        if ( !dma_pte_present(*pte) )
            continue;

        if ( next_level >= 1 )
            iommu_free_pagetable(dma_pte_addr(*pte), next_level);

        dma_clear_pte(*pte);
        iommu_flush_cache_entry(pte);
    }

    unmap_vtd_domain_page(pt_vaddr);
    free_pgtable_maddr(pt_maddr);
}

static int iommu_set_root_entry(struct iommu *iommu)
{
    struct acpi_drhd_unit *drhd;
    u32 sts;
    unsigned long flags;

    spin_lock(&iommu->lock);

    if ( iommu->root_maddr == 0 )
    {
        drhd = iommu_to_drhd(iommu);
        iommu->root_maddr = alloc_pgtable_maddr(drhd, 1);
    }

    if ( iommu->root_maddr == 0 )
    {
        spin_unlock(&iommu->lock);
        return -ENOMEM;
    }

    spin_unlock(&iommu->lock);
    spin_lock_irqsave(&iommu->register_lock, flags);
    dmar_writeq(iommu->reg, DMAR_RTADDR_REG, iommu->root_maddr);

    sts = dmar_readl(iommu->reg, DMAR_GSTS_REG);
    dmar_writel(iommu->reg, DMAR_GCMD_REG, sts | DMA_GCMD_SRTP);

    /* Make sure hardware complete it */
    IOMMU_WAIT_OP(iommu, DMAR_GSTS_REG, dmar_readl,
                  (sts & DMA_GSTS_RTPS), sts);
    spin_unlock_irqrestore(&iommu->register_lock, flags);

    return 0;
}

static void iommu_enable_translation(struct iommu *iommu)
{
    u32 sts;
    unsigned long flags;

    dprintk(XENLOG_INFO VTDPREFIX,
            "iommu_enable_translation: iommu->reg = %p\n", iommu->reg);
    spin_lock_irqsave(&iommu->register_lock, flags);
    sts = dmar_readl(iommu->reg, DMAR_GSTS_REG);
    dmar_writel(iommu->reg, DMAR_GCMD_REG, sts | DMA_GCMD_TE);

    /* Make sure hardware complete it */
    IOMMU_WAIT_OP(iommu, DMAR_GSTS_REG, dmar_readl,
                  (sts & DMA_GSTS_TES), sts);
    spin_unlock_irqrestore(&iommu->register_lock, flags);

    /* Disable PMRs when VT-d engine takes effect per spec definition */
    disable_pmr(iommu);
}

static void iommu_disable_translation(struct iommu *iommu)
{
    u32 sts;
    unsigned long flags;

    spin_lock_irqsave(&iommu->register_lock, flags);
    sts = dmar_readl(iommu->reg, DMAR_GSTS_REG);
    dmar_writel(iommu->reg, DMAR_GCMD_REG, sts & (~DMA_GCMD_TE));

    /* Make sure hardware complete it */
    IOMMU_WAIT_OP(iommu, DMAR_GSTS_REG, dmar_readl,
                  !(sts & DMA_GSTS_TES), sts);
    spin_unlock_irqrestore(&iommu->register_lock, flags);
}

static struct iommu *vector_to_iommu[NR_VECTORS];
static int iommu_page_fault_do_one(struct iommu *iommu, int type,
                                   u8 fault_reason, u16 source_id, u64 addr)
{
    dprintk(XENLOG_WARNING VTDPREFIX,
            "iommu_fault:%s: %x:%x.%x addr %"PRIx64" REASON %x "
            "iommu->reg = %p\n",
            (type ? "DMA Read" : "DMA Write"), (source_id >> 8),
            PCI_SLOT(source_id & 0xFF), PCI_FUNC(source_id & 0xFF), addr,
            fault_reason, iommu->reg);

#ifndef __i386__ /* map_domain_page() cannot be used in this context */
    if ( fault_reason < 0x20 )
        print_vtd_entries(iommu, (source_id >> 8),
                          (source_id & 0xff), (addr >> PAGE_SHIFT));
#endif

    return 0;
}

static void iommu_fault_status(u32 fault_status)
{
    if ( fault_status & DMA_FSTS_PFO )
        dprintk(XENLOG_ERR VTDPREFIX,
            "iommu_fault_status: Fault Overflow\n");
    if ( fault_status & DMA_FSTS_PPF )
        dprintk(XENLOG_ERR VTDPREFIX,
            "iommu_fault_status: Primary Pending Fault\n");
    if ( fault_status & DMA_FSTS_AFO )
        dprintk(XENLOG_ERR VTDPREFIX,
            "iommu_fault_status: Advanced Fault Overflow\n");
    if ( fault_status & DMA_FSTS_APF )
        dprintk(XENLOG_ERR VTDPREFIX,
            "iommu_fault_status: Advanced Pending Fault\n");
    if ( fault_status & DMA_FSTS_IQE )
        dprintk(XENLOG_ERR VTDPREFIX,
            "iommu_fault_status: Invalidation Queue Error\n");
    if ( fault_status & DMA_FSTS_ICE )
        dprintk(XENLOG_ERR VTDPREFIX,
            "iommu_fault_status: Invalidation Completion Error\n");
    if ( fault_status & DMA_FSTS_ITE )
        dprintk(XENLOG_ERR VTDPREFIX,
            "iommu_fault_status: Invalidation Time-out Error\n");
}

#define PRIMARY_FAULT_REG_LEN (16)
static void iommu_page_fault(int vector, void *dev_id,
                             struct cpu_user_regs *regs)
{
    struct iommu *iommu = dev_id;
    int reg, fault_index;
    u32 fault_status;
    unsigned long flags;

    dprintk(XENLOG_WARNING VTDPREFIX,
            "iommu_page_fault: iommu->reg = %p\n", iommu->reg);

    fault_status = dmar_readl(iommu->reg, DMAR_FSTS_REG);

    iommu_fault_status(fault_status);

    /* FIXME: ignore advanced fault log */
    if ( !(fault_status & DMA_FSTS_PPF) )
        goto clear_overflow;

    fault_index = dma_fsts_fault_record_index(fault_status);
    reg = cap_fault_reg_offset(iommu->cap);
    while (1)
    {
        u8 fault_reason;
        u16 source_id;
        u32 data;
        u64 guest_addr;
        int type;

        /* highest 32 bits */
        spin_lock_irqsave(&iommu->register_lock, flags);
        data = dmar_readl(iommu->reg, reg +
                          fault_index * PRIMARY_FAULT_REG_LEN + 12);
        if ( !(data & DMA_FRCD_F) )
        {
            spin_unlock_irqrestore(&iommu->register_lock, flags);
            break;
        }

        fault_reason = dma_frcd_fault_reason(data);
        type = dma_frcd_type(data);

        data = dmar_readl(iommu->reg, reg +
                          fault_index * PRIMARY_FAULT_REG_LEN + 8);
        source_id = dma_frcd_source_id(data);

        guest_addr = dmar_readq(iommu->reg, reg +
                                fault_index * PRIMARY_FAULT_REG_LEN);
        guest_addr = dma_frcd_page_addr(guest_addr);
        /* clear the fault */
        dmar_writel(iommu->reg, reg +
                    fault_index * PRIMARY_FAULT_REG_LEN + 12, DMA_FRCD_F);
        spin_unlock_irqrestore(&iommu->register_lock, flags);

        iommu_page_fault_do_one(iommu, type, fault_reason,
                                source_id, guest_addr);

        fault_index++;
        if ( fault_index > cap_num_fault_regs(iommu->cap) )
            fault_index = 0;
    }
clear_overflow:
    /* clear primary fault overflow */
    fault_status = readl(iommu->reg + DMAR_FSTS_REG);
    if ( fault_status & DMA_FSTS_PFO )
    {
        spin_lock_irqsave(&iommu->register_lock, flags);
        dmar_writel(iommu->reg, DMAR_FSTS_REG, DMA_FSTS_PFO);
        spin_unlock_irqrestore(&iommu->register_lock, flags);
    }
}

static void dma_msi_unmask(unsigned int vector)
{
    struct iommu *iommu = vector_to_iommu[vector];
    unsigned long flags;

    /* unmask it */
    spin_lock_irqsave(&iommu->register_lock, flags);
    dmar_writel(iommu->reg, DMAR_FECTL_REG, 0);
    spin_unlock_irqrestore(&iommu->register_lock, flags);
}

static void dma_msi_mask(unsigned int vector)
{
    unsigned long flags;
    struct iommu *iommu = vector_to_iommu[vector];

    /* mask it */
    spin_lock_irqsave(&iommu->register_lock, flags);
    dmar_writel(iommu->reg, DMAR_FECTL_REG, DMA_FECTL_IM);
    spin_unlock_irqrestore(&iommu->register_lock, flags);
}

static unsigned int dma_msi_startup(unsigned int vector)
{
    dma_msi_unmask(vector);
    return 0;
}

static void dma_msi_end(unsigned int vector)
{
    dma_msi_unmask(vector);
    ack_APIC_irq();
}

static void dma_msi_data_init(struct iommu *iommu, int vector)
{
    u32 msi_data = 0;
    unsigned long flags;

    /* Fixed, edge, assert mode. Follow MSI setting */
    msi_data |= vector & 0xff;
    msi_data |= 1 << 14;

    spin_lock_irqsave(&iommu->register_lock, flags);
    dmar_writel(iommu->reg, DMAR_FEDATA_REG, msi_data);
    spin_unlock_irqrestore(&iommu->register_lock, flags);
}

static void dma_msi_addr_init(struct iommu *iommu, int phy_cpu)
{
    u64 msi_address;
    unsigned long flags;

    /* Physical, dedicated cpu. Follow MSI setting */
    msi_address = (MSI_ADDRESS_HEADER << (MSI_ADDRESS_HEADER_SHIFT + 8));
    msi_address |= MSI_PHYSICAL_MODE << 2;
    msi_address |= MSI_REDIRECTION_HINT_MODE << 3;
    msi_address |= phy_cpu << MSI_TARGET_CPU_SHIFT;

    spin_lock_irqsave(&iommu->register_lock, flags);
    dmar_writel(iommu->reg, DMAR_FEADDR_REG, (u32)msi_address);
    dmar_writel(iommu->reg, DMAR_FEUADDR_REG, (u32)(msi_address >> 32));
    spin_unlock_irqrestore(&iommu->register_lock, flags);
}

static void dma_msi_set_affinity(unsigned int vector, cpumask_t dest)
{
    struct iommu *iommu = vector_to_iommu[vector];
    dma_msi_addr_init(iommu, cpu_physical_id(first_cpu(dest)));
}

static struct hw_interrupt_type dma_msi_type = {
    .typename = "DMA_MSI",
    .startup = dma_msi_startup,
    .shutdown = dma_msi_mask,
    .enable = dma_msi_unmask,
    .disable = dma_msi_mask,
    .ack = dma_msi_mask,
    .end = dma_msi_end,
    .set_affinity = dma_msi_set_affinity,
};

static int iommu_set_interrupt(struct iommu *iommu)
{
    int vector, ret;

    vector = assign_irq_vector(AUTO_ASSIGN_IRQ);
    if ( vector <= 0 )
    {
        gdprintk(XENLOG_ERR VTDPREFIX, "IOMMU: no vectors\n");
        return -EINVAL;
    }

    irq_desc[vector].handler = &dma_msi_type;
    vector_to_iommu[vector] = iommu;
    ret = request_irq_vector(vector, iommu_page_fault, 0, "dmar", iommu);
    if ( ret )
    {
        irq_desc[vector].handler = &no_irq_type;
        vector_to_iommu[vector] = NULL;
        free_irq_vector(vector);
        gdprintk(XENLOG_ERR VTDPREFIX, "IOMMU: can't request irq\n");
        return ret;
    }

    /* Make sure that vector is never re-used. */
    vector_irq[vector] = NEVER_ASSIGN_IRQ;

    return vector;
}

static int iommu_alloc(struct acpi_drhd_unit *drhd)
{
    struct iommu *iommu;
    unsigned long sagaw;
    int agaw;

    if ( nr_iommus > MAX_IOMMUS )
    {
        gdprintk(XENLOG_ERR VTDPREFIX,
                 "IOMMU: nr_iommus %d > MAX_IOMMUS\n", nr_iommus);
        return -ENOMEM;
    }

    iommu = xmalloc(struct iommu);
    if ( iommu == NULL )
        return -ENOMEM;
    memset(iommu, 0, sizeof(struct iommu));

    iommu->vector = -1; /* No vector assigned yet. */

    iommu->intel = alloc_intel_iommu();
    if ( iommu->intel == NULL )
    {
        xfree(iommu);
        return -ENOMEM;
    }

    iommu->reg = map_to_nocache_virt(nr_iommus, drhd->address);
    iommu->index = nr_iommus++;

    iommu->cap = dmar_readq(iommu->reg, DMAR_CAP_REG);
    iommu->ecap = dmar_readq(iommu->reg, DMAR_ECAP_REG);

    gdprintk(XENLOG_INFO VTDPREFIX,
             "drhd->address = %"PRIx64"\n", drhd->address);
    gdprintk(XENLOG_INFO VTDPREFIX, "iommu->reg = %p\n", iommu->reg);

    /* Calculate number of pagetable levels: between 2 and 4. */
    sagaw = cap_sagaw(iommu->cap);
    for ( agaw = level_to_agaw(4); agaw >= 0; agaw-- )
        if ( test_bit(agaw, &sagaw) )
            break;
    if ( agaw < 0 )
    {
        gdprintk(XENLOG_ERR VTDPREFIX,
                 "IOMMU: unsupported sagaw %lx\n", sagaw);
        xfree(iommu);
        return -ENODEV;
    }
    iommu->nr_pt_levels = agaw_to_level(agaw);

    if ( !ecap_coherent(iommu->ecap) )
        iommus_incoherent = 1;

    spin_lock_init(&iommu->lock);
    spin_lock_init(&iommu->register_lock);

    drhd->iommu = iommu;
    return 0;
}

static void iommu_free(struct acpi_drhd_unit *drhd)
{
    struct iommu *iommu = drhd->iommu;

    if ( iommu == NULL )
        return;

    if ( iommu->root_maddr != 0 )
    {
        free_pgtable_maddr(iommu->root_maddr);
        iommu->root_maddr = 0;
    }

    if ( iommu->reg )
        iounmap(iommu->reg);

    free_intel_iommu(iommu->intel);
    release_irq_vector(iommu->vector);
    xfree(iommu);

    drhd->iommu = NULL;
}

#define guestwidth_to_adjustwidth(gaw) ({       \
    int agaw, r = (gaw - 12) % 9;               \
    agaw = (r == 0) ? gaw : (gaw + 9 - r);      \
    if ( agaw > 64 )                            \
        agaw = 64;                              \
    agaw; })

static int intel_iommu_domain_init(struct domain *d)
{
    struct hvm_iommu *hd = domain_hvm_iommu(d);
    struct iommu *iommu = NULL;
    struct acpi_drhd_unit *drhd;

    drhd = list_entry(acpi_drhd_units.next, typeof(*drhd), list);
    iommu = drhd->iommu;

    hd->agaw = width_to_agaw(DEFAULT_DOMAIN_ADDRESS_WIDTH);

    if ( d->domain_id == 0 )
    {
        /* Set up 1:1 page table for dom0 */
        iommu_set_dom0_mapping(d);

        setup_dom0_devices(d);
        setup_dom0_rmrr(d);

        iommu_flush_all();

        for_each_drhd_unit ( drhd )
        {
            iommu = drhd->iommu;
            iommu_enable_translation(iommu);
        }
    }

    return 0;
}

static int domain_context_mapping_one(
    struct domain *domain,
    struct iommu *iommu,
    u8 bus, u8 devfn)
{
    struct hvm_iommu *hd = domain_hvm_iommu(domain);
    struct context_entry *context, *context_entries;
    u64 maddr, pgd_maddr;
    struct pci_dev *pdev = NULL;
    int agaw;

    ASSERT(spin_is_locked(&pcidevs_lock));
    spin_lock(&iommu->lock);
    maddr = bus_to_context_maddr(iommu, bus);
    context_entries = (struct context_entry *)map_vtd_domain_page(maddr);
    context = &context_entries[devfn];

    if ( context_present(*context) )
    {
        int res = 0;

        pdev = pci_get_pdev(bus, devfn);
        if (!pdev)
            res = -ENODEV;
        else if (pdev->domain != domain)
            res = -EINVAL;
        unmap_vtd_domain_page(context_entries);
        spin_unlock(&iommu->lock);
        return res;
    }

    if ( iommu_passthrough && (domain->domain_id == 0) )
    {
        context_set_translation_type(*context, CONTEXT_TT_PASS_THRU);
        agaw = level_to_agaw(iommu->nr_pt_levels);
    }
    else
    {
        spin_lock(&hd->mapping_lock);

        /* Ensure we have pagetables allocated down to leaf PTE. */
        if ( hd->pgd_maddr == 0 )
        {
            addr_to_dma_page_maddr(domain, 0, 1);
            if ( hd->pgd_maddr == 0 )
            {
            nomem:
                spin_unlock(&hd->mapping_lock);
                spin_unlock(&iommu->lock);
                unmap_vtd_domain_page(context_entries);
                return -ENOMEM;
            }
        }

        /* Skip top levels of page tables for 2- and 3-level DRHDs. */
        pgd_maddr = hd->pgd_maddr;
        for ( agaw = level_to_agaw(4);
              agaw != level_to_agaw(iommu->nr_pt_levels);
              agaw-- )
        {
            struct dma_pte *p = map_vtd_domain_page(pgd_maddr);
            pgd_maddr = dma_pte_addr(*p);
            unmap_vtd_domain_page(p);
            if ( pgd_maddr == 0 )
                goto nomem;
        }

        context_set_address_root(*context, pgd_maddr);
        if ( ats_enabled && ecap_dev_iotlb(iommu->ecap) )
            context_set_translation_type(*context, CONTEXT_TT_DEV_IOTLB);
        else
            context_set_translation_type(*context, CONTEXT_TT_MULTI_LEVEL);

        spin_unlock(&hd->mapping_lock);
    }

    /*
     * domain_id 0 is not valid on Intel's IOMMU, force domain_id to
     * be 1 based as required by intel's iommu hw.
     */
    context_set_domain_id(context, domain);
    context_set_address_width(*context, agaw);
    context_set_fault_enable(*context);
    context_set_present(*context);
    iommu_flush_cache_entry(context);
    spin_unlock(&iommu->lock);

    /* Context entry was previously non-present (with domid 0). */
    if ( iommu_flush_context_device(iommu, 0, (((u16)bus) << 8) | devfn,
                                    DMA_CCMD_MASK_NOBIT, 1) )
        iommu_flush_write_buffer(iommu);
    else
    {
        int flush_dev_iotlb = find_ats_dev_drhd(iommu) ? 1 : 0;
        iommu_flush_iotlb_dsi(iommu, 0, 1, flush_dev_iotlb);
    }

    set_bit(iommu->index, &hd->iommu_bitmap);

    unmap_vtd_domain_page(context_entries);

    return 0;
}

static int domain_context_mapping(struct domain *domain, u8 bus, u8 devfn)
{
    struct acpi_drhd_unit *drhd;
    int ret = 0;
    u32 type;
    u8 secbus;
    struct pci_dev *pdev = pci_get_pdev(bus, devfn);

    if ( pdev == NULL )
    {
        /* We can reach here by setup_dom0_rmrr() -> iommu_prepare_rmrr_dev()
         * -> domain_context_mapping().
         * In the case a user enables VT-d and disables USB (that usually needs
         * RMRR) in BIOS, we can't discover the BDF of the USB controller in
         * setup_dom0_devices(), but the ACPI RMRR structures may still contain
         * the BDF and at last pci_get_pdev() returns NULL here.
         */
        gdprintk(XENLOG_WARNING VTDPREFIX,
                "domain_context_mapping: can't find bdf = %x:%x.%x\n",
                 bus, PCI_SLOT(devfn), PCI_FUNC(devfn));
        return 0;
    }

    drhd = acpi_find_matched_drhd_unit(pdev);
    if ( !drhd )
        return -ENODEV;

    ASSERT(spin_is_locked(&pcidevs_lock));

    type = pdev_type(bus, devfn);
    switch ( type )
    {
    case DEV_TYPE_PCIe_BRIDGE:
    case DEV_TYPE_PCIe2PCI_BRIDGE:
    case DEV_TYPE_LEGACY_PCI_BRIDGE:
        break;

    case DEV_TYPE_PCIe_ENDPOINT:
        gdprintk(XENLOG_INFO VTDPREFIX,
                 "domain_context_mapping:PCIe: bdf = %x:%x.%x\n",
                 bus, PCI_SLOT(devfn), PCI_FUNC(devfn));
        ret = domain_context_mapping_one(domain, drhd->iommu, bus, devfn);
        break;

    case DEV_TYPE_PCI:
        gdprintk(XENLOG_INFO VTDPREFIX,
                 "domain_context_mapping:PCI: bdf = %x:%x.%x\n",
                 bus, PCI_SLOT(devfn), PCI_FUNC(devfn));

        ret = domain_context_mapping_one(domain, drhd->iommu, bus, devfn);
        if ( ret )
            break;

        if ( find_upstream_bridge(&bus, &devfn, &secbus) < 1 )
            break;

        /* PCIe to PCI/PCIx bridge */
        if ( pdev_type(bus, devfn) == DEV_TYPE_PCIe2PCI_BRIDGE )
        {
            ret = domain_context_mapping_one(domain, drhd->iommu, bus, devfn);
            if ( ret )
                return ret;

            /*
             * Devices behind PCIe-to-PCI/PCIx bridge may generate
             * different requester-id. It may originate from devfn=0
             * on the secondary bus behind the bridge. Map that id
             * as well.
             */
            ret = domain_context_mapping_one(domain, drhd->iommu, secbus, 0);
        }
        else /* Legacy PCI bridge */
            ret = domain_context_mapping_one(domain, drhd->iommu, bus, devfn);

        break;

    default:
        gdprintk(XENLOG_ERR VTDPREFIX,
                 "domain_context_mapping:unknown type : bdf = %x:%x.%x\n",
                 bus, PCI_SLOT(devfn), PCI_FUNC(devfn));
        ret = -EINVAL;
        break;
    }

    return ret;
}

static int domain_context_unmap_one(
    struct domain *domain,
    struct iommu *iommu,
    u8 bus, u8 devfn)
{
    struct context_entry *context, *context_entries;
    u64 maddr;

    ASSERT(spin_is_locked(&pcidevs_lock));
    spin_lock(&iommu->lock);

    maddr = bus_to_context_maddr(iommu, bus);
    context_entries = (struct context_entry *)map_vtd_domain_page(maddr);
    context = &context_entries[devfn];

    if ( !context_present(*context) )
    {
        spin_unlock(&iommu->lock);
        unmap_vtd_domain_page(context_entries);
        return 0;
    }

    context_clear_present(*context);
    context_clear_entry(*context);
    iommu_flush_cache_entry(context);

    if ( iommu_flush_context_device(iommu, domain_iommu_domid(domain),
                                    (((u16)bus) << 8) | devfn,
                                    DMA_CCMD_MASK_NOBIT, 0) )
        iommu_flush_write_buffer(iommu);
    else
    {
        int flush_dev_iotlb = find_ats_dev_drhd(iommu) ? 1 : 0;
        iommu_flush_iotlb_dsi(iommu, domain_iommu_domid(domain), 0, flush_dev_iotlb);
    }

    spin_unlock(&iommu->lock);
    unmap_vtd_domain_page(context_entries);

    return 0;
}

static int domain_context_unmap(struct domain *domain, u8 bus, u8 devfn)
{
    struct acpi_drhd_unit *drhd;
    int ret = 0;
    u32 type;
    u8 secbus;
    struct pci_dev *pdev = pci_get_pdev(bus, devfn);

    BUG_ON(!pdev);

    drhd = acpi_find_matched_drhd_unit(pdev);
    if ( !drhd )
        return -ENODEV;

    type = pdev_type(bus, devfn);
    switch ( type )
    {
    case DEV_TYPE_PCIe_BRIDGE:
    case DEV_TYPE_PCIe2PCI_BRIDGE:
    case DEV_TYPE_LEGACY_PCI_BRIDGE:
        break;

    case DEV_TYPE_PCIe_ENDPOINT:
        gdprintk(XENLOG_INFO VTDPREFIX,
                 "domain_context_unmap:PCIe: bdf = %x:%x.%x\n",
                 bus, PCI_SLOT(devfn), PCI_FUNC(devfn));
        ret = domain_context_unmap_one(domain, drhd->iommu, bus, devfn);
        break;

    case DEV_TYPE_PCI:
        gdprintk(XENLOG_INFO VTDPREFIX,
                 "domain_context_unmap:PCI: bdf = %x:%x.%x\n",
                 bus, PCI_SLOT(devfn), PCI_FUNC(devfn));
        ret = domain_context_unmap_one(domain, drhd->iommu, bus, devfn);
        if ( ret )
            break;

        if ( find_upstream_bridge(&bus, &devfn, &secbus) < 1 )
            break;

        /* PCIe to PCI/PCIx bridge */
        if ( pdev_type(bus, devfn) == DEV_TYPE_PCIe2PCI_BRIDGE )
        {
            ret = domain_context_unmap_one(domain, drhd->iommu, bus, devfn);
            if ( ret )
                return ret;

            ret = domain_context_unmap_one(domain, drhd->iommu, secbus, 0);
        }
        else /* Legacy PCI bridge */
            ret = domain_context_unmap_one(domain, drhd->iommu, bus, devfn);

        break;

    default:
        gdprintk(XENLOG_ERR VTDPREFIX,
                 "domain_context_unmap:unknown type: bdf = %x:%x:%x\n",
                 bus, PCI_SLOT(devfn), PCI_FUNC(devfn));
        ret = -EINVAL;
        break;
    }

    return ret;
}

static int reassign_device_ownership(
    struct domain *source,
    struct domain *target,
    u8 bus, u8 devfn)
{
    struct hvm_iommu *source_hd = domain_hvm_iommu(source);
    struct pci_dev *pdev;
    struct acpi_drhd_unit *drhd;
    struct iommu *pdev_iommu;
    int ret, found = 0;

    ASSERT(spin_is_locked(&pcidevs_lock));
    pdev = pci_get_pdev_by_domain(source, bus, devfn);

    if (!pdev)
        return -ENODEV;

    drhd = acpi_find_matched_drhd_unit(pdev);
    pdev_iommu = drhd->iommu;
    domain_context_unmap(source, bus, devfn);

    ret = domain_context_mapping(target, bus, devfn);
    if ( ret )
        return ret;

    list_move(&pdev->domain_list, &target->arch.pdev_list);
    pdev->domain = target;

    for_each_pdev ( source, pdev )
    {
        drhd = acpi_find_matched_drhd_unit(pdev);
        if ( drhd->iommu == pdev_iommu )
        {
            found = 1;
            break;
        }
    }

    if ( !found )
        clear_bit(pdev_iommu->index, &source_hd->iommu_bitmap);

    return ret;
}

void iommu_domain_teardown(struct domain *d)
{
    struct hvm_iommu *hd = domain_hvm_iommu(d);

    if ( list_empty(&acpi_drhd_units) )
        return;

    spin_lock(&hd->mapping_lock);
    iommu_free_pagetable(hd->pgd_maddr, agaw_to_level(hd->agaw));
    hd->pgd_maddr = 0;
    spin_unlock(&hd->mapping_lock);

    iommu_domid_release(d);
}

int intel_iommu_map_page(
    struct domain *d, unsigned long gfn, unsigned long mfn)
{
    struct hvm_iommu *hd = domain_hvm_iommu(d);
    struct acpi_drhd_unit *drhd;
    struct iommu *iommu;
    struct dma_pte *page = NULL, *pte = NULL;
    u64 pg_maddr;
    int pte_present;
    int flush_dev_iotlb;

    drhd = list_entry(acpi_drhd_units.next, typeof(*drhd), list);
    iommu = drhd->iommu;

    /* do nothing if dom0 and iommu supports pass thru */
    if ( iommu_passthrough && (d->domain_id == 0) )
        return 0;

    spin_lock(&hd->mapping_lock);

    pg_maddr = addr_to_dma_page_maddr(d, (paddr_t)gfn << PAGE_SHIFT_4K, 1);
    if ( pg_maddr == 0 )
    {
        spin_unlock(&hd->mapping_lock);
        return -ENOMEM;
    }
    page = (struct dma_pte *)map_vtd_domain_page(pg_maddr);
    pte = page + (gfn & LEVEL_MASK);
    pte_present = dma_pte_present(*pte);
    dma_set_pte_addr(*pte, (paddr_t)mfn << PAGE_SHIFT_4K);
    dma_set_pte_prot(*pte, DMA_PTE_READ | DMA_PTE_WRITE);

    /* Set the SNP on leaf page table if Snoop Control available */
    if ( iommu_snoop )
        dma_set_pte_snp(*pte);

    iommu_flush_cache_entry(pte);
    spin_unlock(&hd->mapping_lock);
    unmap_vtd_domain_page(page);

    /*
     * No need pcideves_lock here because we have flush
     * when assign/deassign device
     */
    for_each_drhd_unit ( drhd )
    {
        iommu = drhd->iommu;

        if ( !test_bit(iommu->index, &hd->iommu_bitmap) )
            continue;

        flush_dev_iotlb = find_ats_dev_drhd(iommu) ? 1 : 0;
        if ( iommu_flush_iotlb_psi(iommu, domain_iommu_domid(d),
                                   (paddr_t)gfn << PAGE_SHIFT_4K, 1,
                                   !pte_present, flush_dev_iotlb) )
            iommu_flush_write_buffer(iommu);
    }

    return 0;
}

int intel_iommu_unmap_page(struct domain *d, unsigned long gfn)
{
    struct acpi_drhd_unit *drhd;
    struct iommu *iommu;

    drhd = list_entry(acpi_drhd_units.next, typeof(*drhd), list);
    iommu = drhd->iommu;

    /* do nothing if dom0 and iommu supports pass thru */
    if ( iommu_passthrough && (d->domain_id == 0) )
        return 0;

    dma_pte_clear_one(d, (paddr_t)gfn << PAGE_SHIFT_4K);

    return 0;
}

static int iommu_prepare_rmrr_dev(struct domain *d,
                                  struct acpi_rmrr_unit *rmrr,
                                  u8 bus, u8 devfn)
{
    int ret = 0;
    u64 base, end;
    unsigned long base_pfn, end_pfn;

    ASSERT(spin_is_locked(&pcidevs_lock));
    ASSERT(rmrr->base_address < rmrr->end_address);
    
    base = rmrr->base_address & PAGE_MASK_4K;
    base_pfn = base >> PAGE_SHIFT_4K;
    end = PAGE_ALIGN_4K(rmrr->end_address);
    end_pfn = end >> PAGE_SHIFT_4K;

    while ( base_pfn < end_pfn )
    {
        intel_iommu_map_page(d, base_pfn, base_pfn);
        base_pfn++;
    }

    ret = domain_context_mapping(d, bus, devfn);

    return ret;
}

static int intel_iommu_add_device(struct pci_dev *pdev)
{
    struct acpi_rmrr_unit *rmrr;
    u16 bdf;
    int ret, i;

    ASSERT(spin_is_locked(&pcidevs_lock));

    if ( !pdev->domain )
        return -EINVAL;

    ret = domain_context_mapping(pdev->domain, pdev->bus, pdev->devfn);
    if ( ret )
    {
        gdprintk(XENLOG_ERR VTDPREFIX,
                 "intel_iommu_add_device: context mapping failed\n");
        return ret;
    }

    for_each_rmrr_device ( rmrr, bdf, i )
    {
        if ( PCI_BUS(bdf) == pdev->bus && PCI_DEVFN2(bdf) == pdev->devfn )
        {
            ret = iommu_prepare_rmrr_dev(pdev->domain, rmrr,
                                         pdev->bus, pdev->devfn);
            if ( ret )
                gdprintk(XENLOG_ERR VTDPREFIX,
                         "intel_iommu_add_device: RMRR mapping failed\n");
            break;
        }
    }

    return ret;
}

static int intel_iommu_remove_device(struct pci_dev *pdev)
{
    struct acpi_rmrr_unit *rmrr;
    u16 bdf;
    int i;

    if ( !pdev->domain )
        return -EINVAL;

    /* If the device belongs to dom0, and it has RMRR, don't remove it
     * from dom0, because BIOS may use RMRR at booting time.
     */
    if ( pdev->domain->domain_id == 0 )
    {
        for_each_rmrr_device ( rmrr, bdf, i )
        {
            if ( PCI_BUS(bdf) == pdev->bus &&
                 PCI_DEVFN2(bdf) == pdev->devfn )
                return 0;
        }
    }

    return domain_context_unmap(pdev->domain, pdev->bus, pdev->devfn);
}

static void setup_dom0_devices(struct domain *d)
{
    struct hvm_iommu *hd;
    struct pci_dev *pdev;
    int bus, devfn;

    hd = domain_hvm_iommu(d);

    spin_lock(&pcidevs_lock);
    for ( bus = 0; bus < 256; bus++ )
    {
        for ( devfn = 0; devfn < 256; devfn++ )
        {
            pdev = pci_get_pdev(bus, devfn);
            if ( !pdev )
                continue;

            pdev->domain = d;
            list_add(&pdev->domain_list, &d->arch.pdev_list);
            domain_context_mapping(d, pdev->bus, pdev->devfn);
            if ( ats_device(0, pdev->bus, pdev->devfn) )
                enable_ats_device(0, pdev->bus, pdev->devfn);
        }
    }
    spin_unlock(&pcidevs_lock);
}

void clear_fault_bits(struct iommu *iommu)
{
    u64 val;
    unsigned long flags;

    spin_lock_irqsave(&iommu->register_lock, flags);
    val = dmar_readq(
        iommu->reg,
        cap_fault_reg_offset(dmar_readq(iommu->reg,DMAR_CAP_REG))+0x8);
    dmar_writeq(
        iommu->reg,
        cap_fault_reg_offset(dmar_readq(iommu->reg,DMAR_CAP_REG))+8,
        val);
    dmar_writel(iommu->reg, DMAR_FSTS_REG, DMA_FSTS_FAULTS);
    spin_unlock_irqrestore(&iommu->register_lock, flags);
}

static int init_vtd_hw(void)
{
    struct acpi_drhd_unit *drhd;
    struct iommu *iommu;
    struct iommu_flush *flush = NULL;
    int vector;
    int ret;
    unsigned long flags;

    for_each_drhd_unit ( drhd )
    {
        iommu = drhd->iommu;
        if ( iommu->vector < 0 )
        {
            vector = iommu_set_interrupt(iommu);
            if ( vector < 0 )
            {
                gdprintk(XENLOG_ERR VTDPREFIX, "IOMMU: interrupt setup failed\n");
                return vector;
            }
            iommu->vector = vector;
        }
        dma_msi_data_init(iommu, iommu->vector);
        dma_msi_addr_init(iommu, cpu_physical_id(first_cpu(cpu_online_map)));
        clear_fault_bits(iommu);

        spin_lock_irqsave(&iommu->register_lock, flags);
        dmar_writel(iommu->reg, DMAR_FECTL_REG, 0);
        spin_unlock_irqrestore(&iommu->register_lock, flags);

        /* initialize flush functions */
        flush = iommu_get_flush(iommu);
        flush->context = flush_context_reg;
        flush->iotlb = flush_iotlb_reg;
    }

    if ( iommu_qinval )
    {
        for_each_drhd_unit ( drhd )
        {
            iommu = drhd->iommu;
            if ( enable_qinval(iommu) != 0 )
            {
                dprintk(XENLOG_INFO VTDPREFIX,
                        "Failed to enable Queued Invalidation!\n");
                break;
            }
        }
    }

    if ( iommu_intremap )
    {
        for_each_drhd_unit ( drhd )
        {
            iommu = drhd->iommu;
            if ( enable_intremap(iommu) != 0 )
            {
                dprintk(XENLOG_INFO VTDPREFIX,
                        "Failed to enable Interrupt Remapping!\n");
                break;
            }
        }
    }

    for_each_drhd_unit ( drhd )
    {
        iommu = drhd->iommu;
        ret = iommu_set_root_entry(iommu);
        if ( ret )
        {
            gdprintk(XENLOG_ERR VTDPREFIX, "IOMMU: set root entry failed\n");
            return -EIO;
        }
    }

    /*
     * After set root entry, must globally invalidate context cache, and
     * then globally invalidate IOTLB
     */
    iommu_flush_all();

    return 0;
}

static void setup_dom0_rmrr(struct domain *d)
{
    struct acpi_rmrr_unit *rmrr;
    u16 bdf;
    int ret, i;

    spin_lock(&pcidevs_lock);
    for_each_rmrr_device ( rmrr, bdf, i )
    {
        ret = iommu_prepare_rmrr_dev(d, rmrr, PCI_BUS(bdf), PCI_DEVFN2(bdf));
        if ( ret )
            gdprintk(XENLOG_ERR VTDPREFIX,
                     "IOMMU: mapping reserved region failed\n");
    }
    spin_unlock(&pcidevs_lock);
}

static void platform_quirks(void)
{
    u32 id;

    /* Mobile 4 Series Chipset neglects to set RWBF capability. */
    id = pci_conf_read32(0, 0, 0, 0);
    if ( id == 0x2a408086 )
    {
        dprintk(XENLOG_INFO VTDPREFIX, "DMAR: Forcing write-buffer flush\n");
        rwbf_quirk = 1;
    }
}

int intel_vtd_setup(void)
{
    struct acpi_drhd_unit *drhd;
    struct iommu *iommu;

    if ( !iommu_enabled )
        return -ENODEV;

    platform_quirks();

    spin_lock_init(&domid_bitmap_lock);
    clflush_size = get_cache_line_size();

    /* We enable the following features only if they are supported by all VT-d
     * engines: Snoop Control, DMA passthrough, Queued Invalidation and
     * Interrupt Remapping.
     */
    for_each_drhd_unit ( drhd )
    {
        if ( iommu_alloc(drhd) != 0 )
            goto error;

        iommu = drhd->iommu;

        if ( iommu_snoop && !ecap_snp_ctl(iommu->ecap) )
            iommu_snoop = 0;

        if ( iommu_passthrough && !ecap_pass_thru(iommu->ecap) )
            iommu_passthrough = 0;

        if ( iommu_qinval && !ecap_queued_inval(iommu->ecap) )
            iommu_qinval = 0;

        if ( iommu_intremap && !ecap_intr_remap(iommu->ecap) )
            iommu_intremap = 0;
    }

    if ( !iommu_qinval && iommu_intremap )
    {
        iommu_intremap = 0;
        gdprintk(XENLOG_WARNING VTDPREFIX, "Interrupt Remapping disabled "
            "since Queued Invalidation isn't supported or enabled.\n");
    }

#define P(p,s) printk("Intel VT-d %s %ssupported.\n", s, (p)? "" : "not ")
    P(iommu_snoop, "Snoop Control");
    P(iommu_passthrough, "DMA Passthrough");
    P(iommu_qinval, "Queued Invalidation");
    P(iommu_intremap, "Interrupt Remapping");
#undef P

    /* Allocate IO page directory page for the domain. */
    drhd = list_entry(acpi_drhd_units.next, typeof(*drhd), list);
    iommu = drhd->iommu;

    /* Allocate domain id bitmap, and set bit 0 as reserved */
    domid_bitmap_size = cap_ndoms(iommu->cap);
    domid_bitmap = xmalloc_array(unsigned long,
                                 BITS_TO_LONGS(domid_bitmap_size));
    if ( domid_bitmap == NULL )
        goto error;
    memset(domid_bitmap, 0, domid_bitmap_size / 8);
    set_bit(0, domid_bitmap);

    scan_pci_devices();

    if ( init_vtd_hw() )
        goto error;

    register_keyhandler('V', dump_iommu_info, "dump iommu info");

    return 0;

 error:
    for_each_drhd_unit ( drhd )
        iommu_free(drhd);
    iommu_enabled = 0;
    iommu_snoop = 0;
    iommu_passthrough = 0;
    iommu_qinval = 0;
    iommu_intremap = 0;
    return -ENOMEM;
}

/*
 * If the device isn't owned by dom0, it means it already
 * has been assigned to other domain, or it's not exist.
 */
int device_assigned(u8 bus, u8 devfn)
{
    struct pci_dev *pdev;

    spin_lock(&pcidevs_lock);
    pdev = pci_get_pdev_by_domain(dom0, bus, devfn);
    if (!pdev)
    {
        spin_unlock(&pcidevs_lock);
        return -1;
    }

    spin_unlock(&pcidevs_lock);
    return 0;
}

int intel_iommu_assign_device(struct domain *d, u8 bus, u8 devfn)
{
    struct acpi_rmrr_unit *rmrr;
    int ret = 0, i;
    struct pci_dev *pdev;
    u16 bdf;

    if ( list_empty(&acpi_drhd_units) )
        return -ENODEV;

    ASSERT(spin_is_locked(&pcidevs_lock));
    pdev = pci_get_pdev(bus, devfn);
    if (!pdev)
        return -ENODEV;

    if (pdev->domain != dom0)
    {
        gdprintk(XENLOG_ERR VTDPREFIX,
                "IOMMU: assign a assigned device\n");
       return -EBUSY;
    }

    ret = reassign_device_ownership(dom0, d, bus, devfn);
    if ( ret )
        goto done;

    /* Setup rmrr identity mapping */
    for_each_rmrr_device( rmrr, bdf, i )
    {
        if ( PCI_BUS(bdf) == bus && PCI_DEVFN2(bdf) == devfn )
        {
            /* FIXME: Because USB RMRR conflicts with guest bios region,
             * ignore USB RMRR temporarily.
             */
            if ( is_usb_device(bus, devfn) )
            {
                ret = 0;
                goto done;
            }

            ret = iommu_prepare_rmrr_dev(d, rmrr, bus, devfn);
            if ( ret )
                gdprintk(XENLOG_ERR VTDPREFIX,
                         "IOMMU: mapping reserved region failed\n");
            goto done; 
        }
    }

done:
    return ret;
}

static int intel_iommu_group_id(u8 bus, u8 devfn)
{
    u8 secbus;
    if ( find_upstream_bridge(&bus, &devfn, &secbus) < 0 )
        return -1;
    else
        return PCI_BDF2(bus, devfn);
}

static u32 iommu_state[MAX_IOMMUS][MAX_IOMMU_REGS];
void vtd_suspend(void)
{
    struct acpi_drhd_unit *drhd;
    struct iommu *iommu;
    u32    i;

    if ( !iommu_enabled )
        return;

    iommu_flush_all();

    for_each_drhd_unit ( drhd )
    {
        iommu = drhd->iommu;
        i = iommu->index;

        iommu_state[i][DMAR_FECTL_REG] =
            (u32) dmar_readl(iommu->reg, DMAR_FECTL_REG);
        iommu_state[i][DMAR_FEDATA_REG] =
            (u32) dmar_readl(iommu->reg, DMAR_FEDATA_REG);
        iommu_state[i][DMAR_FEADDR_REG] =
            (u32) dmar_readl(iommu->reg, DMAR_FEADDR_REG);
        iommu_state[i][DMAR_FEUADDR_REG] =
            (u32) dmar_readl(iommu->reg, DMAR_FEUADDR_REG);

        /* don't disable VT-d engine when force_iommu is set. */
        if ( force_iommu )
            continue;

        iommu_disable_translation(iommu);

        if ( iommu_intremap )
            disable_intremap(iommu);

        if ( iommu_qinval )
            disable_qinval(iommu);
    }
}

void vtd_resume(void)
{
    struct acpi_drhd_unit *drhd;
    struct iommu *iommu;
    u32 i;
    unsigned long flags;

    if ( !iommu_enabled )
        return;

    if ( init_vtd_hw() != 0  && force_iommu )
         panic("IOMMU setup failed, crash Xen for security purpose!\n");

    for_each_drhd_unit ( drhd )
    {
        iommu = drhd->iommu;
        i = iommu->index;

        spin_lock_irqsave(&iommu->register_lock, flags);
        dmar_writel(iommu->reg, DMAR_FECTL_REG,
                    (u32) iommu_state[i][DMAR_FECTL_REG]);
        dmar_writel(iommu->reg, DMAR_FEDATA_REG,
                    (u32) iommu_state[i][DMAR_FEDATA_REG]);
        dmar_writel(iommu->reg, DMAR_FEADDR_REG,
                    (u32) iommu_state[i][DMAR_FEADDR_REG]);
        dmar_writel(iommu->reg, DMAR_FEUADDR_REG,
                    (u32) iommu_state[i][DMAR_FEUADDR_REG]);
        spin_unlock_irqrestore(&iommu->register_lock, flags);

        iommu_enable_translation(iommu);
    }
}

struct iommu_ops intel_iommu_ops = {
    .init = intel_iommu_domain_init,
    .add_device = intel_iommu_add_device,
    .remove_device = intel_iommu_remove_device,
    .assign_device  = intel_iommu_assign_device,
    .teardown = iommu_domain_teardown,
    .map_page = intel_iommu_map_page,
    .unmap_page = intel_iommu_unmap_page,
    .reassign_device = reassign_device_ownership,
    .get_device_group_id = intel_iommu_group_id,
    .update_ire_from_apic = io_apic_write_remap_rte,
    .update_ire_from_msi = msi_msg_write_remap_rte,
    .read_apic_from_ire = io_apic_read_remap_rte,
    .read_msi_from_ire = msi_msg_read_remap_rte,
    .suspend = vtd_suspend,
    .resume = vtd_resume,
};

/*
 * Local variables:
 * mode: C
 * c-set-style: "BSD"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
