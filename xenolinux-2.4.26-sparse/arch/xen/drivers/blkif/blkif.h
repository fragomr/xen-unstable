/******************************************************************************
 * blkif.h
 * 
 * Unified block-device I/O interface for Xen guest OSes.
 * 
 * Copyright (c) 2003-2004, Keir Fraser
 */

#ifndef __SHARED_BLKIF_H__
#define __SHARED_BLKIF_H__

#define blkif_vdev_t   u16
#define blkif_sector_t u64

#define BLKIF_OP_READ      0
#define BLKIF_OP_WRITE     1
#define BLKIF_OP_PROBE     2

/* NB. Ring size must be small enough for sizeof(blk_ring_t) <= PAGE_SIZE. */
#define BLKIF_RING_SIZE        64

/*
 * Maximum scatter/gather segments per request.
 * This is carefully chosen so that sizeof(blk_ring_t) <= PAGE_SIZE.
 * NB. This could be 12 if the ring indexes weren't stored in the same page.
 */
#define BLKIF_REQUEST_MAX_SEGMENTS 11

typedef struct {
    unsigned char  operation;        /* BLKIF_OP_???                         */
    unsigned char  nr_segments;      /* number of segments (<= MAX_BLK_SEGS) */
    blkif_vdev_t   device;           /* only for read/write requests         */
    unsigned long  id;               /* private guest value, echoed in resp  */
    xen_sector_t   sector_number;    /* start sector idx on disk (r/w only)  */
    /* Least 9 bits is 'nr_sects'. High 23 bits is the address.      */
    unsigned long  buffer_and_sects[MAX_BLK_SEGS];
} blkif_request_t;

typedef struct {
    unsigned long   id;              /* copied from request */
    unsigned char   operation;       /* copied from request */
    int             status;          /* BLKIF_RSP_???       */
} blkif_response_t;

#define BLKIF_RSP_ERROR  -1 /* non-specific 'error' */
#define BLKIF_RSP_OKAY    0 /* non-specific 'okay'  */

/*
 * We use a special capitalised type name because it is _essential_ that all 
 * arithmetic on indexes is done on an integer type of the correct size.
 */
typedef unsigned int BLKIF_RING_IDX;

/*
 * Ring indexes are 'free running'. That is, they are not stored modulo the
 * size of the ring buffer. The following macro converts a free-running counter
 * into a value that can directly index a ring-buffer array.
 */
#define MASK_BLKIF_IDX(_i) ((_i)&(BLKIF_RING_SIZE-1))

typedef struct {
    BLKIF_RING_IDX req_prod;  /* Request producer. Updated by guest OS. */
    BLKIF_RING_IDX resp_prod; /* Response producer. Updated by Xen.     */
    union {
        blkif_request_t  req;
        blkif_response_t resp;
    } ring[BLKIF_RING_SIZE];
} blkif_ring_t;


/*
 * BLKIF_OP_PROBE:
 * The request format for a probe request is constrained as follows:
 *  @operation   == BLKIF_OP_PROBE
 *  @nr_segments == size of probe buffer in pages
 *  @device      == unused (zero)
 *  @id          == any value (echoed in response message)
 *  @sector_num  == unused (zero)
 *  @buffer_and_sects == list of page-aligned, page-sized buffers.
 *                       (i.e., nr_sects == 8).
 * 
 * The response is a list of vdisk_t elements copied into the out-of-band
 * probe buffer. On success the response status field contains the number
 * of vdisk_t elements.
 */

/* XXX SMH: Type values below are chosen to match ide_xxx in Linux ide.h. */
#define VDISK_TYPE_FLOPPY  0x00
#define VDISK_TYPE_TAPE    0x01
#define VDISK_TYPE_CDROM   0x05
#define VDISK_TYPE_OPTICAL 0x07
#define VDISK_TYPE_DISK    0x20 

#define VDISK_TYPE_MASK    0x3F
#define VDISK_TYPE(_x)     ((_x) & VDISK_TYPE_MASK) 

/* The top two bits of the type field encode various flags. */
#define VDISK_FLAG_RO      0x40
#define VDISK_FLAG_VIRT    0x80
#define VDISK_READONLY(_x) ((_x) & VDISK_FLAG_RO)
#define VDISK_VIRTUAL(_x)  ((_x) & VDISK_FLAG_VIRT) 

typedef struct {
    blkif_vdev_t   device;       /* Device number (opaque 16 bit value). */
    unsigned short info;         /* Device type and flags (VDISK_*).     */
    xen_sector_t   capacity;     /* Size in terms of 512-byte sectors.   */
} vdisk_t;

#endif /* __SHARED_BLKIF_H__ */
