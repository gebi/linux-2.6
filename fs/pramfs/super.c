/*
 * FILE NAME fs/pramfs/super.c
 *
 * BRIEF DESCRIPTION
 *
 * Super block operations.
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
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/blkdev.h>
#include <linux/parser.h>
#include <linux/vfs.h>
#include <linux/uaccess.h>
#include <linux/io.h>
#include <linux/seq_file.h>
#include <linux/mount.h>
#include <linux/mm.h>
#include <linux/ctype.h>
#include <linux/bitops.h>
#include <linux/magic.h>
#include "pram.h"

static struct super_operations pram_sops;

DEFINE_MUTEX(pram_lock);

#ifdef CONFIG_PRAMFS_TEST
static void *first_pram_super;

struct pram_super_block *get_pram_super(void)
{
	return (struct pram_super_block *)first_pram_super;
}
EXPORT_SYMBOL(get_pram_super);
#endif

static void pram_set_blocksize(struct super_block *sb, unsigned long size)
{
	int bits;

	/*
	* We've already validated the user input and the value here must be
	* between PRAM_MAX_BLOCK_SIZE and PRAM_MIN_BLOCK_SIZE
	* and it must be a power of 2.
	*/
	bits = fls(size) - 1;
	sb->s_blocksize_bits = bits;
	sb->s_blocksize = (1<<bits);
}

static inline void *pram_ioremap(phys_addr_t phys_addr, size_t size)
{
	void *retval;

	/*
	 * NOTE: Userland may not map this resource, we will mark the region so
	 * /dev/mem and the sysfs MMIO access will not be allowed. This
	 * restriction depends on STRICT_DEVMEM option. If this option is
	 * disabled or not available we mark the region only as busy.
	 */
	retval = request_mem_region_exclusive(phys_addr, size, "pramfs");
	if (!retval)
		goto fail;

	retval = ioremap_nocache(phys_addr, size);

	if (retval)
		wrprotect(retval, size);
fail:
	return retval;
}

enum {
	Opt_addr, Opt_bpi, Opt_size,
	Opt_num_inodes, Opt_mode, Opt_uid,
	Opt_gid, Opt_blocksize, Opt_err
};

static const match_table_t tokens = {
	{Opt_bpi,	"physaddr=%x"},
	{Opt_bpi,	"bpi=%u"},
	{Opt_size,	"init=%s"},
	{Opt_num_inodes, "N=%u"},
	{Opt_mode,	"mode=%o"},
	{Opt_uid,	"uid=%u"},
	{Opt_gid,	"gid=%u"},
	{Opt_blocksize,	"bs=%s"},
	{Opt_err,	NULL},
};

static phys_addr_t get_phys_addr(void **data)
{
	phys_addr_t phys_addr;
	char *options = (char *) *data;

	if (!options || strncmp(options, "physaddr=", 9) != 0)
		return (phys_addr_t)ULLONG_MAX;
	options += 9;
	phys_addr = (phys_addr_t)simple_strtoull(options, &options, 0);
	if (*options && *options != ',') {
		pram_err("Invalid phys addr specification: %s\n",
		       (char *) *data);
		return (phys_addr_t)ULLONG_MAX;
	}
	if (phys_addr & (PAGE_SIZE - 1)) {
		pram_err("physical address 0x%16llx for pramfs isn't "
			  "aligned to a page boundary\n",
			  (u64)phys_addr);
		return (phys_addr_t)ULLONG_MAX;
	}
	if (*options == ',')
		options++;
	*data = (void *) options;
	return phys_addr;
}

static int pram_parse_options(char *options, struct pram_sb_info *sbi)
{
	char *p, *rest;
	substring_t args[MAX_OPT_ARGS];
	int option;

	if (!options)
		return 0;

	while ((p = strsep(&options, ",")) != NULL) {
		int token;
		if (!*p)
			continue;

		token = match_token(p, tokens, args);
		switch (token) {
			case Opt_addr: {
				/* physaddr managed in get_phys_addr() */
				break;
			}
			case Opt_bpi: {
				if (match_int(&args[0], &option))
					goto bad_val;
				sbi->bpi = option;
				break;
			}
			case Opt_uid: {
				if (match_int(&args[0], &option))
					goto bad_val;
				sbi->uid = option;
				break;
			}
			case Opt_gid: {
				if (match_int(&args[0], &option))
					goto bad_val;
				sbi->gid = option;
				break;
			}
			case Opt_mode: {
				if (match_octal(&args[0], &option))
					goto bad_val;
				sbi->mode = option & 01777U;
				break;
			}
			case Opt_size: {
				/* memparse() will accept a K/M/G without a digit */
				if (!isdigit(*args[0].from))
					goto bad_val;
				sbi->initsize = memparse(args[0].from, &rest);
				break;
			}
			case Opt_num_inodes: {
				if (match_int(&args[0], &option))
					goto bad_val;
				sbi->num_inodes = option;
				break;
			}
			case Opt_blocksize: {
				/* memparse() will accept a K/M/G without a digit */
				if (!isdigit(*args[0].from))
					goto bad_val;
				sbi->blocksize = memparse(args[0].from, &rest);
				if (sbi->blocksize < PRAM_MIN_BLOCK_SIZE ||
					sbi->blocksize > PRAM_MAX_BLOCK_SIZE ||
					!is_power_of_2(sbi->blocksize))
					goto bad_val;
				break;
			}
			default: {
				pram_err("Bad mount option: \"%s\"\n", p);
				return -EINVAL;
				break;
			}
		}
	}

	return 0;

bad_val:
	pram_err("Bad value '%s' for mount option '%s'\n", args[0].from, p);
	return -EINVAL;
}

static struct pram_inode *pram_init(struct super_block *sb, unsigned long size)
{
	unsigned long bpi, num_inodes, bitmap_size, blocksize, num_blocks;
	u64 bitmap_start;
	struct pram_inode *root_i;
	struct pram_super_block *super;
	struct pram_sb_info *sbi = (struct pram_sb_info *)sb->s_fs_info;

	pram_info("creating an empty pramfs of size %lu\n", size);
#ifdef CONFIG_PRAMFS_XIP
	pram_info("pramfs with xip enabled\n");
#endif

	sbi->virt_addr = pram_ioremap(sbi->phys_addr, size);
	if (!sbi->virt_addr) {
		pram_err("ioremap of the pramfs image failed\n");
		return ERR_PTR(-EINVAL);
	}

#ifdef CONFIG_PRAMFS_TEST
	if (!first_pram_super)
		first_pram_super = sbi->virt_addr;
#endif

	if (!sbi->blocksize)
		blocksize = PRAM_DEF_BLOCK_SIZE;
	else
		blocksize = sbi->blocksize;

	pram_set_blocksize(sb, blocksize);
	blocksize = sb->s_blocksize;

	if (sbi->blocksize && sbi->blocksize != blocksize)
		sbi->blocksize = blocksize;

	if (size < blocksize) {
		pram_err("size smaller then block size\n");
		return ERR_PTR(-EINVAL);
	}

	if (!sbi->bpi)
		/* default is that 5% of the filesystem is
		   devoted to the inode table */
		bpi = 20 * PRAM_INODE_SIZE;
	else
		bpi = sbi->bpi;

	if (!sbi->num_inodes)
		num_inodes = size / bpi;
	else
		num_inodes = sbi->num_inodes;

	/* up num_inodes such that the end of the inode table
	   (and start of bitmap) is on a block boundary */
	bitmap_start = (PRAM_SB_SIZE*2) + (num_inodes<<PRAM_INODE_BITS);
	if (bitmap_start & (blocksize - 1))
		bitmap_start = (bitmap_start + blocksize) &
			~(blocksize-1);
	num_inodes = (bitmap_start - (PRAM_SB_SIZE*2)) >> PRAM_INODE_BITS;

	if (sbi->num_inodes && num_inodes != sbi->num_inodes)
		sbi->num_inodes = num_inodes;

	num_blocks = (size - bitmap_start) >> sb->s_blocksize_bits;

	if (!num_blocks) {
		pram_err("num blocks equals to zero\n");
		return ERR_PTR(-EINVAL);
	}

	/* calc the data blocks in-use bitmap size in bytes */
	if (num_blocks & 7)
		bitmap_size = ((num_blocks + 8) & ~7) >> 3;
	else
		bitmap_size = num_blocks >> 3;
	/* round it up to the nearest blocksize boundary */
	if (bitmap_size & (blocksize - 1))
		bitmap_size = (bitmap_size + blocksize) & ~(blocksize-1);

	pram_info("blocksize %lu, num inodes %lu, num blocks %lu\n",
		  blocksize, num_inodes, num_blocks);
	pram_dbg("bitmap start 0x%08x, bitmap size %lu\n",
		 (unsigned int)bitmap_start, bitmap_size);
	pram_dbg("max name length %d\n", PRAM_NAME_LEN);

	super = pram_get_super(sb);
	pram_lock_range(super, bitmap_start + bitmap_size);

	/* clear out super-block and inode table */
	memset(super, 0, bitmap_start);
	super->s_size = cpu_to_be32(size);
	super->s_blocksize = cpu_to_be32(blocksize);
	super->s_inodes_count = cpu_to_be32(num_inodes);
	super->s_blocks_count = cpu_to_be32(num_blocks);
	super->s_free_inodes_count = cpu_to_be32(num_inodes - 1);
	super->s_bitmap_blocks = cpu_to_be32(bitmap_size >> sb->s_blocksize_bits);
	super->s_free_blocks_count = cpu_to_be32(num_blocks - super->s_bitmap_blocks);
	super->s_free_inode_hint = cpu_to_be32(1);
	super->s_bitmap_start = cpu_to_be64(bitmap_start);
	super->s_magic = cpu_to_be16(PRAM_SUPER_MAGIC);
	pram_sync_super(super);

	root_i = pram_get_inode(sb, PRAM_ROOT_INO);

	root_i->i_mode = cpu_to_be32(sbi->mode);
	root_i->i_mode = cpu_to_be16(root_i->i_mode | S_IFDIR);
	root_i->i_uid = cpu_to_be32(sbi->uid);
	root_i->i_gid = cpu_to_be32(sbi->gid);
	root_i->i_links_count = cpu_to_be16(2);
	root_i->i_d.d_parent = cpu_to_be64(PRAM_ROOT_INO);
	pram_sync_inode(root_i);

	pram_init_bitmap(sb);

	pram_unlock_range(super, bitmap_start + bitmap_size);

	return root_i;
}

static int pram_fill_super(struct super_block *sb, void *data, int silent)
{
	struct pram_super_block *super, *super_redund;
	struct pram_inode *root_i;
	struct pram_sb_info *sbi = NULL;
	u64 root_offset;
	unsigned long blocksize, initsize = 0;
	int retval = -EINVAL;

	sbi = kzalloc(sizeof(struct pram_sb_info), GFP_KERNEL);
	if (!sbi)
		return -ENOMEM;
	sb->s_fs_info = sbi;

	sbi->phys_addr = get_phys_addr(&data);
	if (sbi->phys_addr == (phys_addr_t)ULLONG_MAX)
		goto out;

	/* Init with default values */
	sbi->mode = (S_IRWXUGO | S_ISVTX);
	sbi->uid = current_fsuid();
	sbi->gid = current_fsgid();

	if (pram_parse_options(data, sbi))
		goto out;

	initsize = sbi->initsize;

	/* Init a new pramfs instance */
	if (initsize) {
		root_i = pram_init(sb, initsize);

		if (IS_ERR(root_i))
			goto out;

		super = pram_get_super(sb);

		goto setup_sb;
	}

	pram_dbg("checking physical address 0x%016llx for pramfs image\n",
		   (u64)sbi->phys_addr);

	/* Map only one page for now. Will remap it when fs size is known. */
	initsize = PAGE_SIZE;
	sbi->virt_addr = pram_ioremap(sbi->phys_addr, initsize);
	if (!sbi->virt_addr) {
		pram_err("ioremap of the pramfs image failed\n");
		goto out;
	}

	super = pram_get_super(sb);
	super_redund = pram_get_redund_super(sb);

	/* Do sanity checks on the superblock */
	if (be32_to_cpu(super->s_magic) != PRAM_SUPER_MAGIC) {
		if (be32_to_cpu(super_redund->s_magic) != PRAM_SUPER_MAGIC) {
			if (!silent)
				pram_err("Can't find a valid pramfs "
								"partition\n");
			goto out;
		} else {
			pram_warn("Error in super block: try to repair it with "
							  "the redundant copy");
			/* Try to auto-recover the super block */
			memcpy(super, super_redund, PRAM_SB_SIZE);
		}
	}

	/* Read the superblock */
	if (pram_calc_checksum((u8 *)super, PRAM_SB_SIZE)) {
		if (pram_calc_checksum((u8 *)super_redund, PRAM_SB_SIZE)) {
			pram_err("checksum error in super block\n");
			goto out;
		} else {
			pram_warn("Error in super block: try to repair it with "
							  "the redundant copy");
			/* Try to auto-recover the super block */
			memcpy(super, super_redund, PRAM_SB_SIZE);
		}
	}

	blocksize = super->s_blocksize;
	pram_set_blocksize(sb, blocksize);

	initsize = super->s_size;
	pram_info("pramfs image appears to be %lu KB in size\n", initsize>>10);
	pram_info("blocksize %lu\n", blocksize);

	/* Read the root inode */
	root_i = pram_get_inode(sb, PRAM_ROOT_INO);

	/* Check that the root inode is in a sane state */
	if (pram_calc_checksum((u8 *)root_i, PRAM_INODE_SIZE)) {
		pram_err("checksum error in root inode!\n");
		goto out;
	}

	if (be64_to_cpu(root_i->i_d.d_next)) {
		pram_err("root->next not NULL??!!\n");
		goto out;
	}

	if (!S_ISDIR(be16_to_cpu(root_i->i_mode))) {
		pram_err("root is not a directory!\n");
		goto out;
	}

	root_offset = be64_to_cpu(root_i->i_type.dir.head);
	if (root_offset == 0)
		pram_dbg("empty filesystem\n");

	/* Remap the whole filesystem now */
	iounmap(sbi->virt_addr);
	release_mem_region(sbi->phys_addr, PAGE_SIZE);
	sbi->virt_addr = pram_ioremap(sbi->phys_addr, initsize);
	if (!sbi->virt_addr) {
		pram_err("ioremap of the pramfs image failed\n");
		goto out;
	}
	super = pram_get_super(sb);
	root_i = pram_get_inode(sb, PRAM_ROOT_INO);

#ifdef CONFIG_PRAMFS_TEST
	if (!first_pram_super)
		first_pram_super = sbi->virt_addr;
#endif

	/* Set it all up.. */
 setup_sb:
	sb->s_magic = be16_to_cpu(super->s_magic);
	sb->s_op = &pram_sops;
	sb->s_root = d_alloc_root(pram_fill_new_inode(sb, root_i));

	retval = 0;
 out:
	if (retval && sbi->virt_addr) {
		iounmap(sbi->virt_addr);
		release_mem_region(sbi->phys_addr, initsize);
	}

	return retval;
}

int pram_statfs(struct dentry *d, struct kstatfs *buf)
{
	struct super_block *sb = d->d_sb;
	struct pram_super_block *ps = pram_get_super(sb);

	buf->f_type = PRAM_SUPER_MAGIC;
	buf->f_bsize = sb->s_blocksize;
	buf->f_blocks = be32_to_cpu(ps->s_blocks_count);
	buf->f_bfree = buf->f_bavail = be32_to_cpu(ps->s_free_blocks_count);
	buf->f_files = be32_to_cpu(ps->s_inodes_count);
	buf->f_ffree = be32_to_cpu(ps->s_free_inodes_count);
	buf->f_namelen = PRAM_NAME_LEN;
	return 0;
}

static int pram_show_options(struct seq_file *seq, struct vfsmount *vfs)
{
	struct pram_sb_info *sbi = (struct pram_sb_info *)vfs->mnt_sb->s_fs_info;

	seq_printf(seq, ",physaddr=0x%016llx", (u64)sbi->phys_addr);
	if (sbi->initsize)
		seq_printf(seq, ",init=%luk", sbi->initsize >> 10);
	if (sbi->blocksize)
		seq_printf(seq, ",bs=%lu", sbi->blocksize);
	if (sbi->bpi)
		seq_printf(seq, ",bpi=%lu", sbi->bpi);
	if (sbi->num_inodes)
		seq_printf(seq, ",N=%lu", sbi->num_inodes);
	if (sbi->mode != (S_IRWXUGO | S_ISVTX))
		seq_printf(seq, ",mode=%03o", sbi->mode);
	if (sbi->uid != 0)
		seq_printf(seq, ",uid=%u", sbi->uid);
	if (sbi->gid != 0)
		seq_printf(seq, ",gid=%u", sbi->gid);

	return 0;
}

int pram_remount(struct super_block *sb, int *mntflags, char *data)
{
	struct pram_super_block *ps;

	if ((*mntflags & MS_RDONLY) != (sb->s_flags & MS_RDONLY)) {
		ps = pram_get_super(sb);
		pram_lock_super(ps);
		ps->s_mtime = cpu_to_be32(get_seconds()); /* update mount time */
		pram_unlock_super(ps);
	}

	return 0;
}

void pram_put_super(struct super_block *sb)
{
	struct pram_sb_info *sbi = (struct pram_sb_info *)sb->s_fs_info;
	struct pram_super_block *ps = pram_get_super(sb);

#ifdef CONFIG_PRAMFS_TEST
	if (first_pram_super == sbi->virt_addr)
		first_pram_super = NULL;
#endif

	/* It's unmount time, so unmap the pramfs memory */
	if (sbi->virt_addr) {
		iounmap(sbi->virt_addr);
		sbi->virt_addr = NULL;
		release_mem_region(sbi->phys_addr, ps->s_size);
	}

	sb->s_fs_info = NULL;
	kfree(sbi);
}

/*
 * the super block writes are all done "on the fly", so the
 * super block is never in a "dirty" state, so there's no need
 * for write_super.
 */
static struct super_operations pram_sops = {
	.write_inode	= pram_write_inode,
	.dirty_inode	= pram_dirty_inode,
	.delete_inode	= pram_delete_inode,
	.put_super	= pram_put_super,
	.statfs		= pram_statfs,
	.remount_fs	= pram_remount,
	.show_options	= pram_show_options,
};

static int pram_get_sb(struct file_system_type *fs_type,
	int flags, const char *dev_name, void *data, struct vfsmount *mnt)
{
	return get_sb_nodev(fs_type, flags, data, pram_fill_super, mnt);
}

static struct file_system_type pram_fs_type = {
	.owner          = THIS_MODULE,
	.name           = "pramfs",
	.get_sb         = pram_get_sb,
	.kill_sb        = kill_anon_super,
};

static int __init init_pram_fs(void)
{
	return register_filesystem(&pram_fs_type);
}

static void __exit exit_pram_fs(void)
{
	unregister_filesystem(&pram_fs_type);
}

MODULE_AUTHOR("Marco Stornelli <marco.stornelli@gmail.com>");
MODULE_DESCRIPTION("Protected/Persistent RAM Filesystem");
MODULE_LICENSE("GPL");

module_init(init_pram_fs)
module_exit(exit_pram_fs)
