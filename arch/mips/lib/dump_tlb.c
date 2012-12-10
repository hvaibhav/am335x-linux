/*
 * Dump R4x00 TLB for debugging purposes.
 *
 * Copyright (C) 1994, 1995 by Waldorf Electronics, written by Ralf Baechle.
 * Copyright (C) 1999 by Silicon Graphics, Inc.
 */
#include <linux/kernel.h>
#include <linux/hugetlb.h>
#include <linux/mm.h>
#include <linux/sched.h>

#include <asm/mipsregs.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/current.h>
#include <asm/tlbdebug.h>

static inline const char *msk2str(unsigned int mask)
{
	switch (mask) {
	case PM_4K:	return "4kb";
	case PM_16K:	return "16kb";
	case PM_64K:	return "64kb";
	case PM_256K:	return "256kb";
#ifdef CONFIG_CPU_CAVIUM_OCTEON
	case PM_8K:	return "8kb";
	case PM_32K:	return "32kb";
	case PM_128K:	return "128kb";
	case PM_512K:	return "512kb";
	case PM_2M:	return "2Mb";
	case PM_8M:	return "8Mb";
	case PM_32M:	return "32Mb";
#endif
#ifndef CONFIG_CPU_VR41XX
	case PM_1M:	return "1Mb";
	case PM_4M:	return "4Mb";
	case PM_16M:	return "16Mb";
	case PM_64M:	return "64Mb";
	case PM_256M:	return "256Mb";
	case PM_1G:	return "1Gb";
#endif
	}
	return "";
}

#define BARRIER()					\
	__asm__ __volatile__(				\
		".set\tnoreorder\n\t"			\
		"nop;nop;nop;nop;nop;nop;nop\n\t"	\
		".set\treorder");

static void dump_tlb(int first, int last)
{
	unsigned long s_entryhi, entryhi, asid;
	unsigned long long entrylo0, entrylo1;
	unsigned int s_index, s_pagemask, pagemask, c0, c1, i;

	s_pagemask = read_c0_pagemask();
	s_entryhi = read_c0_entryhi();
	s_index = read_c0_index();
	asid = s_entryhi & 0xff;

	for (i = first; i <= last; i++) {
		write_c0_index(i);
		BARRIER();
		tlb_read();
		BARRIER();
		pagemask = read_c0_pagemask();
		entryhi  = read_c0_entryhi();
		entrylo0 = read_c0_entrylo0();
		entrylo1 = read_c0_entrylo1();

		/* Unused entries have a virtual address of CKSEG0.  */
		if ((entryhi & ~0x1ffffUL) != CKSEG0
		    && (entryhi & 0xff) == asid) {
#ifdef CONFIG_32BIT
			int width = 8;
#else
			int width = 11;
#endif
			/*
			 * Only print entries in use
			 */
			printk("Index: %2d pgmask=%s ", i, msk2str(pagemask));

			c0 = (entrylo0 >> 3) & 7;
			c1 = (entrylo1 >> 3) & 7;

			printk("va=%0*lx asid=%02lx\n",
			       width, (entryhi & ~0x1fffUL),
			       entryhi & 0xff);
			printk("\t[pa=%0*llx c=%d d=%d v=%d g=%d] ",
			       width,
			       (entrylo0 << 6) & PAGE_MASK, c0,
			       (entrylo0 & 4) ? 1 : 0,
			       (entrylo0 & 2) ? 1 : 0,
			       (entrylo0 & 1) ? 1 : 0);
			printk("[pa=%0*llx c=%d d=%d v=%d g=%d]\n",
			       width,
			       (entrylo1 << 6) & PAGE_MASK, c1,
			       (entrylo1 & 4) ? 1 : 0,
			       (entrylo1 & 2) ? 1 : 0,
			       (entrylo1 & 1) ? 1 : 0);
		}
	}
	printk("\n");

	write_c0_entryhi(s_entryhi);
	write_c0_index(s_index);
	write_c0_pagemask(s_pagemask);
}

void dump_tlb_all(void)
{
	dump_tlb(0, current_cpu_data.tlbsize - 1);
}

void dump_current_addr(unsigned long addr)
{
	pgd_t *pgdp;
	pud_t *pudp;
	pmd_t *pmdp;
	pte_t *ptep;
	pte_t pte;

	printk("Dumping for address %lx\n", addr);
	pgdp = pgd_offset(current->mm, addr);
	printk("pgd %lx\n", pgd_val(*pgdp));

	pudp = pud_offset(pgdp, addr);
	printk("pud %lx", pud_val(*pudp));
#ifndef __PAGETABLE_PMD_FOLDED
	if (pud_val(*pudp) == (unsigned long) invalid_pmd_table) {
		printk("  (invalid_pmd_table)\n");

		return;
	}
#endif
	printk("\n");

	pmdp = pmd_offset(pudp, addr);
	printk("pmd  %lx", pmd_val(*pmdp));
	if (pmd_huge(*pmdp)) {
		printk("  pmd is huge\n");

		return;
	}
	if (pmd_val(*pmdp) == (unsigned long) invalid_pte_table) {
		printk("  (invalid_pte_table)\n");

		return;
	}
	printk("\n");

	ptep = pte_offset_map(pmdp, addr);
	pte = *ptep;
	printk("pte %lx\n", pte_val(pte));
	if (pte_huge(pte))
		printk("  pte is huge\n");
}
