/******************************************************************************
 * xl_vbd.c
 * 
 * Xenolinux virtual block-device driver (xvd).
 * 
 * Copyright (c) 2003-2004, Keir Fraser & Steve Hand
 * Modifications by Mark A. Williamson are (c) Intel Research Cambridge
 */

#include "xl_block.h"
#include <linux/blk.h>

/*
 * For convenience we distinguish between ide, scsi and 'other' (i.e.
 * potentially combinations of the two) in the naming scheme and in a few 
 * other places (like default readahead, etc).
 */
#define XLIDE_MAJOR_NAME  "hd"
#define XLSCSI_MAJOR_NAME "sd"
#define XLVBD_MAJOR_NAME "xvd"

#define XLIDE_DEVS_PER_MAJOR   2
#define XLSCSI_DEVS_PER_MAJOR 16
#define XLVBD_DEVS_PER_MAJOR  16

#define XLIDE_PARTN_SHIFT  6    /* amount to shift minor to get 'real' minor */
#define XLIDE_MAX_PART    (1 << XLIDE_PARTN_SHIFT)     /* minors per ide vbd */

#define XLSCSI_PARTN_SHIFT 4    /* amount to shift minor to get 'real' minor */
#define XLSCSI_MAX_PART   (1 << XLSCSI_PARTN_SHIFT)   /* minors per scsi vbd */

#define XLVBD_PARTN_SHIFT  4    /* amount to shift minor to get 'real' minor */
#define XLVBD_MAX_PART    (1 << XLVBD_PARTN_SHIFT) /* minors per 'other' vbd */

/* The below are for the generic drivers/block/ll_rw_block.c code. */
static int xlide_blksize_size[256];
static int xlide_hardsect_size[256];
static int xlide_max_sectors[256];
static int xlscsi_blksize_size[256];
static int xlscsi_hardsect_size[256];
static int xlscsi_max_sectors[256];
static int xlvbd_blksize_size[256];
static int xlvbd_hardsect_size[256];
static int xlvbd_max_sectors[256];

/* Information from Xen about our VBDs. */
#define MAX_VBDS 64
static int nr_vbds;
static xen_disk_t *vbd_info;

static struct block_device_operations xlvbd_block_fops = 
{
    open:               xenolinux_block_open,
    release:            xenolinux_block_release,
    ioctl:              xenolinux_block_ioctl,
    check_media_change: xenolinux_block_check,
    revalidate:         xenolinux_block_revalidate,
};

static int xlvbd_get_vbd_info(xen_disk_t *disk_info)
{
    int error;
    block_io_op_t op; 

    /* Probe for disk information. */
    memset(&op, 0, sizeof(op)); 
    op.cmd = BLOCK_IO_OP_VBD_PROBE; 
    op.u.probe_params.domain    = 0; 
    op.u.probe_params.xdi.max   = MAX_VBDS;
    op.u.probe_params.xdi.disks = disk_info;
    op.u.probe_params.xdi.count = 0;

    if ( (error = HYPERVISOR_block_io_op(&op)) != 0 )
    {
        printk(KERN_ALERT "Could not probe disks (%d)\n", error);
        return -1;
    }

    return op.u.probe_params.xdi.count;
}

/*
 * xlvbd_init_device - initialise a VBD device
 * @disk:              a xen_disk_t describing the VBD
 *
 * Takes a xen_disk_t * that describes a VBD the domain has access to.
 * Performs appropriate initialisation and registration of the device.
 *
 * Care needs to be taken when making re-entrant calls to ensure that
 * corruption does not occur.  Also, devices that are in use should not have
 * their details updated.  This is the caller's responsibility.
 */
static int xlvbd_init_device(xen_disk_t *xd)
{
    int device = xd->device;
    int major  = MAJOR(device); 
    int minor  = MINOR(device);
    int is_ide = IDE_DISK_MAJOR(major);  /* is this an ide device? */
    int is_scsi= SCSI_BLK_MAJOR(major);  /* is this a scsi device? */
    char *major_name;
    struct gendisk *gd;
    struct block_device *bd;
    xl_disk_t *disk;
    int i, rc = 0, max_part, partno;

    unsigned char buf[64];

    if ( (bd = bdget(device)) == NULL )
        return -1;

    /*
     * Update of partition info, and check of usage count, is protected
     * by the per-block-device semaphore.
     */
    down(&bd->bd_sem);

    if ( ((disk = xldev_to_xldisk(device)) != NULL) && (disk->usage != 0) )
    {
        printk(KERN_ALERT "VBD update failed - in use [dev=%x]\n", device);
        rc = -1;
        goto out;
    }

    if ( is_ide )
    { 
	major_name = XLIDE_MAJOR_NAME; 
	max_part   = XLIDE_MAX_PART;
    }
    else if ( is_scsi )
    { 
	major_name = XLSCSI_MAJOR_NAME;
	max_part   = XLSCSI_MAX_PART;
    }
    else
    { 
	major_name = XLVBD_MAJOR_NAME;
	max_part   = XLVBD_MAX_PART;
    }
    
    partno = minor & (max_part - 1); 
    
    if ( (gd = get_gendisk(device)) == NULL )
    {
        rc = register_blkdev(major, major_name, &xlvbd_block_fops);
        if ( rc < 0 )
        {
            printk(KERN_ALERT "XL VBD: can't get major %d\n", major);
            goto out;
        }

        if ( is_ide )
        { 
            blksize_size[major]  = xlide_blksize_size;
            hardsect_size[major] = xlide_hardsect_size;
            max_sectors[major]   = xlide_max_sectors;
            read_ahead[major]    = 8; /* from drivers/ide/ide-probe.c */
        } 
        else if ( is_scsi )
        { 
            blksize_size[major]  = xlscsi_blksize_size;
            hardsect_size[major] = xlscsi_hardsect_size;
            max_sectors[major]   = xlscsi_max_sectors;
            read_ahead[major]    = 0; /* XXX 8; -- guessing */
        }
        else
        { 
            blksize_size[major]  = xlvbd_blksize_size;
            hardsect_size[major] = xlvbd_hardsect_size;
            max_sectors[major]   = xlvbd_max_sectors;
            read_ahead[major]    = 8;
        }

        blk_init_queue(BLK_DEFAULT_QUEUE(major), do_xlblk_request);

        /*
         * Turn off barking 'headactive' mode. We dequeue buffer heads as
         * soon as we pass them down to Xen.
         */
        blk_queue_headactive(BLK_DEFAULT_QUEUE(major), 0);

        /* Construct an appropriate gendisk structure. */
        gd             = kmalloc(sizeof(struct gendisk), GFP_KERNEL);
        gd->major      = major;
        gd->major_name = major_name; 
    
        gd->max_p      = max_part; 
        if ( is_ide )
        { 
            gd->minor_shift  = XLIDE_PARTN_SHIFT; 
            gd->nr_real      = XLIDE_DEVS_PER_MAJOR; 
        } 
        else if ( is_scsi )
        { 
            gd->minor_shift  = XLSCSI_PARTN_SHIFT; 
            gd->nr_real      = XLSCSI_DEVS_PER_MAJOR; 
        }
        else
        { 
            gd->minor_shift  = XLVBD_PARTN_SHIFT; 
            gd->nr_real      = XLVBD_DEVS_PER_MAJOR; 
        }

        /* 
        ** The sizes[] and part[] arrays hold the sizes and other 
        ** information about every partition with this 'major' (i.e. 
        ** every disk sharing the 8 bit prefix * max partns per disk) 
        */
        gd->sizes = kmalloc(max_part*gd->nr_real*sizeof(int), GFP_KERNEL);
        gd->part  = kmalloc(max_part*gd->nr_real*sizeof(struct hd_struct), 
                            GFP_KERNEL);
        memset(gd->sizes, 0, max_part * gd->nr_real * sizeof(int));
        memset(gd->part,  0, max_part * gd->nr_real 
               * sizeof(struct hd_struct));


        gd->real_devices = kmalloc(gd->nr_real * sizeof(xl_disk_t), 
                                   GFP_KERNEL);
        memset(gd->real_devices, 0, gd->nr_real * sizeof(xl_disk_t));

        gd->next   = NULL;            
        gd->fops   = &xlvbd_block_fops;

        gd->de_arr = kmalloc(gd->nr_real * sizeof(*gd->de_arr), 
                             GFP_KERNEL);
        gd->flags  = kmalloc(gd->nr_real * sizeof(*gd->flags), GFP_KERNEL);
    
        memset(gd->de_arr, 0, gd->nr_real * sizeof(*gd->de_arr));
        memset(gd->flags, 0, gd->nr_real *  sizeof(*gd->flags));

        add_gendisk(gd);

        blk_size[major] = gd->sizes;
    }

    if ( XD_READONLY(xd->info) )
        set_device_ro(device, 1); 

    gd->flags[minor >> gd->minor_shift] |= GENHD_FL_XENO;
        
    if ( partno != 0 )
    {
        /*
         * If this was previously set up as a real disc we will have set 
         * up partition-table information. Virtual partitions override 
         * 'real' partitions, and the two cannot coexist on a device.
         */
        if ( !(gd->flags[minor >> gd->minor_shift] & GENHD_FL_VIRT_PARTNS) &&
             (gd->sizes[minor & ~(max_part-1)] != 0) )
        {
            /*
             * Any non-zero sub-partition entries must be cleaned out before
             * installing 'virtual' partition entries. The two types cannot
             * coexist, and virtual partitions are favoured.
             */
            kdev_t dev = device & ~(max_part-1);
            for ( i = max_part - 1; i > 0; i-- )
            {
                invalidate_device(dev+i, 1);
                gd->part[MINOR(dev+i)].start_sect = 0;
                gd->part[MINOR(dev+i)].nr_sects   = 0;
                gd->sizes[MINOR(dev+i)]           = 0;
            }
            printk(KERN_ALERT
                   "Virtual partitions found for /dev/%s - ignoring any "
                   "real partition information we may have found.\n",
                   disk_name(gd, MINOR(device), buf));
        }

        /* Need to skankily setup 'partition' information */
        gd->part[minor].start_sect = 0; 
        gd->part[minor].nr_sects   = xd->capacity; 
        gd->sizes[minor]           = xd->capacity; 

        gd->flags[minor >> gd->minor_shift] |= GENHD_FL_VIRT_PARTNS;
    }
    else
    {
        gd->part[minor].nr_sects = xd->capacity;
        gd->sizes[minor] = xd->capacity>>(BLOCK_SIZE_BITS-9);
        
        /* Some final fix-ups depending on the device type */
        switch ( XD_TYPE(xd->info) )
        { 
        case XD_TYPE_CDROM:
        case XD_TYPE_FLOPPY: 
        case XD_TYPE_TAPE:
            gd->flags[minor >> gd->minor_shift] |= GENHD_FL_REMOVABLE; 
            printk(KERN_ALERT 
                   "Skipping partition check on %s /dev/%s\n", 
                   XD_TYPE(xd->info)==XD_TYPE_CDROM ? "cdrom" : 
                   (XD_TYPE(xd->info)==XD_TYPE_TAPE ? "tape" : 
                    "floppy"), disk_name(gd, MINOR(device), buf)); 
            break; 

        case XD_TYPE_DISK:
            /* Only check partitions on real discs (not virtual!). */
            if ( gd->flags[minor>>gd->minor_shift] & GENHD_FL_VIRT_PARTNS )
            {
                printk(KERN_ALERT
                       "Skipping partition check on virtual /dev/%s\n",
                       disk_name(gd, MINOR(device), buf));
                break;
            }
            register_disk(gd, device, gd->max_p, &xlvbd_block_fops, 
                          xd->capacity);            
            break; 

        default:
            printk(KERN_ALERT "XenoLinux: unknown device type %d\n", 
                   XD_TYPE(xd->info)); 
            break; 
        }
    }

 out:
    up(&bd->bd_sem);
    bdput(bd);    
    return rc;
}


/*
 * xlvbd_remove_device - remove a device node if possible
 * @device:       numeric device ID
 *
 * Updates the gendisk structure and invalidates devices.
 *
 * This is OK for now but in future, should perhaps consider where this should
 * deallocate gendisks / unregister devices.
 */
static int xlvbd_remove_device(int device)
{
    int i, rc = 0, minor = MINOR(device);
    struct gendisk *gd;
    struct block_device *bd;
    xl_disk_t *disk = NULL;

    if ( (bd = bdget(device)) == NULL )
        return -1;

    /*
     * Update of partition info, and check of usage count, is protected
     * by the per-block-device semaphore.
     */
    down(&bd->bd_sem);

    if ( ((gd = get_gendisk(device)) == NULL) ||
         ((disk = xldev_to_xldisk(device)) == NULL) )
        BUG();

    if ( disk->usage != 0 )
    {
        printk(KERN_ALERT "VBD removal failed - in use [dev=%x]\n", device);
        rc = -1;
        goto out;
    }
 
    if ( (minor & (gd->max_p-1)) != 0 )
    {
        /* 1: The VBD is mapped to a partition rather than a whole unit. */
        invalidate_device(device, 1);
	gd->part[minor].start_sect = 0;
        gd->part[minor].nr_sects   = 0;
        gd->sizes[minor]           = 0;

        /* Clear the consists-of-virtual-partitions flag if possible. */
        gd->flags[minor >> gd->minor_shift] &= ~GENHD_FL_VIRT_PARTNS;
        for ( i = 1; i < gd->max_p; i++ )
            if ( gd->sizes[(minor & ~(gd->max_p-1)) + i] != 0 )
                gd->flags[minor >> gd->minor_shift] |= GENHD_FL_VIRT_PARTNS;

        /*
         * If all virtual partitions are now gone, and a 'whole unit' VBD is
         * present, then we can try to grok the unit's real partition table.
         */
        if ( !(gd->flags[minor >> gd->minor_shift] & GENHD_FL_VIRT_PARTNS) &&
             (gd->sizes[minor & ~(gd->max_p-1)] != 0) &&
             !(gd->flags[minor >> gd->minor_shift] & GENHD_FL_REMOVABLE) )
        {
            register_disk(gd,
                          device&~(gd->max_p-1), 
                          gd->max_p, 
                          &xlvbd_block_fops,
                          gd->part[minor&~(gd->max_p-1)].nr_sects);
        }
    }
    else
    {
        /*
         * 2: The VBD is mapped to an entire 'unit'. Clear all partitions.
         * NB. The partition entries are only cleared if there are no VBDs
         * mapped to individual partitions on this unit.
         */
        i = gd->max_p - 1; /* Default: clear subpartitions as well. */
        if ( gd->flags[minor >> gd->minor_shift] & GENHD_FL_VIRT_PARTNS )
            i = 0; /* 'Virtual' mode: only clear the 'whole unit' entry. */
        while ( i >= 0 )
        {
            invalidate_device(device+i, 1);
            gd->part[minor+i].start_sect = 0;
            gd->part[minor+i].nr_sects   = 0;
            gd->sizes[minor+i]           = 0;
            i--;
        }
    }

 out:
    up(&bd->bd_sem);
    bdput(bd);
    return rc;
}

/*
 * xlvbd_update_vbds - reprobes the VBD status and performs updates driver
 * state. The VBDs need to be updated in this way when the domain is
 * initialised and also each time we receive an XLBLK_UPDATE event.
 */
void xlvbd_update_vbds(void)
{
    int i, j, k, old_nr, new_nr;
    xen_disk_t *old_info, *new_info, *merged_info;

    old_info = vbd_info;
    old_nr   = nr_vbds;

    new_info = kmalloc(MAX_VBDS * sizeof(xen_disk_t), GFP_KERNEL);
    if ( unlikely(new_nr = xlvbd_get_vbd_info(new_info)) < 0 )
    {
        kfree(new_info);
        return;
    }

    /*
     * Final list maximum size is old list + new list. This occurs only when
     * old list and new list do not overlap at all, and we cannot yet destroy
     * VBDs in the old list because the usage counts are busy.
     */
    merged_info = kmalloc((old_nr + new_nr) * sizeof(xen_disk_t), GFP_KERNEL);

    /* @i tracks old list; @j tracks new list; @k tracks merged list. */
    i = j = k = 0;

    while ( (i < old_nr) && (j < new_nr) )
    {
        if ( old_info[i].device < new_info[j].device )
        {
            if ( xlvbd_remove_device(old_info[i].device) != 0 )
                memcpy(&merged_info[k++], &old_info[i], sizeof(xen_disk_t));
            i++;
        }
        else if ( old_info[i].device > new_info[j].device )
        {
            if ( xlvbd_init_device(&new_info[j]) == 0 )
                memcpy(&merged_info[k++], &new_info[j], sizeof(xen_disk_t));
            j++;
        }
        else
        {
            if ( (memcmp(&old_info[i], &new_info[j], sizeof(xen_disk_t)) == 0) ||
                 (xlvbd_remove_device(old_info[i].device) != 0) )
                memcpy(&merged_info[k++], &old_info[i], sizeof(xen_disk_t));
            else if ( xlvbd_init_device(&new_info[j]) == 0 )
                memcpy(&merged_info[k++], &new_info[j], sizeof(xen_disk_t));
            i++; j++;
        }
    }

    for ( ; i < old_nr; i++ )
    {
        if ( xlvbd_remove_device(old_info[i].device) != 0 )
            memcpy(&merged_info[k++], &old_info[i], sizeof(xen_disk_t));
    }

    for ( ; j < new_nr; j++ )
    {
        if ( xlvbd_init_device(&new_info[j]) == 0 )
            memcpy(&merged_info[k++], &new_info[j], sizeof(xen_disk_t));
    }

    vbd_info = merged_info;
    nr_vbds  = k;

    kfree(old_info);
    kfree(new_info);
}


/*
 * Set up all the linux device goop for the virtual block devices (vbd's) that 
 * xen tells us about. Note that although from xen's pov VBDs are addressed 
 * simply an opaque 16-bit device number, the domain creation tools 
 * conventionally allocate these numbers to correspond to those used by 'real' 
 * linux -- this is just for convenience as it means e.g. that the same 
 * /etc/fstab can be used when booting with or without xen.
 */
int __init xlvbd_init(void)
{
    int i;
    
    /*
     * If compiled as a module, we don't support unloading yet. We therefore 
     * permanently increment the reference count to disallow it.
     */
    SET_MODULE_OWNER(&xlvbd_block_fops);
    MOD_INC_USE_COUNT;

    /* Initialize the global arrays. */
    for ( i = 0; i < 256; i++ ) 
    {
        /* from the generic ide code (drivers/ide/ide-probe.c, etc) */
        xlide_blksize_size[i]  = 1024;
        xlide_hardsect_size[i] = 512;
        xlide_max_sectors[i]   = 128;  /* 'hwif->rqsize' if we knew it */

        /* from the generic scsi disk code (drivers/scsi/sd.c) */
        xlscsi_blksize_size[i]  = 1024; /* XXX 512; */
        xlscsi_hardsect_size[i] = 512;
        xlscsi_max_sectors[i]   = 128*8; /* XXX 128; */

        /* we don't really know what to set these too since it depends */
        xlvbd_blksize_size[i]  = 512;
        xlvbd_hardsect_size[i] = 512;
        xlvbd_max_sectors[i]   = 128;
    }

    vbd_info = kmalloc(MAX_VBDS * sizeof(xen_disk_t), GFP_KERNEL);
    nr_vbds  = xlvbd_get_vbd_info(vbd_info);

    if ( nr_vbds < 0 )
    {
        kfree(vbd_info);
        vbd_info = NULL;
        nr_vbds  = 0;
    }
    else
    {
        for ( i = 0; i < nr_vbds; i++ )
            xlvbd_init_device(&vbd_info[i]);
    }

    return 0;
}


#ifdef MODULE
module_init(xlvbd_init);
#endif
