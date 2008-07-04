/******************************************************************************
 * pci.h
 * 
 * PCI access functions.
 */

#ifndef __XEN_PCI_H__
#define __XEN_PCI_H__

#include <xen/config.h>
#include <xen/types.h>
#include <xen/list.h>

/*
 * The PCI interface treats multi-function devices as independent
 * devices.  The slot/function address of each device is encoded
 * in a single byte as follows:
 *
 * 15:8 = bus
 *  7:3 = slot
 *  2:0 = function
 */
#define PCI_BUS(bdf)    (((bdf) >> 8) & 0xff)
#define PCI_SLOT(bdf)   (((bdf) >> 3) & 0x1f)
#define PCI_FUNC(bdf)   ((bdf) & 0x07)
#define PCI_DEVFN(d,f)  (((d & 0x1f) << 3) | (f & 0x07))
#define PCI_DEVFN2(bdf) ((bdf) & 0xff)
#define PCI_BDF(b,d,f)  (((b * 0xff) << 8) | PCI_DEVFN(d,f))
#define PCI_BDF2(b,df)  (((b & 0xff) << 8) | (df & 0xff))

struct pci_dev {
    struct list_head domain_list;
    struct list_head msi_dev_list;
    u8 bus;
    u8 devfn;
    struct list_head msi_list;
};

#define for_each_pdev(domain, pdev) \
    list_for_each_entry(pdev, &(domain->arch.pdev_list), domain_list)


uint8_t pci_conf_read8(
    unsigned int bus, unsigned int dev, unsigned int func, unsigned int reg);
uint16_t pci_conf_read16(
    unsigned int bus, unsigned int dev, unsigned int func, unsigned int reg);
uint32_t pci_conf_read32(
    unsigned int bus, unsigned int dev, unsigned int func, unsigned int reg);
void pci_conf_write8(
    unsigned int bus, unsigned int dev, unsigned int func, unsigned int reg,
    uint8_t data);
void pci_conf_write16(
    unsigned int bus, unsigned int dev, unsigned int func, unsigned int reg,
    uint16_t data);
void pci_conf_write32(
    unsigned int bus, unsigned int dev, unsigned int func, unsigned int reg,
    uint32_t data);
int pci_find_cap_offset(u8 bus, u8 dev, u8 func, u8 cap);
int pci_find_next_cap(u8 bus, unsigned int devfn, u8 pos, int cap);

#endif /* __XEN_PCI_H__ */
