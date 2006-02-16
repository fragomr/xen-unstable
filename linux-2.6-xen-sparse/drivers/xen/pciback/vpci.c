/*
 * PCI Backend - Provides a Virtual PCI bus (with real devices)
 *               to the frontend
 *
 *   Author: Ryan Wilson <hap9@epoch.ncsc.mil>
 */

#include <linux/list.h>
#include <linux/slab.h>
#include <linux/pci.h>
#include "pciback.h"

#define PCI_SLOT_MAX 32

struct vpci_dev_data {
	struct list_head dev_list[PCI_SLOT_MAX];
};

static inline struct list_head *list_first(struct list_head *head)
{
	return head->next;
}

struct pci_dev *pciback_get_pci_dev(struct pciback_device *pdev,
				    unsigned int domain, unsigned int bus,
				    unsigned int devfn)
{
	struct pci_dev_entry *dev_entry;
	struct vpci_dev_data *vpci_dev = pdev->pci_dev_data;

	if (domain != 0 || bus != 0)
		return NULL;

	if (PCI_SLOT(devfn) < PCI_SLOT_MAX) {
		/* we don't need to lock the list here because once the backend
		 * is in operation, it won't have any more devices addeded
		 * (or removed).
		 */
		list_for_each_entry(dev_entry,
				    &vpci_dev->dev_list[PCI_SLOT(devfn)],
				    list) {
			if (PCI_FUNC(dev_entry->dev->devfn) == PCI_FUNC(devfn))
				return dev_entry->dev;
		}
	}
	return NULL;
}

static inline int match_slot(struct pci_dev *l, struct pci_dev *r)
{
	if (pci_domain_nr(l->bus) == pci_domain_nr(r->bus)
	    && l->bus == r->bus && PCI_SLOT(l->devfn) == PCI_SLOT(r->devfn))
		return 1;

	return 0;
}

/* Must hold pciback_device->dev_lock when calling this */
int pciback_add_pci_dev(struct pciback_device *pdev, struct pci_dev *dev)
{
	int err = 0, slot;
	struct pci_dev_entry *t, *dev_entry;
	struct vpci_dev_data *vpci_dev = pdev->pci_dev_data;

	if ((dev->class >> 24) == PCI_BASE_CLASS_BRIDGE) {
		err = -EFAULT;
		xenbus_dev_fatal(pdev->xdev, err,
				 "Can't export bridges on the virtual PCI bus");
		goto out;
	}

	dev_entry = kmalloc(sizeof(*dev_entry), GFP_KERNEL);
	if (!dev_entry) {
		err = -ENOMEM;
		xenbus_dev_fatal(pdev->xdev, err,
				 "Error adding entry to virtual PCI bus");
		goto out;
	}

	dev_entry->dev = dev;

	/* Keep multi-function devices together on the virtual PCI bus */
	for (slot = 0; slot < PCI_SLOT_MAX; slot++) {
		if (!list_empty(&vpci_dev->dev_list[slot])) {
			t = list_entry(list_first(&vpci_dev->dev_list[slot]),
				       struct pci_dev_entry, list);

			if (match_slot(dev, t->dev)) {
				pr_info("pciback: vpci: %s: "
					"assign to virtual slot %d func %d\n",
					pci_name(dev), slot,
					PCI_FUNC(dev->devfn));
				list_add_tail(&dev_entry->list,
					      &vpci_dev->dev_list[slot]);
				goto out;
			}
		}
	}

	/* Assign to a new slot on the virtual PCI bus */
	for (slot = 0; slot < PCI_SLOT_MAX; slot++) {
		if (list_empty(&vpci_dev->dev_list[slot])) {
			printk(KERN_INFO
			       "pciback: vpci: %s: assign to virtual slot %d\n",
			       pci_name(dev), slot);
			list_add_tail(&dev_entry->list,
				      &vpci_dev->dev_list[slot]);
			goto out;
		}
	}

	err = -ENOMEM;
	xenbus_dev_fatal(pdev->xdev, err,
			 "No more space on root virtual PCI bus");

      out:
	return err;
}

int pciback_init_devices(struct pciback_device *pdev)
{
	int slot;
	struct vpci_dev_data *vpci_dev;

	vpci_dev = kmalloc(sizeof(*vpci_dev), GFP_KERNEL);
	if (!vpci_dev)
		return -ENOMEM;

	for (slot = 0; slot < PCI_SLOT_MAX; slot++) {
		INIT_LIST_HEAD(&vpci_dev->dev_list[slot]);
	}

	pdev->pci_dev_data = vpci_dev;

	return 0;
}

int pciback_publish_pci_roots(struct pciback_device *pdev,
			      publish_pci_root_cb publish_cb)
{
	/* The Virtual PCI bus has only one root */
	return publish_cb(pdev, 0, 0);
}

/* Must hold pciback_device->dev_lock when calling this */
void pciback_release_devices(struct pciback_device *pdev)
{
	int slot;
	struct vpci_dev_data *vpci_dev = pdev->pci_dev_data;

	for (slot = 0; slot < PCI_SLOT_MAX; slot++) {
		struct pci_dev_entry *e, *tmp;
		list_for_each_entry_safe(e, tmp, &vpci_dev->dev_list[slot],
					 list) {
			list_del(&e->list);
			pcistub_put_pci_dev(e->dev);
			kfree(e);
		}
	}

	kfree(vpci_dev);
	pdev->pci_dev_data = NULL;
}
