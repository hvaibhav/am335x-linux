#ifndef _LINUX_PAGE_FLAGS_LAYOUT
#define _LINUX_PAGE_FLAGS_LAYOUT

#include <linux/numa.h>
#include <generated/bounds.h>

#if MAX_NR_ZONES < 2
#define ZONES_SHIFT 0
#elif MAX_NR_ZONES <= 2
#define ZONES_SHIFT 1
#elif MAX_NR_ZONES <= 4
#define ZONES_SHIFT 2
#else
#error ZONES_SHIFT -- too many zones configured adjust calculation
#endif

#ifdef CONFIG_SPARSEMEM
#include <asm/sparsemem.h>

/* 
 * SECTION_SHIFT    		#bits space required to store a section #
 */
#define SECTIONS_SHIFT         (MAX_PHYSMEM_BITS - SECTION_SIZE_BITS)
#endif

/*
 * page->flags layout:
 *
 * There are five possibilities for how page->flags get laid out.  The first
 * (and second) is for the normal case, without sparsemem. The third is for
 * sparsemem when there is plenty of space for node and section. The last is
 * when we have run out of space and have to fall back to an alternate (slower)
 * way of determining the node.
 *
 * No sparsemem or sparsemem vmemmap: |       NODE     | ZONE |            ... | FLAGS |
 *     "      plus space for last_cpu:|       NODE     | ZONE | LAST_CPU | ... | FLAGS |
 * classic sparse with space for node:| SECTION | NODE | ZONE |            ... | FLAGS |
 *     "      plus space for last_cpu:| SECTION | NODE | ZONE | LAST_CPU | ... | FLAGS |
 * classic sparse no space for node:  | SECTION |     ZONE    |            ... | FLAGS |
 */
#if defined(CONFIG_SPARSEMEM) && !defined(CONFIG_SPARSEMEM_VMEMMAP)

#define SECTIONS_WIDTH		SECTIONS_SHIFT
#else
#define SECTIONS_WIDTH		0
#endif

#define ZONES_WIDTH		ZONES_SHIFT

#if SECTIONS_WIDTH+ZONES_WIDTH+NODES_SHIFT <= BITS_PER_LONG - NR_PAGEFLAGS
#define NODES_WIDTH		NODES_SHIFT
#else
#ifdef CONFIG_SPARSEMEM_VMEMMAP
#error "Vmemmap: No space for nodes field in page flags"
#endif
#define NODES_WIDTH		0
#endif

#ifdef CONFIG_NUMA_BALANCING
#define LAST_CPU_SHIFT	NR_CPUS_BITS
#else
#define LAST_CPU_SHIFT	0
#endif

#if SECTIONS_WIDTH+ZONES_WIDTH+NODES_SHIFT+LAST_CPU_SHIFT <= BITS_PER_LONG - NR_PAGEFLAGS
#define LAST_CPU_WIDTH	LAST_CPU_SHIFT
#else
#define LAST_CPU_WIDTH	0
#endif

/*
 * We are going to use the flags for the page to node mapping if its in
 * there.  This includes the case where there is no node, so it is implicit.
 */
#if !(NODES_WIDTH > 0 || NODES_SHIFT == 0)
#define NODE_NOT_IN_PAGE_FLAGS
#endif

#if defined(CONFIG_NUMA_BALANCING) && LAST_CPU_WIDTH == 0
#define LAST_CPU_NOT_IN_PAGE_FLAGS
#endif

#endif /* _LINUX_PAGE_FLAGS_LAYOUT */
