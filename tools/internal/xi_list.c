/******************************************************************************
 * xi_list.c
 * 
 * This is a silly little program to dump the currently running domains.
 * The output format is a series of space-separate fields for each domain:
 * 
 *  1. Domain id
 *  2. Processor
 *  3. Has CPU (1 => true, 0 => false)
 *  4. State (integer)
 *  5. State (RUNNING, INTERRUPTIBLE, UNINTERRUPTIBLE, WAIT, SUSPENDED, DYING)
 *  6. Pending events (hex value)
 *  7. MCU advance
 *  8. Total pages
 *  9. Name
 */

/*
 * Xen indicates when we've read info on all domains by returning error ESRCH. 
 * We don't want the helper functiosn to interpret this as a real error!
 */
#define SILENT_ERRORS_FROM_XEN

#include "dom0_defs.h"
#include "mem_defs.h"

static char *argv0 = "internal_domain_list";

static char *statestr(int state)
{
    switch ( state )
    {
    case  0: return "RUNNING";
    case  1: return "INTERRUPTIBLE";
    case  2: return "UNINTERRUPTIBLE";
    case  4: return "WAIT";
    case  8: return "SUSPENDED";
    case 16: return "DYING";
    default: return "UNKNOWN";
    }
    return NULL;
}

int main(int argc, char **argv)
{
    dom0_op_t op;

    if ( argv[0] != NULL ) 
        argv0 = argv[0];

    if ( argc != 1 ) 
    {
        fprintf(stderr, "Usage: %s\n", argv0);
        return 1;
    }

    op.cmd = DOM0_GETDOMAININFO;
    op.u.getdominfo.domain = 0;
    while ( do_dom0_op(&op) >= 0 )
    {
        printf("%8d %2d %1d %2d %s %08x %8ld %8d %s\n",
               op.u.getdominfo.domain, 
               op.u.getdominfo.processor,
               op.u.getdominfo.has_cpu,
               op.u.getdominfo.state,
               statestr(op.u.getdominfo.state),
               op.u.getdominfo.hyp_events,
               op.u.getdominfo.mcu_advance,
               op.u.getdominfo.tot_pages,
               op.u.getdominfo.name);
        op.u.getdominfo.domain++;
    }

    return 0;
}
