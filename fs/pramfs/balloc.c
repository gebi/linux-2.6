/*
 * FILE NAME fs/pramfs/balloc.c
 *
 * BRIEF MODULE DESCRIPTION
 *
 * The blocks allocation and deallocation routines.
 *
 * Copyright 2009 Marco Stornelli <marco.stornelli@gmail.com>
 * Copyright 2003 Sony Corporation
 * Copyright 2003 Matsushita Electric Industrial Co., Ltd.
 * 2003-2004 (c) MontaVista Software, Inc. , Steve Longerbeam
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/fs.h>
#include <linux/quotaops.h>
#include <linux/bitops.h>
#include "pram.h"

/*
 * This just marks in-use the blocks that make up the bitmap.
 * The bitmap must be writeable before calling.
 */
void pram_init_bitmap(struct super_block *sb)
{
	struct pram_super_block *ps = pram_get_super(sb);
	u64 *bitmap = pram_get_bitmap(sb);
	int blocks = be32_to_cpu(ps->s_bitmap_blocks);

	memset(bitmap, 0, blocks<<sb->s_blocksize_bits);

	while (blocks >= 64) {
		*bitmap++ = (u64)ULLONG_MAX;
		blocks -= 64;
	}

	if (blocks)
		*bitmap = (1<<blocks) - 1;
}


/* Free absolute blocknr */
void pram_free_block(struct super_block *sb, int blocknr)
{
	struct pram_super_block *ps;
	u64 bitmap_block;
	int bitmap_bnr;
	void *bitmap;
	void *bp;

	lock_super(sb);

	bitmap = pram_get_bitmap(sb);
	/*
	 * find the block within the bitmap that contains the inuse bit
	 * for the block we need to free. We need to unlock this bitmap
	 * block to clear the inuse bit.
	 */
	bitmap_bnr = blocknr >> (3 + be32_to_cpu(sb->s_blocksize_bits));
	bitmap_block = pram_get_block_off(sb, bitmap_bnr);
	bp = pram_get_block(sb, bitmap_block);

	pram_lock_block(sb, bp);
	clear_bit(blocknr, bitmap); /* mark the block free */
	pram_unlock_block(sb, bp);

	ps = pram_get_super(sb);
	pram_lock_super(ps);
	if (blocknr < be32_to_cpu(ps->s_free_blocknr_hint))
		ps->s_free_blocknr_hint = cpu_to_be32(blocknr);
	be32_add_cpu(&ps->s_free_blocks_count, 1);
	pram_unlock_super(ps);

	unlock_super(sb);
}


/*
 * allocate a block and return it's absolute blocknr. Zeroes out the
 * block if zero set.
 */
int pram_new_block(struct super_block *sb, int *blocknr, int zero)
{
	struct pram_super_block *ps;
	off_t bitmap_block;
	int bnr, bitmap_bnr, errval;
	void *bitmap;
	void *bp;

	lock_super(sb);
	ps = pram_get_super(sb);
	bitmap = pram_get_bitmap(sb);

	if (be32_to_cpu(ps->s_free_blocks_count)) {
		/* find the oldest unused block */
		bnr = find_next_zero_bit(bitmap,
					 be32_to_cpu(ps->s_blocks_count),
					 be32_to_cpu(ps->s_free_blocknr_hint));

		if (bnr < be32_to_cpu(ps->s_bitmap_blocks) ||
				bnr >= be32_to_cpu(ps->s_blocks_count)) {
			pram_err("no free blocks found!\n");
			errval = -ENOSPC;
			goto fail;
		}

		pram_dbg("allocating blocknr %d\n", bnr);
		pram_lock_super(ps);
		be32_add_cpu(&ps->s_free_blocks_count, -1);
		if (bnr < (be32_to_cpu(ps->s_blocks_count)-1))
			ps->s_free_blocknr_hint = cpu_to_be32(bnr+1);
		else
			ps->s_free_blocknr_hint = 0;
		pram_unlock_super(ps);
	} else {
		pram_err("all blocks allocated\n");
		errval = -ENOSPC;
		goto fail;
	}

	/*
	 * find the block within the bitmap that contains the inuse bit
	 * for the unused block we just found. We need to unlock it to
	 * set the inuse bit.
	 */
	bitmap_bnr = bnr >> (3 + sb->s_blocksize_bits);
	bitmap_block = pram_get_block_off(sb, bitmap_bnr);
	bp = pram_get_block(sb, bitmap_block);

	pram_lock_block(sb, bp);
	set_bit(bnr, bitmap); /* mark the new block in use */
	pram_unlock_block(sb, bp);

	if (zero) {
		bp = pram_get_block(sb, pram_get_block_off(sb, bnr));
		pram_lock_block(sb, bp);
		memset(bp, 0, sb->s_blocksize);
		pram_unlock_block(sb, bp);
	}

	*blocknr = bnr;
	errval = 0;
 fail:
	unlock_super(sb);
	return errval;
}

unsigned long pram_count_free_blocks(struct super_block *sb)
{
	struct pram_super_block *ps = pram_get_super(sb);
	return be32_to_cpu(ps->s_free_blocks_count);
}
