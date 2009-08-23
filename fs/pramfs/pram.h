/*
 * FILE NAME pram.h
 *
 * BRIEF DESCRIPTION
 *
 * Definitions for the PRAMFS filesystem.
 *
 * Copyright 2009 Marco Stornelli <marco.stornelli@gmail.com>
 * Copyright 2003 Sony Corporation
 * Copyright 2003 Matsushita Electric Industrial Co., Ltd.
 * 2003-2004 (c) MontaVista Software, Inc. , Steve Longerbeam
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */
#ifndef __PRAM_H
#define __PRAM_H

#include <linux/pram_fs.h>
#include <linux/buffer_head.h>
#include <linux/pram_fs_sb.h>
#include <linux/crc16.h>
#include <linux/mutex.h>

/*
 * Debug code
 */
#define pram_dbg(s, args...)	pr_debug("PRAMFS: "s, ## args)
#define pram_err(s, args...)	pr_err("PRAMFS: "s, ## args)
#define pram_warn(s, args...)	pr_warning("PRAMFS: "s, ## args)
#define pram_info(s, args...)	pr_info("PRAMFS: "s, ## args)

/* Function Prototypes */

#ifdef CONFIG_PRAMFS_XIP

#define pram_read	xip_file_read
#define pram_write	xip_file_write
#define pram_mmap	xip_file_mmap
#define pram_aio_read	NULL
#define pram_aio_write	NULL
#define pram_readpage	NULL
#define pram_direct_IO	NULL

#else

#define pram_read	do_sync_read
#define pram_write	do_sync_write
#define pram_mmap	generic_file_mmap
#define pram_aio_read	generic_file_aio_read
#define pram_aio_write	generic_file_aio_write
#define pram_direct_IO	__pram_direct_IO
#define pram_readpage	__pram_readpage

extern int pram_get_and_update_block(struct inode *inode, sector_t iblock,
				     struct buffer_head *bh, int create);

static inline int __pram_readpage(struct file *file, struct page *page)
{
	return block_read_full_page(page, pram_get_and_update_block);
}

/* file.c */
extern ssize_t __pram_direct_IO(int rw, struct kiocb *iocb,
			  const struct iovec *iov,
			  loff_t offset, unsigned long nr_segs);


#endif /* CONFIG_PRAMFS_XIP */

/* balloc.c */
extern void pram_init_bitmap(struct super_block *sb);
extern void pram_free_block(struct super_block *sb, int blocknr);
extern int pram_new_block(struct super_block *sb, int *blocknr, int zero);
extern unsigned long pram_count_free_blocks(struct super_block *sb);

/* dir.c */
extern int pram_add_link(struct dentry *dentry, struct inode *inode);
extern int pram_remove_link(struct inode *inode);

/* inode.c */
extern int pram_alloc_blocks(struct inode *inode, int file_blocknr, int num);
extern off_t pram_find_data_block(struct inode *inode,
					 int file_blocknr);
extern struct inode *pram_fill_new_inode(struct super_block *sb,
					   struct pram_inode *raw_inode);
extern void pram_put_inode(struct inode *inode);
extern void pram_delete_inode(struct inode *inode);
extern struct inode *pram_new_inode(const struct inode *dir, int mode);
extern void pram_read_inode(struct inode *inode);
extern void pram_truncate(struct inode *inode);
extern int pram_write_inode(struct inode *inode, int wait);
extern void pram_dirty_inode(struct inode *inode);
extern int pram_notify_change(struct dentry *dentry, struct iattr *attr);

/* super.c */
#ifdef CONFIG_PRAMFS_TEST
extern struct pram_super_block *get_pram_super(void);
#endif
extern struct super_block *pram_read_super(struct super_block *sb,
					      void *data,
					      int silent);
extern int pram_statfs(struct dentry *d, struct kstatfs *buf);
extern int pram_remount(struct super_block *sb, int *flags, char *data);

/* symlink.c */
extern int pram_block_symlink(struct inode *inode,
			       const char *symname, int len);


#ifdef CONFIG_PRAMFS_WRITE_PROTECT
extern void pram_writeable(void *vaddr, unsigned long size, int rw);

#define wrprotect(addr, size) pram_writeable(addr, size, 0)

#else

#define wrprotect(addr, size) do {} while (0)

#endif /* CONFIG PRAMFS_WRITE_PROTECT */

/* Inline functions start here */

static inline int pram_calc_checksum(u8 *data, int n)
{
	u16 crc = 0;
	crc = crc16(~0, (__u8 *)data + sizeof(__be16), n - sizeof(__be16));
	if (*((__be16 *)data) == cpu_to_be16(crc))
		return 0;
	else
		return 1;
}

/* If this is part of a read-modify-write of the super block,
   pram_lock_super() before calling! */
static inline struct pram_super_block *
pram_get_super(struct super_block *sb)
{
	struct pram_sb_info *sbi = (struct pram_sb_info *)sb->s_fs_info;
	return (struct pram_super_block *)sbi->virt_addr;
}

static inline struct pram_super_block *
pram_get_redund_super(struct super_block *sb)
{
	struct pram_sb_info *sbi = (struct pram_sb_info *)sb->s_fs_info;
	return (struct pram_super_block *)(sbi->virt_addr + PRAM_SB_SIZE);
}

/* pram_lock_super() before calling! */
static inline void pram_sync_super(struct pram_super_block *ps)
{
	u16 crc = 0;
	ps->s_wtime = cpu_to_be32(get_seconds());
	ps->s_sum = 0;
	crc = crc16(~0, (__u8 *)ps + sizeof(__be16), PRAM_SB_SIZE - sizeof(__be16));
	ps->s_sum = cpu_to_be16(crc);
	/* Keep sync redundant super block */
	memcpy((void *)ps + PRAM_SB_SIZE, (void *)ps, PRAM_SB_SIZE);
}

/* pram_lock_inode() before calling! */
static inline void pram_sync_inode(struct pram_inode *pi)
{
	u16 crc = 0;
	pi->i_sum = 0;
	crc = crc16(~0, (__u8 *)pi + sizeof(pi->i_sum), PRAM_INODE_SIZE - sizeof(__be16));
	pi->i_sum = cpu_to_be16(crc);
}

#ifdef CONFIG_PRAMFS_WRITE_PROTECT
#define pram_lock_range(p, len) pram_writeable((p), (len), 1);
#define pram_unlock_range(p, len) pram_writeable((p), (len), 0);
#else
#define pram_lock_range(p, len) do {} while (0)
#define pram_unlock_range(p, len) do {} while (0)
#endif

/* write protection for super block */
#define pram_lock_super(ps) \
	pram_lock_range((ps), PRAM_SB_SIZE)
#define pram_unlock_super(ps) {\
	pram_sync_super(ps);\
	pram_unlock_range((ps), PRAM_SB_SIZE);\
}

/* write protection for inode metadata */
#define pram_lock_inode(pi) \
	pram_lock_range((pi), PRAM_INODE_SIZE)
#define pram_unlock_inode(pi) {\
	pram_sync_inode(pi);\
	pram_unlock_range((pi), PRAM_INODE_SIZE);\
}

/* write protection for a data block */
#define pram_lock_block(sb, bp) \
	pram_lock_range((bp), (sb)->s_blocksize)
#define pram_unlock_block(sb, bp) \
	pram_unlock_range((bp), (sb)->s_blocksize)

static inline void *
pram_get_bitmap(struct super_block *sb)
{
	struct pram_super_block *ps = pram_get_super(sb);
	return (void *)ps + be64_to_cpu(ps->s_bitmap_start);
}

/* If this is part of a read-modify-write of the inode metadata,
   pram_lock_inode() before calling! */
static inline struct pram_inode *
pram_get_inode(struct super_block *sb, u64 ino)
{
	struct pram_super_block *ps = pram_get_super(sb);
	return ino ? (struct pram_inode *)((void *)ps + ino) : NULL;
}

static inline ino_t
pram_get_inodenr(struct super_block *sb, struct pram_inode *pi)
{
	struct pram_super_block *ps = pram_get_super(sb);
	return (ino_t)((unsigned long)pi - (unsigned long)ps);
}

static inline u64
pram_get_block_off(struct super_block *sb, unsigned long blocknr)
{
	struct pram_super_block *ps = pram_get_super(sb);
	return (u64)(be64_to_cpu(ps->s_bitmap_start) +
			     (blocknr << sb->s_blocksize_bits));
}

static inline unsigned long
pram_get_blocknr(struct super_block *sb, u64 block)
{
	struct pram_super_block *ps = pram_get_super(sb);
	return (block - be64_to_cpu(ps->s_bitmap_start)) >> sb->s_blocksize_bits;
}

/* If this is part of a read-modify-write of the block,
   pram_lock_block() before calling! */
static inline void *
pram_get_block(struct super_block *sb, u64 block)
{
	struct pram_super_block *ps = pram_get_super(sb);
	return block ? ((void *)ps + block) : NULL;
}


/*
 * Inodes and files operations
 */

/* dir.c */
extern struct file_operations pram_dir_operations;

/* file.c */
extern struct inode_operations pram_file_inode_operations;
extern struct file_operations pram_file_operations;

/* inode.c */
extern struct address_space_operations pram_aops;

/* namei.c */
extern struct inode_operations pram_dir_inode_operations;

/* symlink.c */
extern struct inode_operations pram_symlink_inode_operations;

extern struct mutex pram_lock;

#endif	/* __PRAM_H */
