/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2002 by Ralf Baechle
 */
#ifndef __ASM_TLBDEBUG_H
#define __ASM_TLBDEBUG_H

/*
 * TLB debugging functions:
 */
extern void dump_tlb_all(void);
extern void dump_current_addr(unsigned long addr);

#endif /* __ASM_TLBDEBUG_H */
