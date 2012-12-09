#ifndef __ASM_GENERIC_MMAN_H
#define __ASM_GENERIC_MMAN_H

#include <asm-generic/mman-common.h>

#define MAP_GROWSDOWN	0x0100		/* stack-like segment */
#define MAP_DENYWRITE	0x0800		/* ETXTBSY */
#define MAP_EXECUTABLE	0x1000		/* mark it as an executable */
#define MAP_LOCKED	0x2000		/* pages are locked */
#define MAP_NORESERVE	0x4000		/* don't check for reservations */
#define MAP_POPULATE	0x8000		/* populate (prefault) pagetables */
#define MAP_NONBLOCK	0x10000		/* do not block on IO */
#define MAP_STACK	0x20000		/* give out an address that is best suited for process/thread stacks */
#define MAP_HUGETLB	0x40000		/* create a huge page mapping */

/* Bits [26:31] are reserved */

/*
 * When MAP_HUGETLB is set bits [26:31] encode the log2 of the huge page size.
 * This gives us 6 bits, which is enough until someone invents 128 bit address
 * spaces.
 *
 * Assume these are all power of twos.
 * When 0 use the default page size.
 */
#define MAP_HUGE_SHIFT  26
#define MAP_HUGE_MASK   0x3f

#define MCL_CURRENT	1		/* lock all current mappings */
#define MCL_FUTURE	2		/* lock all future mappings */

#endif /* __ASM_GENERIC_MMAN_H */
