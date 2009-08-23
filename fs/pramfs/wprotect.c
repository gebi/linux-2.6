/*
 * FILE NAME fs/pramfs/wprotect.c
 *
 * BRIEF DESCRIPTION
 *
 * Write protection for the filesystem pages.
 *
 * Copyright 2009 Marco Stornelli <marco.stornelli@gmail.com>
 * Copyright 2003 Sony Corporation
 * Copyright 2003 Matsushita Electric Industrial Co., Ltd.
 * 2003-2004 (c) MontaVista Software, Inc. , Steve Longerbeam
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <asm/pgtable.h>
#include <asm/pgalloc.h>
#include <asm/tlbflush.h>
#include "pram.h"

void pram_writeable(void *vaddr, unsigned long size, int rw)
{
	unsigned long addr = (unsigned long)vaddr & PAGE_MASK;
	unsigned long end = (unsigned long)vaddr + size;
	unsigned long start = addr;
	int ret = 0;

	do {
		spinlock_t *ptl;
                pte_t *ptep;

		ret = follow_pte(&init_mm, addr, &ptep, &ptl);
		BUG_ON(ret);

		addr += PAGE_SIZE;
		pte_unmap_unlock(ptep, ptl);
	} while (addr && (addr < end));

	 flush_tlb_kernel_range(start, end);
}
