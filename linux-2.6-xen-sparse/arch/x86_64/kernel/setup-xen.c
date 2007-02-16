/*
 *  linux/arch/x86-64/kernel/setup.c
 *
 *  Copyright (C) 1995  Linus Torvalds
 *
 *  Nov 2001 Dave Jones <davej@suse.de>
 *  Forked from i386 setup code.
 */

/*
 * This file handles the architecture-dependent parts of initialization
 */

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/stddef.h>
#include <linux/unistd.h>
#include <linux/ptrace.h>
#include <linux/slab.h>
#include <linux/user.h>
#include <linux/a.out.h>
#include <linux/screen_info.h>
#include <linux/ioport.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/initrd.h>
#include <linux/highmem.h>
#include <linux/bootmem.h>
#include <linux/module.h>
#include <asm/processor.h>
#include <linux/console.h>
#include <linux/seq_file.h>
#include <linux/crash_dump.h>
#include <linux/root_dev.h>
#include <linux/pci.h>
#include <linux/acpi.h>
#include <linux/kallsyms.h>
#include <linux/edd.h>
#include <linux/mmzone.h>
#include <linux/kexec.h>
#include <linux/cpufreq.h>
#include <linux/dmi.h>
#include <linux/dma-mapping.h>
#include <linux/ctype.h>

#include <asm/mtrr.h>
#include <asm/uaccess.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/smp.h>
#include <asm/msr.h>
#include <asm/desc.h>
#include <video/edid.h>
#include <asm/e820.h>
#include <asm/dma.h>
#include <asm/mpspec.h>
#include <asm/mmu_context.h>
#include <asm/bootsetup.h>
#include <asm/proto.h>
#include <asm/setup.h>
#include <asm/mach_apic.h>
#include <asm/numa.h>
#include <asm/sections.h>
#include <asm/dmi.h>
#ifdef CONFIG_XEN
#include <linux/percpu.h>
#include <xen/interface/physdev.h>
#include "setup_arch_pre.h"
#include <asm/hypervisor.h>
#include <xen/interface/nmi.h>
#include <xen/features.h>
#include <xen/xencons.h>
#define PFN_UP(x)       (((x) + PAGE_SIZE-1) >> PAGE_SHIFT)
#define PFN_PHYS(x)     ((x) << PAGE_SHIFT)
#include <asm/mach-xen/setup_arch_post.h>
#include <xen/interface/memory.h>

#ifdef CONFIG_XEN
#include <xen/interface/kexec.h>
#endif

extern unsigned long start_pfn;
extern struct edid_info edid_info;

shared_info_t *HYPERVISOR_shared_info = (shared_info_t *)empty_zero_page;
EXPORT_SYMBOL(HYPERVISOR_shared_info);

extern char hypercall_page[PAGE_SIZE];
EXPORT_SYMBOL(hypercall_page);

static int xen_panic_event(struct notifier_block *, unsigned long, void *);
static struct notifier_block xen_panic_block = {
	xen_panic_event, NULL, 0 /* try to go last */
};

unsigned long *phys_to_machine_mapping;
unsigned long *pfn_to_mfn_frame_list_list, *pfn_to_mfn_frame_list[512];

EXPORT_SYMBOL(phys_to_machine_mapping);

DEFINE_PER_CPU(multicall_entry_t, multicall_list[8]);
DEFINE_PER_CPU(int, nr_multicall_ents);

/* Raw start-of-day parameters from the hypervisor. */
start_info_t *xen_start_info;
EXPORT_SYMBOL(xen_start_info);
#endif

/*
 * Machine setup..
 */

struct cpuinfo_x86 boot_cpu_data __read_mostly;
EXPORT_SYMBOL(boot_cpu_data);

unsigned long mmu_cr4_features;

int acpi_disabled;
EXPORT_SYMBOL(acpi_disabled);
#ifdef	CONFIG_ACPI
extern int __initdata acpi_ht;
extern acpi_interrupt_flags	acpi_sci_flags;
int __initdata acpi_force = 0;
#endif

int acpi_numa __initdata;

/* Boot loader ID as an integer, for the benefit of proc_dointvec */
int bootloader_type;

unsigned long saved_video_mode;

/* 
 * Early DMI memory
 */
int dmi_alloc_index;
char dmi_alloc_data[DMI_MAX_DATA];

/*
 * Setup options
 */
struct screen_info screen_info;
EXPORT_SYMBOL(screen_info);
struct sys_desc_table_struct {
	unsigned short length;
	unsigned char table[0];
};

struct edid_info edid_info;
EXPORT_SYMBOL_GPL(edid_info);
struct e820map e820;
#ifdef CONFIG_XEN
struct e820map machine_e820;
#endif

extern int root_mountflags;

char command_line[COMMAND_LINE_SIZE];

struct resource standard_io_resources[] = {
	{ .name = "dma1", .start = 0x00, .end = 0x1f,
		.flags = IORESOURCE_BUSY | IORESOURCE_IO },
	{ .name = "pic1", .start = 0x20, .end = 0x21,
		.flags = IORESOURCE_BUSY | IORESOURCE_IO },
	{ .name = "timer0", .start = 0x40, .end = 0x43,
		.flags = IORESOURCE_BUSY | IORESOURCE_IO },
	{ .name = "timer1", .start = 0x50, .end = 0x53,
		.flags = IORESOURCE_BUSY | IORESOURCE_IO },
	{ .name = "keyboard", .start = 0x60, .end = 0x6f,
		.flags = IORESOURCE_BUSY | IORESOURCE_IO },
	{ .name = "dma page reg", .start = 0x80, .end = 0x8f,
		.flags = IORESOURCE_BUSY | IORESOURCE_IO },
	{ .name = "pic2", .start = 0xa0, .end = 0xa1,
		.flags = IORESOURCE_BUSY | IORESOURCE_IO },
	{ .name = "dma2", .start = 0xc0, .end = 0xdf,
		.flags = IORESOURCE_BUSY | IORESOURCE_IO },
	{ .name = "fpu", .start = 0xf0, .end = 0xff,
		.flags = IORESOURCE_BUSY | IORESOURCE_IO }
};

#define STANDARD_IO_RESOURCES \
	(sizeof standard_io_resources / sizeof standard_io_resources[0])

#define IORESOURCE_RAM (IORESOURCE_BUSY | IORESOURCE_MEM)

struct resource data_resource = {
	.name = "Kernel data",
	.start = 0,
	.end = 0,
	.flags = IORESOURCE_RAM,
};
struct resource code_resource = {
	.name = "Kernel code",
	.start = 0,
	.end = 0,
	.flags = IORESOURCE_RAM,
};

#define IORESOURCE_ROM (IORESOURCE_BUSY | IORESOURCE_READONLY | IORESOURCE_MEM)

static struct resource system_rom_resource = {
	.name = "System ROM",
	.start = 0xf0000,
	.end = 0xfffff,
	.flags = IORESOURCE_ROM,
};

static struct resource extension_rom_resource = {
	.name = "Extension ROM",
	.start = 0xe0000,
	.end = 0xeffff,
	.flags = IORESOURCE_ROM,
};

static struct resource adapter_rom_resources[] = {
	{ .name = "Adapter ROM", .start = 0xc8000, .end = 0,
		.flags = IORESOURCE_ROM },
	{ .name = "Adapter ROM", .start = 0, .end = 0,
		.flags = IORESOURCE_ROM },
	{ .name = "Adapter ROM", .start = 0, .end = 0,
		.flags = IORESOURCE_ROM },
	{ .name = "Adapter ROM", .start = 0, .end = 0,
		.flags = IORESOURCE_ROM },
	{ .name = "Adapter ROM", .start = 0, .end = 0,
		.flags = IORESOURCE_ROM },
	{ .name = "Adapter ROM", .start = 0, .end = 0,
		.flags = IORESOURCE_ROM }
};

#define ADAPTER_ROM_RESOURCES \
	(sizeof adapter_rom_resources / sizeof adapter_rom_resources[0])

static struct resource video_rom_resource = {
	.name = "Video ROM",
	.start = 0xc0000,
	.end = 0xc7fff,
	.flags = IORESOURCE_ROM,
};

static struct resource video_ram_resource = {
	.name = "Video RAM area",
	.start = 0xa0000,
	.end = 0xbffff,
	.flags = IORESOURCE_RAM,
};

#define romsignature(x) (*(unsigned short *)(x) == 0xaa55)

static int __init romchecksum(unsigned char *rom, unsigned long length)
{
	unsigned char *p, sum = 0;

	for (p = rom; p < rom + length; p++)
		sum += *p;
	return sum == 0;
}

static void __init probe_roms(void)
{
	unsigned long start, length, upper;
	unsigned char *rom;
	int	      i;

#ifdef CONFIG_XEN
	/* Nothing to do if not running in dom0. */
	if (!is_initial_xendomain())
		return;
#endif

	/* video rom */
	upper = adapter_rom_resources[0].start;
	for (start = video_rom_resource.start; start < upper; start += 2048) {
		rom = isa_bus_to_virt(start);
		if (!romsignature(rom))
			continue;

		video_rom_resource.start = start;

		/* 0 < length <= 0x7f * 512, historically */
		length = rom[2] * 512;

		/* if checksum okay, trust length byte */
		if (length && romchecksum(rom, length))
			video_rom_resource.end = start + length - 1;

		request_resource(&iomem_resource, &video_rom_resource);
		break;
			}

	start = (video_rom_resource.end + 1 + 2047) & ~2047UL;
	if (start < upper)
		start = upper;

	/* system rom */
	request_resource(&iomem_resource, &system_rom_resource);
	upper = system_rom_resource.start;

	/* check for extension rom (ignore length byte!) */
	rom = isa_bus_to_virt(extension_rom_resource.start);
	if (romsignature(rom)) {
		length = extension_rom_resource.end - extension_rom_resource.start + 1;
		if (romchecksum(rom, length)) {
			request_resource(&iomem_resource, &extension_rom_resource);
			upper = extension_rom_resource.start;
		}
	}

	/* check for adapter roms on 2k boundaries */
	for (i = 0; i < ADAPTER_ROM_RESOURCES && start < upper; start += 2048) {
		rom = isa_bus_to_virt(start);
		if (!romsignature(rom))
			continue;

		/* 0 < length <= 0x7f * 512, historically */
		length = rom[2] * 512;

		/* but accept any length that fits if checksum okay */
		if (!length || start + length > upper || !romchecksum(rom, length))
			continue;

		adapter_rom_resources[i].start = start;
		adapter_rom_resources[i].end = start + length - 1;
		request_resource(&iomem_resource, &adapter_rom_resources[i]);

		start = adapter_rom_resources[i++].end & ~2047UL;
	}
}

/* Check for full argument with no trailing characters */
static int fullarg(char *p, char *arg)
{
	int l = strlen(arg);
	return !memcmp(p, arg, l) && (p[l] == 0 || isspace(p[l]));
}

static __init void parse_cmdline_early (char ** cmdline_p)
{
	char c = ' ', *to = command_line, *from = COMMAND_LINE;
	int len = 0;
	int userdef = 0;

	for (;;) {
		if (c != ' ') 
			goto next_char; 

#ifdef  CONFIG_SMP
		/*
		 * If the BIOS enumerates physical processors before logical,
		 * maxcpus=N at enumeration-time can be used to disable HT.
		 */
		else if (!memcmp(from, "maxcpus=", 8)) {
			extern unsigned int maxcpus;

			maxcpus = simple_strtoul(from + 8, NULL, 0);
		}
#endif
#ifdef CONFIG_ACPI
		/* "acpi=off" disables both ACPI table parsing and interpreter init */
		if (fullarg(from,"acpi=off"))
			disable_acpi();

		if (fullarg(from, "acpi=force")) { 
			/* add later when we do DMI horrors: */
			acpi_force = 1;
			acpi_disabled = 0;
		}

		/* acpi=ht just means: do ACPI MADT parsing 
		   at bootup, but don't enable the full ACPI interpreter */
		if (fullarg(from, "acpi=ht")) { 
			if (!acpi_force)
				disable_acpi();
			acpi_ht = 1; 
		}
                else if (fullarg(from, "pci=noacpi")) 
			acpi_disable_pci();
		else if (fullarg(from, "acpi=noirq"))
			acpi_noirq_set();

		else if (fullarg(from, "acpi_sci=edge"))
			acpi_sci_flags.trigger =  1;
		else if (fullarg(from, "acpi_sci=level"))
			acpi_sci_flags.trigger = 3;
		else if (fullarg(from, "acpi_sci=high"))
			acpi_sci_flags.polarity = 1;
		else if (fullarg(from, "acpi_sci=low"))
			acpi_sci_flags.polarity = 3;

		/* acpi=strict disables out-of-spec workarounds */
		else if (fullarg(from, "acpi=strict")) {
			acpi_strict = 1;
		}
#ifdef CONFIG_X86_IO_APIC
		else if (fullarg(from, "acpi_skip_timer_override"))
			acpi_skip_timer_override = 1;
#endif
#endif

#ifndef CONFIG_XEN
		if (fullarg(from, "nolapic") || fullarg(from, "disableapic")) {
			clear_bit(X86_FEATURE_APIC, boot_cpu_data.x86_capability);
			disable_apic = 1;
		}

		if (fullarg(from, "noapic"))
			skip_ioapic_setup = 1;

		if (fullarg(from,"apic")) {
			skip_ioapic_setup = 0;
			ioapic_force = 1;
		}
#endif
			
		if (!memcmp(from, "mem=", 4))
			parse_memopt(from+4, &from); 

		if (!memcmp(from, "memmap=", 7)) {
			/* exactmap option is for used defined memory */
			if (!memcmp(from+7, "exactmap", 8)) {
#ifdef CONFIG_CRASH_DUMP
				/* If we are doing a crash dump, we
				 * still need to know the real mem
				 * size before original memory map is
				 * reset.
				 */
				saved_max_pfn = e820_end_of_ram();
#endif
				from += 8+7;
				end_pfn_map = 0;
				e820.nr_map = 0;
				userdef = 1;
			}
			else {
				parse_memmapopt(from+7, &from);
				userdef = 1;
			}
		}

#ifdef CONFIG_NUMA
		if (!memcmp(from, "numa=", 5))
			numa_setup(from+5); 
#endif

		if (!memcmp(from,"iommu=",6)) { 
			iommu_setup(from+6); 
		}

		if (fullarg(from,"oops=panic"))
			panic_on_oops = 1;

		if (!memcmp(from, "noexec=", 7))
			nonx_setup(from + 7);

#ifdef CONFIG_KEXEC
		/* crashkernel=size@addr specifies the location to reserve for
		 * a crash kernel.  By reserving this memory we guarantee
		 * that linux never set's it up as a DMA target.
		 * Useful for holding code to do something appropriate
		 * after a kernel panic.
		 */
		else if (!memcmp(from, "crashkernel=", 12)) {
#ifndef CONFIG_XEN
			unsigned long size, base;
			size = memparse(from+12, &from);
			if (*from == '@') {
				base = memparse(from+1, &from);
				/* FIXME: Do I want a sanity check
				 * to validate the memory range?
				 */
				crashk_res.start = base;
				crashk_res.end   = base + size - 1;
			}
#else
			printk("Ignoring crashkernel command line, "
			       "parameter will be supplied by xen\n");
#endif
		}
#endif

#ifdef CONFIG_PROC_VMCORE
		/* elfcorehdr= specifies the location of elf core header
		 * stored by the crashed kernel. This option will be passed
		 * by kexec loader to the capture kernel.
		 */
		else if(!memcmp(from, "elfcorehdr=", 11))
			elfcorehdr_addr = memparse(from+11, &from);
#endif

#if defined(CONFIG_HOTPLUG_CPU) && !defined(CONFIG_XEN)
		else if (!memcmp(from, "additional_cpus=", 16))
			setup_additional_cpus(from+16);
#endif

	next_char:
		c = *(from++);
		if (!c)
			break;
		if (COMMAND_LINE_SIZE <= ++len)
			break;
		*(to++) = c;
	}
	if (userdef) {
		printk(KERN_INFO "user-defined physical RAM map:\n");
		e820_print_map("user");
	}
	*to = '\0';
	*cmdline_p = command_line;
}

#ifndef CONFIG_NUMA
static void __init
contig_initmem_init(unsigned long start_pfn, unsigned long end_pfn)
{
	unsigned long bootmap_size, bootmap;

	bootmap_size = bootmem_bootmap_pages(end_pfn)<<PAGE_SHIFT;
	bootmap = find_e820_area(0, end_pfn<<PAGE_SHIFT, bootmap_size);
	if (bootmap == -1L)
		panic("Cannot find bootmem map of size %ld\n",bootmap_size);
	bootmap_size = init_bootmem(bootmap >> PAGE_SHIFT, end_pfn);
#ifdef CONFIG_XEN
	e820_bootmem_free(NODE_DATA(0), 0, xen_start_info->nr_pages<<PAGE_SHIFT);
#else
	e820_bootmem_free(NODE_DATA(0), 0, end_pfn << PAGE_SHIFT);
#endif
	reserve_bootmem(bootmap, bootmap_size);
} 
#endif

#if defined(CONFIG_EDD) || defined(CONFIG_EDD_MODULE)
struct edd edd;
#ifdef CONFIG_EDD_MODULE
EXPORT_SYMBOL(edd);
#endif
/**
 * copy_edd() - Copy the BIOS EDD information
 *              from boot_params into a safe place.
 *
 */
static inline void copy_edd(void)
{
     memcpy(edd.mbr_signature, EDD_MBR_SIGNATURE, sizeof(edd.mbr_signature));
     memcpy(edd.edd_info, EDD_BUF, sizeof(edd.edd_info));
     edd.mbr_signature_nr = EDD_MBR_SIG_NR;
     edd.edd_info_nr = EDD_NR;
}
#else
static inline void copy_edd(void)
{
}
#endif

#ifndef CONFIG_XEN
#define EBDA_ADDR_POINTER 0x40E

unsigned __initdata ebda_addr;
unsigned __initdata ebda_size;

static void discover_ebda(void)
{
	/*
	 * there is a real-mode segmented pointer pointing to the 
	 * 4K EBDA area at 0x40E
	 */
	ebda_addr = *(unsigned short *)EBDA_ADDR_POINTER;
	ebda_addr <<= 4;

	ebda_size = *(unsigned short *)(unsigned long)ebda_addr;

	/* Round EBDA up to pages */
	if (ebda_size == 0)
		ebda_size = 1;
	ebda_size <<= 10;
	ebda_size = round_up(ebda_size + (ebda_addr & ~PAGE_MASK), PAGE_SIZE);
	if (ebda_size > 64*1024)
		ebda_size = 64*1024;
}
#else
#define discover_ebda() ((void)0)
#endif

void __init setup_arch(char **cmdline_p)
{
#ifdef CONFIG_XEN
	/* Register a call for panic conditions. */
	atomic_notifier_chain_register(&panic_notifier_list, &xen_panic_block);

 	ROOT_DEV = MKDEV(RAMDISK_MAJOR,0); 
 	screen_info = SCREEN_INFO;

	if (is_initial_xendomain()) {
		/* This is drawn from a dump from vgacon:startup in
		 * standard Linux. */
		screen_info.orig_video_mode = 3;
		screen_info.orig_video_isVGA = 1;
		screen_info.orig_video_lines = 25;
		screen_info.orig_video_cols = 80;
		screen_info.orig_video_ega_bx = 3;
		screen_info.orig_video_points = 16;
		screen_info.orig_y = screen_info.orig_video_lines - 1;
		if (xen_start_info->console.dom0.info_size >=
		    sizeof(struct dom0_vga_console_info)) {
			const struct dom0_vga_console_info *info =
				(struct dom0_vga_console_info *)(
					(char *)xen_start_info +
					xen_start_info->console.dom0.info_off);
			dom0_init_screen_info(info);
		}
		xen_start_info->console.domU.mfn = 0;
		xen_start_info->console.domU.evtchn = 0;
	} else
		screen_info.orig_video_isVGA = 0;

	edid_info = EDID_INFO;
	saved_video_mode = SAVED_VIDEO_MODE;
	bootloader_type = LOADER_TYPE;

#ifdef CONFIG_BLK_DEV_RAM
	rd_image_start = RAMDISK_FLAGS & RAMDISK_IMAGE_START_MASK;
	rd_prompt = ((RAMDISK_FLAGS & RAMDISK_PROMPT_FLAG) != 0);
	rd_doload = ((RAMDISK_FLAGS & RAMDISK_LOAD_FLAG) != 0);


#endif

	setup_xen_features();

	HYPERVISOR_vm_assist(VMASST_CMD_enable,
			     VMASST_TYPE_writable_pagetables);

	ARCH_SETUP
#else
 	ROOT_DEV = old_decode_dev(ORIG_ROOT_DEV);
 	screen_info = SCREEN_INFO;
	edid_info = EDID_INFO;
	saved_video_mode = SAVED_VIDEO_MODE;
	bootloader_type = LOADER_TYPE;

#ifdef CONFIG_BLK_DEV_RAM
	rd_image_start = RAMDISK_FLAGS & RAMDISK_IMAGE_START_MASK;
	rd_prompt = ((RAMDISK_FLAGS & RAMDISK_PROMPT_FLAG) != 0);
	rd_doload = ((RAMDISK_FLAGS & RAMDISK_LOAD_FLAG) != 0);
#endif
#endif	/* !CONFIG_XEN */
	setup_memory_region();
	copy_edd();

	if (!MOUNT_ROOT_RDONLY)
		root_mountflags &= ~MS_RDONLY;
	init_mm.start_code = (unsigned long) &_text;
	init_mm.end_code = (unsigned long) &_etext;
	init_mm.end_data = (unsigned long) &_edata;
	init_mm.brk = (unsigned long) &_end;

	code_resource.start = virt_to_phys(&_text);
	code_resource.end = virt_to_phys(&_etext)-1;
	data_resource.start = virt_to_phys(&_etext);
	data_resource.end = virt_to_phys(&_edata)-1;

	parse_cmdline_early(cmdline_p);

	early_identify_cpu(&boot_cpu_data);

	/*
	 * partially used pages are not usable - thus
	 * we are rounding upwards:
	 */
	end_pfn = e820_end_of_ram();
	num_physpages = end_pfn;		/* for pfn_valid */

	check_efer();

	discover_ebda();

	init_memory_mapping(0, (end_pfn_map << PAGE_SHIFT));

	/* dmi_scan_machine(); */

#ifdef CONFIG_ACPI_NUMA
	/*
	 * Parse SRAT to discover nodes.
	 */
	acpi_numa_init();
#endif

#ifdef CONFIG_NUMA
	numa_initmem_init(0, end_pfn); 
#else
	contig_initmem_init(0, end_pfn);
#endif

	/* Reserve direct mapping */
	reserve_bootmem_generic(table_start << PAGE_SHIFT, 
				(table_end - table_start) << PAGE_SHIFT);

	/* reserve kernel */
	reserve_bootmem_generic(__pa_symbol(&_text),
				__pa_symbol(&_end) - __pa_symbol(&_text));

#ifdef CONFIG_XEN
	/* reserve physmap, start info and initial page tables */
	reserve_bootmem_generic(__pa_symbol(&_end),
				(table_start << PAGE_SHIFT) -
				__pa_symbol(&_end));
#else
	/*
	 * reserve physical page 0 - it's a special BIOS page on many boxes,
	 * enabling clean reboots, SMP operation, laptop functions.
	 */
	reserve_bootmem_generic(0, PAGE_SIZE);

	/* reserve ebda region */
	if (ebda_addr)
		reserve_bootmem_generic(ebda_addr, ebda_size);

#ifdef CONFIG_SMP
	/*
	 * But first pinch a few for the stack/trampoline stuff
	 * FIXME: Don't need the extra page at 4K, but need to fix
	 * trampoline before removing it. (see the GDT stuff)
	 */
	reserve_bootmem_generic(PAGE_SIZE, PAGE_SIZE);

	/* Reserve SMP trampoline */
	reserve_bootmem_generic(SMP_TRAMPOLINE_BASE, PAGE_SIZE);
#endif
#endif

#ifdef CONFIG_ACPI_SLEEP
       /*
        * Reserve low memory region for sleep support.
        */
       acpi_reserve_bootmem();
#endif
#ifdef CONFIG_XEN
#ifdef CONFIG_BLK_DEV_INITRD
	if (xen_start_info->mod_start) {
		if (INITRD_START + INITRD_SIZE <= (end_pfn << PAGE_SHIFT)) {
			/*reserve_bootmem_generic(INITRD_START, INITRD_SIZE);*/
			initrd_start = INITRD_START + PAGE_OFFSET;
			initrd_end = initrd_start+INITRD_SIZE;
			initrd_below_start_ok = 1;
		} else {
			printk(KERN_ERR "initrd extends beyond end of memory "
				"(0x%08lx > 0x%08lx)\ndisabling initrd\n",
				(unsigned long)(INITRD_START + INITRD_SIZE),
				(unsigned long)(end_pfn << PAGE_SHIFT));
			initrd_start = 0;
		}
	}
#endif
#else	/* CONFIG_XEN */
#ifdef CONFIG_BLK_DEV_INITRD
	if (LOADER_TYPE && INITRD_START) {
		if (INITRD_START + INITRD_SIZE <= (end_pfn << PAGE_SHIFT)) {
			reserve_bootmem_generic(INITRD_START, INITRD_SIZE);
			initrd_start =
				INITRD_START ? INITRD_START + PAGE_OFFSET : 0;
			initrd_end = initrd_start+INITRD_SIZE;
		}
		else {
			printk(KERN_ERR "initrd extends beyond end of memory "
			    "(0x%08lx > 0x%08lx)\ndisabling initrd\n",
			    (unsigned long)(INITRD_START + INITRD_SIZE),
			    (unsigned long)(end_pfn << PAGE_SHIFT));
			initrd_start = 0;
		}
	}
#endif
#endif	/* !CONFIG_XEN */
#ifdef CONFIG_KEXEC
#ifdef CONFIG_XEN
	xen_machine_kexec_setup_resources();
#else
	if (crashk_res.start != crashk_res.end) {
		reserve_bootmem_generic(crashk_res.start,
			crashk_res.end - crashk_res.start + 1);
	}
#endif
#endif

	paging_init();
#ifdef CONFIG_X86_LOCAL_APIC
	/*
	 * Find and reserve possible boot-time SMP configuration:
	 */
	find_smp_config();
#endif
#ifdef CONFIG_XEN
	{
		int i, j, k, fpp;

		if (!xen_feature(XENFEAT_auto_translated_physmap)) {
			/* Make sure we have a large enough P->M table. */
			phys_to_machine_mapping = alloc_bootmem_pages(
				end_pfn * sizeof(unsigned long));
			memset(phys_to_machine_mapping, ~0,
			       end_pfn * sizeof(unsigned long));
			memcpy(phys_to_machine_mapping,
			       (unsigned long *)xen_start_info->mfn_list,
			       xen_start_info->nr_pages * sizeof(unsigned long));
			free_bootmem(
				__pa(xen_start_info->mfn_list),
				PFN_PHYS(PFN_UP(xen_start_info->nr_pages *
						sizeof(unsigned long))));

			/*
			 * Initialise the list of the frames that specify the
			 * list of frames that make up the p2m table. Used by
                         * save/restore.
			 */
			pfn_to_mfn_frame_list_list = alloc_bootmem_pages(PAGE_SIZE);

			fpp = PAGE_SIZE/sizeof(unsigned long);
			for (i=0, j=0, k=-1; i< end_pfn; i+=fpp, j++) {
				if ((j % fpp) == 0) {
					k++;
					BUG_ON(k>=fpp);
					pfn_to_mfn_frame_list[k] =
						alloc_bootmem_pages(PAGE_SIZE);
					pfn_to_mfn_frame_list_list[k] =
						virt_to_mfn(pfn_to_mfn_frame_list[k]);
					j=0;
				}
				pfn_to_mfn_frame_list[k][j] =
					virt_to_mfn(&phys_to_machine_mapping[i]);
			}
			HYPERVISOR_shared_info->arch.max_pfn = end_pfn;
			HYPERVISOR_shared_info->arch.pfn_to_mfn_frame_list_list =
				virt_to_mfn(pfn_to_mfn_frame_list_list);
		}

	}

	if (!is_initial_xendomain()) {
		acpi_disabled = 1;
#ifdef  CONFIG_ACPI
		acpi_ht = 0;
#endif
	}
#endif

#ifndef CONFIG_XEN
	check_ioapic();
#endif

	zap_low_mappings(0);

	/*
	 * set this early, so we dont allocate cpu0
	 * if MADT list doesnt list BSP first
	 * mpparse.c/MP_processor_info() allocates logical cpu numbers.
	 */
	cpu_set(0, cpu_present_map);
#ifdef CONFIG_ACPI
	/*
	 * Initialize the ACPI boot-time table parser (gets the RSDP and SDT).
	 * Call this early for SRAT node setup.
	 */
	acpi_boot_table_init();

	/*
	 * Read APIC and some other early information from ACPI tables.
	 */
	acpi_boot_init();
#endif

	init_cpu_to_node();

#ifdef CONFIG_X86_LOCAL_APIC
	/*
	 * get boot-time SMP configuration:
	 */
	if (smp_found_config)
		get_smp_config();
#ifndef CONFIG_XEN
	init_apic_mappings();
#endif
#endif
#if defined(CONFIG_XEN) && defined(CONFIG_SMP) && !defined(CONFIG_HOTPLUG_CPU)
	prefill_possible_map();
#endif

	/*
	 * Request address space for all standard RAM and ROM resources
	 * and also for regions reported as reserved by the e820.
	 */
	probe_roms();
#ifdef CONFIG_XEN
	if (is_initial_xendomain()) {
		struct xen_memory_map memmap;

		memmap.nr_entries = E820MAX;
		set_xen_guest_handle(memmap.buffer, machine_e820.map);

		if (HYPERVISOR_memory_op(XENMEM_machine_memory_map, &memmap))
			BUG();
		machine_e820.nr_map = memmap.nr_entries;

		e820_reserve_resources(machine_e820.map, machine_e820.nr_map);
	}
#else
	e820_reserve_resources(e820.map, e820.nr_map);
#endif

	request_resource(&iomem_resource, &video_ram_resource);

	{
	unsigned i;
	/* request I/O space for devices used on all i[345]86 PCs */
	for (i = 0; i < STANDARD_IO_RESOURCES; i++)
		request_resource(&ioport_resource, &standard_io_resources[i]);
	}

#ifdef CONFIG_XEN
	if (is_initial_xendomain())
		e820_setup_gap(machine_e820.map, machine_e820.nr_map);
#else
	e820_setup_gap(e820.map, e820.nr_map);
#endif

#ifdef CONFIG_XEN
	{
		struct physdev_set_iopl set_iopl;

		set_iopl.iopl = 1;
		HYPERVISOR_physdev_op(PHYSDEVOP_set_iopl, &set_iopl);

		if (is_initial_xendomain()) {
#ifdef CONFIG_VT
#if defined(CONFIG_VGA_CONSOLE)
			conswitchp = &vga_con;
#elif defined(CONFIG_DUMMY_CONSOLE)
			conswitchp = &dummy_con;
#endif
#endif
		} else {
#if defined(CONFIG_VT) && defined(CONFIG_DUMMY_CONSOLE)
			conswitchp = &dummy_con;
#endif
                }
	}
	xencons_early_setup();
#else	/* CONFIG_XEN */

#ifdef CONFIG_VT
#if defined(CONFIG_VGA_CONSOLE)
	conswitchp = &vga_con;
#elif defined(CONFIG_DUMMY_CONSOLE)
	conswitchp = &dummy_con;
#endif
#endif

#endif /* !CONFIG_XEN */
}

#ifdef CONFIG_XEN
static int
xen_panic_event(struct notifier_block *this, unsigned long event, void *ptr)
{
	HYPERVISOR_shutdown(SHUTDOWN_crash);
	/* we're never actually going to get here... */
	return NOTIFY_DONE;
}
#endif /* !CONFIG_XEN */


static int __cpuinit get_model_name(struct cpuinfo_x86 *c)
{
	unsigned int *v;

	if (c->extended_cpuid_level < 0x80000004)
		return 0;

	v = (unsigned int *) c->x86_model_id;
	cpuid(0x80000002, &v[0], &v[1], &v[2], &v[3]);
	cpuid(0x80000003, &v[4], &v[5], &v[6], &v[7]);
	cpuid(0x80000004, &v[8], &v[9], &v[10], &v[11]);
	c->x86_model_id[48] = 0;
	return 1;
}


static void __cpuinit display_cacheinfo(struct cpuinfo_x86 *c)
{
	unsigned int n, dummy, eax, ebx, ecx, edx;

	n = c->extended_cpuid_level;

	if (n >= 0x80000005) {
		cpuid(0x80000005, &dummy, &ebx, &ecx, &edx);
		printk(KERN_INFO "CPU: L1 I Cache: %dK (%d bytes/line), D cache %dK (%d bytes/line)\n",
			edx>>24, edx&0xFF, ecx>>24, ecx&0xFF);
		c->x86_cache_size=(ecx>>24)+(edx>>24);
		/* On K8 L1 TLB is inclusive, so don't count it */
		c->x86_tlbsize = 0;
	}

	if (n >= 0x80000006) {
		cpuid(0x80000006, &dummy, &ebx, &ecx, &edx);
		ecx = cpuid_ecx(0x80000006);
		c->x86_cache_size = ecx >> 16;
		c->x86_tlbsize += ((ebx >> 16) & 0xfff) + (ebx & 0xfff);

		printk(KERN_INFO "CPU: L2 Cache: %dK (%d bytes/line)\n",
		c->x86_cache_size, ecx & 0xFF);
	}

	if (n >= 0x80000007)
		cpuid(0x80000007, &dummy, &dummy, &dummy, &c->x86_power); 
	if (n >= 0x80000008) {
		cpuid(0x80000008, &eax, &dummy, &dummy, &dummy); 
		c->x86_virt_bits = (eax >> 8) & 0xff;
		c->x86_phys_bits = eax & 0xff;
	}
}

#ifdef CONFIG_NUMA
static int nearby_node(int apicid)
{
	int i;
	for (i = apicid - 1; i >= 0; i--) {
		int node = apicid_to_node[i];
		if (node != NUMA_NO_NODE && node_online(node))
			return node;
	}
	for (i = apicid + 1; i < MAX_LOCAL_APIC; i++) {
		int node = apicid_to_node[i];
		if (node != NUMA_NO_NODE && node_online(node))
			return node;
	}
	return first_node(node_online_map); /* Shouldn't happen */
}
#endif

/*
 * On a AMD dual core setup the lower bits of the APIC id distingush the cores.
 * Assumes number of cores is a power of two.
 */
static void __init amd_detect_cmp(struct cpuinfo_x86 *c)
{
#ifdef CONFIG_SMP
	unsigned bits;
#ifdef CONFIG_NUMA
	int cpu = smp_processor_id();
	int node = 0;
	unsigned apicid = hard_smp_processor_id();
#endif
	unsigned ecx = cpuid_ecx(0x80000008);

	c->x86_max_cores = (ecx & 0xff) + 1;

	/* CPU telling us the core id bits shift? */
	bits = (ecx >> 12) & 0xF;

	/* Otherwise recompute */
	if (bits == 0) {
		while ((1 << bits) < c->x86_max_cores)
			bits++;
	}

	/* Low order bits define the core id (index of core in socket) */
	c->cpu_core_id = c->phys_proc_id & ((1 << bits)-1);
	/* Convert the APIC ID into the socket ID */
	c->phys_proc_id = phys_pkg_id(bits);

#ifdef CONFIG_NUMA
  	node = c->phys_proc_id;
 	if (apicid_to_node[apicid] != NUMA_NO_NODE)
 		node = apicid_to_node[apicid];
 	if (!node_online(node)) {
 		/* Two possibilities here:
 		   - The CPU is missing memory and no node was created.
 		   In that case try picking one from a nearby CPU
 		   - The APIC IDs differ from the HyperTransport node IDs
 		   which the K8 northbridge parsing fills in.
 		   Assume they are all increased by a constant offset,
 		   but in the same order as the HT nodeids.
 		   If that doesn't result in a usable node fall back to the
 		   path for the previous case.  */
 		int ht_nodeid = apicid - (cpu_data[0].phys_proc_id << bits);
 		if (ht_nodeid >= 0 &&
 		    apicid_to_node[ht_nodeid] != NUMA_NO_NODE)
 			node = apicid_to_node[ht_nodeid];
 		/* Pick a nearby node */
 		if (!node_online(node))
 			node = nearby_node(apicid);
 	}
	numa_set_node(cpu, node);

	printk(KERN_INFO "CPU %d/%x -> Node %d\n", cpu, apicid, node);
#endif
#endif
}

static void __init init_amd(struct cpuinfo_x86 *c)
{
	unsigned level;

#ifdef CONFIG_SMP
	unsigned long value;

	/*
	 * Disable TLB flush filter by setting HWCR.FFDIS on K8
	 * bit 6 of msr C001_0015
 	 *
	 * Errata 63 for SH-B3 steppings
	 * Errata 122 for all steppings (F+ have it disabled by default)
	 */
	if (c->x86 == 15) {
		rdmsrl(MSR_K8_HWCR, value);
		value |= 1 << 6;
		wrmsrl(MSR_K8_HWCR, value);
	}
#endif

	/* Bit 31 in normal CPUID used for nonstandard 3DNow ID;
	   3DNow is IDd by bit 31 in extended CPUID (1*32+31) anyway */
	clear_bit(0*32+31, &c->x86_capability);
	
	/* On C+ stepping K8 rep microcode works well for copy/memset */
	level = cpuid_eax(1);
	if (c->x86 == 15 && ((level >= 0x0f48 && level < 0x0f50) || level >= 0x0f58))
		set_bit(X86_FEATURE_REP_GOOD, &c->x86_capability);

	/* Enable workaround for FXSAVE leak */
	if (c->x86 >= 6)
		set_bit(X86_FEATURE_FXSAVE_LEAK, &c->x86_capability);

	level = get_model_name(c);
	if (!level) {
		switch (c->x86) { 
		case 15:
			/* Should distinguish Models here, but this is only
			   a fallback anyways. */
			strcpy(c->x86_model_id, "Hammer");
			break; 
		} 
	} 
	display_cacheinfo(c);

	/* c->x86_power is 8000_0007 edx. Bit 8 is constant TSC */
	if (c->x86_power & (1<<8))
		set_bit(X86_FEATURE_CONSTANT_TSC, &c->x86_capability);

	/* Multi core CPU? */
	if (c->extended_cpuid_level >= 0x80000008)
		amd_detect_cmp(c);

	/* Fix cpuid4 emulation for more */
	num_cache_leaves = 3;
}

static void __cpuinit detect_ht(struct cpuinfo_x86 *c)
{
#ifdef CONFIG_SMP
	u32 	eax, ebx, ecx, edx;
	int 	index_msb, core_bits;

	cpuid(1, &eax, &ebx, &ecx, &edx);


	if (!cpu_has(c, X86_FEATURE_HT))
		return;
 	if (cpu_has(c, X86_FEATURE_CMP_LEGACY))
		goto out;

	smp_num_siblings = (ebx & 0xff0000) >> 16;

	if (smp_num_siblings == 1) {
		printk(KERN_INFO  "CPU: Hyper-Threading is disabled\n");
	} else if (smp_num_siblings > 1 ) {

		if (smp_num_siblings > NR_CPUS) {
			printk(KERN_WARNING "CPU: Unsupported number of the siblings %d", smp_num_siblings);
			smp_num_siblings = 1;
			return;
		}

		index_msb = get_count_order(smp_num_siblings);
		c->phys_proc_id = phys_pkg_id(index_msb);

		smp_num_siblings = smp_num_siblings / c->x86_max_cores;

		index_msb = get_count_order(smp_num_siblings) ;

		core_bits = get_count_order(c->x86_max_cores);

		c->cpu_core_id = phys_pkg_id(index_msb) &
					       ((1 << core_bits) - 1);
	}
out:
	if ((c->x86_max_cores * smp_num_siblings) > 1) {
		printk(KERN_INFO  "CPU: Physical Processor ID: %d\n", c->phys_proc_id);
		printk(KERN_INFO  "CPU: Processor Core ID: %d\n", c->cpu_core_id);
	}

#endif
}

/*
 * find out the number of processor cores on the die
 */
static int __cpuinit intel_num_cpu_cores(struct cpuinfo_x86 *c)
{
	unsigned int eax, t;

	if (c->cpuid_level < 4)
		return 1;

	cpuid_count(4, 0, &eax, &t, &t, &t);

	if (eax & 0x1f)
		return ((eax >> 26) + 1);
	else
		return 1;
}

static void srat_detect_node(void)
{
#ifdef CONFIG_NUMA
	unsigned node;
	int cpu = smp_processor_id();
	int apicid = hard_smp_processor_id();

	/* Don't do the funky fallback heuristics the AMD version employs
	   for now. */
	node = apicid_to_node[apicid];
	if (node == NUMA_NO_NODE)
		node = first_node(node_online_map);
	numa_set_node(cpu, node);

	if (acpi_numa > 0)
		printk(KERN_INFO "CPU %d/%x -> Node %d\n", cpu, apicid, node);
#endif
}

static void __cpuinit init_intel(struct cpuinfo_x86 *c)
{
	/* Cache sizes */
	unsigned n;

	init_intel_cacheinfo(c);
	if (c->cpuid_level > 9 ) {
		unsigned eax = cpuid_eax(10);
		/* Check for version and the number of counters */
		if ((eax & 0xff) && (((eax>>8) & 0xff) > 1))
			set_bit(X86_FEATURE_ARCH_PERFMON, &c->x86_capability);
	}

	n = c->extended_cpuid_level;
	if (n >= 0x80000008) {
		unsigned eax = cpuid_eax(0x80000008);
		c->x86_virt_bits = (eax >> 8) & 0xff;
		c->x86_phys_bits = eax & 0xff;
		/* CPUID workaround for Intel 0F34 CPU */
		if (c->x86_vendor == X86_VENDOR_INTEL &&
		    c->x86 == 0xF && c->x86_model == 0x3 &&
		    c->x86_mask == 0x4)
			c->x86_phys_bits = 36;
	}

	if (c->x86 == 15)
		c->x86_cache_alignment = c->x86_clflush_size * 2;
	if ((c->x86 == 0xf && c->x86_model >= 0x03) ||
	    (c->x86 == 0x6 && c->x86_model >= 0x0e))
		set_bit(X86_FEATURE_CONSTANT_TSC, &c->x86_capability);
	set_bit(X86_FEATURE_SYNC_RDTSC, &c->x86_capability);
 	c->x86_max_cores = intel_num_cpu_cores(c);

	srat_detect_node();
}

static void __cpuinit get_cpu_vendor(struct cpuinfo_x86 *c)
{
	char *v = c->x86_vendor_id;

	if (!strcmp(v, "AuthenticAMD"))
		c->x86_vendor = X86_VENDOR_AMD;
	else if (!strcmp(v, "GenuineIntel"))
		c->x86_vendor = X86_VENDOR_INTEL;
	else
		c->x86_vendor = X86_VENDOR_UNKNOWN;
}

struct cpu_model_info {
	int vendor;
	int family;
	char *model_names[16];
};

/* Do some early cpuid on the boot CPU to get some parameter that are
   needed before check_bugs. Everything advanced is in identify_cpu
   below. */
void __cpuinit early_identify_cpu(struct cpuinfo_x86 *c)
{
	u32 tfms;

	c->loops_per_jiffy = loops_per_jiffy;
	c->x86_cache_size = -1;
	c->x86_vendor = X86_VENDOR_UNKNOWN;
	c->x86_model = c->x86_mask = 0;	/* So far unknown... */
	c->x86_vendor_id[0] = '\0'; /* Unset */
	c->x86_model_id[0] = '\0';  /* Unset */
	c->x86_clflush_size = 64;
	c->x86_cache_alignment = c->x86_clflush_size;
	c->x86_max_cores = 1;
	c->extended_cpuid_level = 0;
	memset(&c->x86_capability, 0, sizeof c->x86_capability);

	/* Get vendor name */
	cpuid(0x00000000, (unsigned int *)&c->cpuid_level,
	      (unsigned int *)&c->x86_vendor_id[0],
	      (unsigned int *)&c->x86_vendor_id[8],
	      (unsigned int *)&c->x86_vendor_id[4]);
		
	get_cpu_vendor(c);

	/* Initialize the standard set of capabilities */
	/* Note that the vendor-specific code below might override */

	/* Intel-defined flags: level 0x00000001 */
	if (c->cpuid_level >= 0x00000001) {
		__u32 misc;
		cpuid(0x00000001, &tfms, &misc, &c->x86_capability[4],
		      &c->x86_capability[0]);
		c->x86 = (tfms >> 8) & 0xf;
		c->x86_model = (tfms >> 4) & 0xf;
		c->x86_mask = tfms & 0xf;
		if (c->x86 == 0xf)
			c->x86 += (tfms >> 20) & 0xff;
		if (c->x86 >= 0x6)
			c->x86_model += ((tfms >> 16) & 0xF) << 4;
		if (c->x86_capability[0] & (1<<19)) 
			c->x86_clflush_size = ((misc >> 8) & 0xff) * 8;
	} else {
		/* Have CPUID level 0 only - unheard of */
		c->x86 = 4;
	}

#ifdef CONFIG_SMP
	c->phys_proc_id = (cpuid_ebx(1) >> 24) & 0xff;
#endif
}

/*
 * This does the hard work of actually picking apart the CPU stuff...
 */
void __cpuinit identify_cpu(struct cpuinfo_x86 *c)
{
	int i;
	u32 xlvl;

	early_identify_cpu(c);

	/* AMD-defined flags: level 0x80000001 */
	xlvl = cpuid_eax(0x80000000);
	c->extended_cpuid_level = xlvl;
	if ((xlvl & 0xffff0000) == 0x80000000) {
		if (xlvl >= 0x80000001) {
			c->x86_capability[1] = cpuid_edx(0x80000001);
			c->x86_capability[6] = cpuid_ecx(0x80000001);
		}
		if (xlvl >= 0x80000004)
			get_model_name(c); /* Default name */
	}

	/* Transmeta-defined flags: level 0x80860001 */
	xlvl = cpuid_eax(0x80860000);
	if ((xlvl & 0xffff0000) == 0x80860000) {
		/* Don't set x86_cpuid_level here for now to not confuse. */
		if (xlvl >= 0x80860001)
			c->x86_capability[2] = cpuid_edx(0x80860001);
	}

	c->apicid = phys_pkg_id(0);

	/*
	 * Vendor-specific initialization.  In this section we
	 * canonicalize the feature flags, meaning if there are
	 * features a certain CPU supports which CPUID doesn't
	 * tell us, CPUID claiming incorrect flags, or other bugs,
	 * we handle them here.
	 *
	 * At the end of this section, c->x86_capability better
	 * indicate the features this CPU genuinely supports!
	 */
	switch (c->x86_vendor) {
	case X86_VENDOR_AMD:
		init_amd(c);
		break;

	case X86_VENDOR_INTEL:
		init_intel(c);
		break;

	case X86_VENDOR_UNKNOWN:
	default:
		display_cacheinfo(c);
		break;
	}

	select_idle_routine(c);
	detect_ht(c); 

	/*
	 * On SMP, boot_cpu_data holds the common feature set between
	 * all CPUs; so make sure that we indicate which features are
	 * common between the CPUs.  The first time this routine gets
	 * executed, c == &boot_cpu_data.
	 */
	if (c != &boot_cpu_data) {
		/* AND the already accumulated flags with these */
		for (i = 0 ; i < NCAPINTS ; i++)
			boot_cpu_data.x86_capability[i] &= c->x86_capability[i];
	}

#ifdef CONFIG_X86_MCE
	mcheck_init(c);
#endif
	if (c == &boot_cpu_data)
		mtrr_bp_init();
	else
		mtrr_ap_init();
#ifdef CONFIG_NUMA
	numa_add_cpu(smp_processor_id());
#endif
}
 

void __cpuinit print_cpu_info(struct cpuinfo_x86 *c)
{
	if (c->x86_model_id[0])
		printk("%s", c->x86_model_id);

	if (c->x86_mask || c->cpuid_level >= 0) 
		printk(" stepping %02x\n", c->x86_mask);
	else
		printk("\n");
}

/*
 *	Get CPU information for use by the procfs.
 */

static int show_cpuinfo(struct seq_file *m, void *v)
{
	struct cpuinfo_x86 *c = v;

	/* 
	 * These flag bits must match the definitions in <asm/cpufeature.h>.
	 * NULL means this bit is undefined or reserved; either way it doesn't
	 * have meaning as far as Linux is concerned.  Note that it's important
	 * to realize there is a difference between this table and CPUID -- if
	 * applications want to get the raw CPUID data, they should access
	 * /dev/cpu/<cpu_nr>/cpuid instead.
	 */
	static char *x86_cap_flags[] = {
		/* Intel-defined */
	        "fpu", "vme", "de", "pse", "tsc", "msr", "pae", "mce",
	        "cx8", "apic", NULL, "sep", "mtrr", "pge", "mca", "cmov",
	        "pat", "pse36", "pn", "clflush", NULL, "dts", "acpi", "mmx",
	        "fxsr", "sse", "sse2", "ss", "ht", "tm", "ia64", NULL,

		/* AMD-defined */
		NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
		NULL, NULL, NULL, "syscall", NULL, NULL, NULL, NULL,
		NULL, NULL, NULL, NULL, "nx", NULL, "mmxext", NULL,
		NULL, "fxsr_opt", NULL, "rdtscp", NULL, "lm", "3dnowext", "3dnow",

		/* Transmeta-defined */
		"recovery", "longrun", NULL, "lrti", NULL, NULL, NULL, NULL,
		NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
		NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
		NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,

		/* Other (Linux-defined) */
		"cxmmx", NULL, "cyrix_arr", "centaur_mcr", NULL,
		"constant_tsc", NULL, NULL,
		"up", NULL, NULL, NULL, NULL, NULL, NULL, NULL,
		NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
		NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,

		/* Intel-defined (#2) */
		"pni", NULL, NULL, "monitor", "ds_cpl", "vmx", "smx", "est",
		"tm2", NULL, "cid", NULL, NULL, "cx16", "xtpr", NULL,
		NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
		NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,

		/* VIA/Cyrix/Centaur-defined */
		NULL, NULL, "rng", "rng_en", NULL, NULL, "ace", "ace_en",
		NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
		NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
		NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,

		/* AMD-defined (#2) */
		"lahf_lm", "cmp_legacy", "svm", NULL, "cr8_legacy", NULL, NULL, NULL,
		NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
		NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
		NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	};
	static char *x86_power_flags[] = { 
		"ts",	/* temperature sensor */
		"fid",  /* frequency id control */
		"vid",  /* voltage id control */
		"ttp",  /* thermal trip */
		"tm",
		"stc",
		NULL,
		/* nothing */	/* constant_tsc - moved to flags */
	};


#ifdef CONFIG_SMP
	if (!cpu_online(c-cpu_data))
		return 0;
#endif

	seq_printf(m,"processor\t: %u\n"
		     "vendor_id\t: %s\n"
		     "cpu family\t: %d\n"
		     "model\t\t: %d\n"
		     "model name\t: %s\n",
		     (unsigned)(c-cpu_data),
		     c->x86_vendor_id[0] ? c->x86_vendor_id : "unknown",
		     c->x86,
		     (int)c->x86_model,
		     c->x86_model_id[0] ? c->x86_model_id : "unknown");
	
	if (c->x86_mask || c->cpuid_level >= 0)
		seq_printf(m, "stepping\t: %d\n", c->x86_mask);
	else
		seq_printf(m, "stepping\t: unknown\n");
	
	if (cpu_has(c,X86_FEATURE_TSC)) {
		unsigned int freq = cpufreq_quick_get((unsigned)(c-cpu_data));
		if (!freq)
			freq = cpu_khz;
		seq_printf(m, "cpu MHz\t\t: %u.%03u\n",
			     freq / 1000, (freq % 1000));
	}

	/* Cache size */
	if (c->x86_cache_size >= 0) 
		seq_printf(m, "cache size\t: %d KB\n", c->x86_cache_size);
	
#ifdef CONFIG_SMP
	if (smp_num_siblings * c->x86_max_cores > 1) {
		int cpu = c - cpu_data;
		seq_printf(m, "physical id\t: %d\n", c->phys_proc_id);
		seq_printf(m, "siblings\t: %d\n", cpus_weight(cpu_core_map[cpu]));
		seq_printf(m, "core id\t\t: %d\n", c->cpu_core_id);
		seq_printf(m, "cpu cores\t: %d\n", c->booted_cores);
	}
#endif	

	seq_printf(m,
	        "fpu\t\t: yes\n"
	        "fpu_exception\t: yes\n"
	        "cpuid level\t: %d\n"
	        "wp\t\t: yes\n"
	        "flags\t\t:",
		   c->cpuid_level);

	{ 
		int i; 
		for ( i = 0 ; i < 32*NCAPINTS ; i++ )
			if (cpu_has(c, i) && x86_cap_flags[i] != NULL)
				seq_printf(m, " %s", x86_cap_flags[i]);
	}
		
	seq_printf(m, "\nbogomips\t: %lu.%02lu\n",
		   c->loops_per_jiffy/(500000/HZ),
		   (c->loops_per_jiffy/(5000/HZ)) % 100);

	if (c->x86_tlbsize > 0) 
		seq_printf(m, "TLB size\t: %d 4K pages\n", c->x86_tlbsize);
	seq_printf(m, "clflush size\t: %d\n", c->x86_clflush_size);
	seq_printf(m, "cache_alignment\t: %d\n", c->x86_cache_alignment);

	seq_printf(m, "address sizes\t: %u bits physical, %u bits virtual\n", 
		   c->x86_phys_bits, c->x86_virt_bits);

	seq_printf(m, "power management:");
	{
		unsigned i;
		for (i = 0; i < 32; i++) 
			if (c->x86_power & (1 << i)) {
				if (i < ARRAY_SIZE(x86_power_flags) &&
					x86_power_flags[i])
					seq_printf(m, "%s%s",
						x86_power_flags[i][0]?" ":"",
						x86_power_flags[i]);
				else
					seq_printf(m, " [%d]", i);
			}
	}

	seq_printf(m, "\n\n");

	return 0;
}

static void *c_start(struct seq_file *m, loff_t *pos)
{
	return *pos < NR_CPUS ? cpu_data + *pos : NULL;
}

static void *c_next(struct seq_file *m, void *v, loff_t *pos)
{
	++*pos;
	return c_start(m, pos);
}

static void c_stop(struct seq_file *m, void *v)
{
}

struct seq_operations cpuinfo_op = {
	.start =c_start,
	.next =	c_next,
	.stop =	c_stop,
	.show =	show_cpuinfo,
};

static int __init run_dmi_scan(void)
{
	dmi_scan_machine();
	return 0;
}
core_initcall(run_dmi_scan);

#if defined(CONFIG_INPUT_PCSPKR) || defined(CONFIG_INPUT_PCSPKR_MODULE)
#include <linux/platform_device.h>
static __init int add_pcspkr(void)
{
	struct platform_device *pd;
	int ret;

	pd = platform_device_alloc("pcspkr", -1);
	if (!pd)
		return -ENOMEM;

	ret = platform_device_add(pd);
	if (ret)
		platform_device_put(pd);

	return ret;
}
device_initcall(add_pcspkr);
#endif
