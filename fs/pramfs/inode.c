/*
 * FILE NAME fs/pramfs/inode.c
 *
 * BRIEF DESCRIPTION
 *
 * Inode methods (allocate/free/read/write).
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
#include <linux/smp_lock.h>
#include <linux/sched.h>
#include <linux/highuid.h>
#include <linux/quotaops.h>
#include <linux/module.h>
#include <linux/mpage.h>
#include <linux/backing-dev.h>
#include "pram_fs.h"
#include "xip.h"

static struct backing_dev_info pram_backing_dev_info = {
       .ra_pages       = 0,    /* No readahead */
       .capabilities   = BDI_CAP_NO_ACCT_AND_WRITEBACK,
};

/*
 * allocate a data block for inode and return it's absolute blocknr.
 * Zeroes out the block if zero set. Increments inode->i_blocks.
 */
static int
pram_new_data_block(struct inode *inode, int *blocknr, int zero)
{
       unsigned long flags;

       int errval = pram_new_block(inode->i_sb, blocknr, zero);

       flags = 0;

       if (!errval) {
               struct pram_inode *pi = pram_get_inode(inode->i_sb,
                                                       inode->i_ino);
               inode->i_blocks++;
               pram_lock_inode(pi, flags);
               pi->i_blocks = inode->i_blocks;
               pram_unlock_inode(pi, flags);
       }

       return errval;
}

/*
 * find the offset to the block represented by the given inode's file
 * relative block number.
 */
off_t pram_find_data_block(struct inode *inode, int file_blocknr)
{
       struct super_block *sb = inode->i_sb;
       struct pram_inode *pi;
       off_t *row; /* ptr to row block */
       off_t *col; /* ptr to column blocks */
       off_t bp = 0;
       int i_row, i_col;
       int N = sb->s_blocksize >> 2; /* num block ptrs per block */
       int Nbits = sb->s_blocksize_bits - 2;

       pi = pram_get_inode(sb, inode->i_ino);

       i_row = file_blocknr >> Nbits;
       i_col  = file_blocknr & (N-1);

       row = pram_get_block(sb, pi->i_type.reg.row_block);
       if (row) {
               col = pram_get_block(sb, row[i_row]);
               if (col)
                       bp = col[i_col];
       }

       return bp;
}


/*
 * Free data blocks from inode starting at first_trunc_block.
 */
static void
pram_truncate_blocks(struct inode *inode, int first_trunc_block)
{
       struct super_block *sb = inode->i_sb;
       struct pram_inode *pi = pram_get_inode(sb, inode->i_ino);
       int N = sb->s_blocksize >> 2; /* num block ptrs per block */
       int Nbits = sb->s_blocksize_bits - 2;
       int first_row_index, last_row_index;
       int i, j, first_blocknr, last_blocknr, blocknr;
       unsigned long flags;
       off_t *row; /* ptr to row block */
       off_t *col; /* ptr to column blocks */

       flags = 0;

       if (first_trunc_block >= inode->i_blocks ||
           !inode->i_blocks || !pi->i_type.reg.row_block) {
               return;
       }

       first_blocknr = first_trunc_block;
       last_blocknr = inode->i_blocks - 1;
       first_row_index = first_blocknr >> Nbits;
       last_row_index  = last_blocknr >> Nbits;

       row = pram_get_block(sb, pi->i_type.reg.row_block);

       for (i = first_row_index; i <= last_row_index; i++) {
               int first_col_index = (i == first_row_index) ?
                       first_blocknr & (N-1) : 0;
               int last_col_index = (i == last_row_index) ?
                       last_blocknr & (N-1) : N-1;

               col = pram_get_block(sb, row[i]);
               for (j = first_col_index; j <= last_col_index; j++) {
                       blocknr = pram_get_blocknr(sb, col[j]);
                       pram_free_block(sb, blocknr);
                       pram_lock_block(sb, col, flags);
                       col[j] = 0;
                       pram_unlock_block(sb, col, flags);
               }

               if (first_col_index == 0) {
                       blocknr = pram_get_blocknr(sb, row[i]);
                       pram_free_block(sb, blocknr);
                       pram_lock_block(sb, row, flags);
                       row[i] = 0;
                       pram_unlock_block(sb, row, flags);
               }
       }

       inode->i_blocks -= (last_blocknr - first_blocknr + 1);

       if (first_row_index == 0) {
               blocknr = pram_get_blocknr(sb, pi->i_type.reg.row_block);
               pram_free_block(sb, blocknr);
               pram_lock_inode(pi, flags);
               pi->i_type.reg.row_block = 0;
               pram_unlock_inode(pi, flags);
       }
       pram_lock_inode(pi, flags);
       pi->i_blocks = inode->i_blocks;
       pram_unlock_inode(pi, flags);
}

/*
 * Allocate num data blocks for inode, starting at given file-relative
 * block number. Any unallocated file blocks before file_blocknr
 * are allocated. All blocks except the last are zeroed out.
 */
int pram_alloc_blocks(struct inode *inode, int file_blocknr, int num)
{
       struct super_block *sb = inode->i_sb;
       struct pram_inode *pi = pram_get_inode(sb, inode->i_ino);
       int N = sb->s_blocksize >> 2; /* num block ptrs per block */
       int Nbits = sb->s_blocksize_bits - 2;
       int first_file_blocknr;
       int last_file_blocknr;
       int first_row_index, last_row_index;
       int i, j, blocknr, errval;
       unsigned long flags;
       off_t *row;
       off_t *col;

       flags = 0;

       if (!pi->i_type.reg.row_block) {
               /* alloc the 2nd order array block */
               errval = pram_new_block(sb, &blocknr, 1);
               if (errval) {
                       pram_err("failed to alloc 2nd order array block\n");
                       goto fail;
               }
               pram_lock_inode(pi, flags);
               pi->i_type.reg.row_block = pram_get_block_off(sb, blocknr);
               pram_unlock_inode(pi, flags);
       }

       row = pram_get_block(sb, pi->i_type.reg.row_block);

       first_file_blocknr = (file_blocknr > inode->i_blocks) ?
               inode->i_blocks : file_blocknr;
       last_file_blocknr = file_blocknr + num - 1;

       first_row_index = first_file_blocknr >> Nbits;
       last_row_index  = last_file_blocknr >> Nbits;

       for (i = first_row_index; i <= last_row_index; i++) {
               int first_col_index, last_col_index;
               /*
                * we are starting a new row, so make sure
                * there is a block allocated for the row.
                */
               if (!row[i]) {
                       /* allocate the row block */
                       errval = pram_new_block(sb, &blocknr, 1);
                       if (errval) {
                               pram_err("failed to alloc row block\n");
                               goto fail;
                       }
                       pram_lock_block(sb, row, flags);
                       row[i] = pram_get_block_off(sb, blocknr);
                       pram_unlock_block(sb, row, flags);
               }
               col = pram_get_block(sb, row[i]);

               first_col_index = (i == first_row_index) ?
                       first_file_blocknr & (N-1) : 0;

               last_col_index = (i == last_row_index) ?
                       last_file_blocknr & (N-1) : N-1;

               for (j = first_col_index; j <= last_col_index; j++) {
                       int last_block =
                               (i == last_row_index) && (j == last_col_index);
                       if (!col[j]) {
                               errval = pram_new_data_block(inode,
                                                             &blocknr,
                                                             !last_block);
                               if (errval) {
                                       pram_err("failed to alloc "
                                                 "data block\n");
                                       goto fail;
                               }
                               pram_lock_block(sb, col, flags);
                               col[j] = pram_get_block_off(sb, blocknr);
                               pram_unlock_block(sb, col, flags);
                       }
               }
       }

       errval = 0;
 fail:
       return errval;
}

static int
pram_fill_inode(struct inode *inode, struct pram_inode *pi)
{
       int ret = -EIO;

       if (pram_calc_checksum((u32 *)pi, PRAM_INODE_SIZE>>2)) {
               pram_err("checksum error in inode %08x\n",
                         (u32)inode->i_ino);
               goto bad_inode;
       }

       inode->i_mode = pi->i_mode;
       inode->i_uid = pi->i_uid;
       inode->i_gid = pi->i_gid;
       inode->i_nlink = pi->i_links_count;
       inode->i_size = pi->i_size;
       inode->i_atime.tv_sec = pi->i_atime;
       inode->i_ctime.tv_sec = pi->i_ctime;
       inode->i_mtime.tv_sec = pi->i_mtime;
       inode->i_atime.tv_nsec = inode->i_mtime.tv_nsec =
               inode->i_ctime.tv_nsec = 0;

       /* check if the inode is active. */
       if (inode->i_nlink == 0 && (inode->i_mode == 0 || pi->i_dtime)) {
               /* this inode is deleted */
               ret = -EINVAL;
               goto bad_inode;
       }

       inode->i_blocks = pi->i_blocks;
       inode->i_ino = pram_get_inodenr(inode->i_sb, pi);
       inode->i_mapping->a_ops = &pram_aops;
       inode->i_mapping->backing_dev_info = &pram_backing_dev_info;

       insert_inode_hash(inode);
       switch (inode->i_mode & S_IFMT) {
       case S_IFREG:
               inode->i_op = &pram_file_inode_operations;
               inode->i_fop = &pram_file_operations;
               break;
       case S_IFDIR:
               inode->i_op = &pram_dir_inode_operations;
               inode->i_fop = &pram_dir_operations;
               break;
       case S_IFLNK:
               inode->i_op = &pram_symlink_inode_operations;
               break;
       default:
               inode->i_size = 0;
               init_special_inode(inode, inode->i_mode,
                                  pi->i_type.dev.rdev);
               break;
       }

       return 0;

 bad_inode:
       make_bad_inode(inode);
       return ret;
}

static int pram_update_inode(struct inode *inode)
{
       struct pram_inode *pi;
       int retval = 0;
       unsigned long flags;

       flags = 0;

       pi = pram_get_inode(inode->i_sb, inode->i_ino);

       pram_lock_inode(pi, flags);
       pi->i_mode = inode->i_mode;
       pi->i_uid = inode->i_uid;
       pi->i_gid = inode->i_gid;
       pi->i_links_count = inode->i_nlink;
       pi->i_size = inode->i_size;
       pi->i_blocks = inode->i_blocks;
       pi->i_atime = inode->i_atime.tv_sec;
       pi->i_ctime = inode->i_ctime.tv_sec;
       pi->i_mtime = inode->i_mtime.tv_sec;

       if (S_ISCHR(inode->i_mode) || S_ISBLK(inode->i_mode))
               pi->i_type.dev.rdev = inode->i_rdev;

       pram_unlock_inode(pi, flags);
       return retval;
}

/*
 * NOTE! When we get the inode, we're the only people
 * that have access to it, and as such there are no
 * race conditions we have to worry about. The inode
 * is not on the hash-lists, and it cannot be reached
 * through the filesystem because the directory entry
 * has been deleted earlier.
 */
static void pram_free_inode(struct inode *inode)
{
       struct super_block *sb = inode->i_sb;
       struct pram_super_block *ps;
       struct pram_inode *pi;
       unsigned long inode_nr;
       unsigned long flags;

       flags = 0;

       /*
        * Note: we must free any quota before locking the superblock,
        * as writing the quota to disk may need the lock as well.
        */
       if (!is_bad_inode(inode)) {
               /* Quota is already initialized in iput() */
               vfs_dq_free_inode(inode);
               vfs_dq_drop(inode);
       }

       lock_super(sb);
       clear_inode(inode);

       inode_nr = (inode->i_ino - PRAM_ROOT_INO) >> PRAM_INODE_BITS;

       pi = pram_get_inode(sb, inode->i_ino);
       pram_lock_inode(pi, flags);
       pi->i_dtime = get_seconds();
       pi->i_type.reg.row_block = 0;
       pram_unlock_inode(pi, flags);

       /* increment s_free_inodes_count */
       ps = pram_get_super(sb);
       pram_lock_super(ps, flags);
       if (inode_nr < ps->s_free_inode_hint)
               ps->s_free_inode_hint = inode_nr;
       ps->s_free_inodes_count++;
       if (ps->s_free_inodes_count == ps->s_inodes_count - 1) {
               /* filesystem is empty */
               pram_dbg("fs is empty!\n");
               ps->s_free_inode_hint = 1;
       }
       pram_unlock_super(ps, flags);

       unlock_super(sb);
}


struct inode *
pram_fill_new_inode(struct super_block *sb,
                    struct pram_inode *pi)
{
       struct inode *inode = new_inode(sb);

       if (inode)
               pram_fill_inode(inode, pi);

       return inode;
}


/*
 * Called at the last iput() if i_nlink is zero.
 */
void pram_delete_inode(struct inode *inode)
{
       lock_kernel();

       if (is_bad_inode(inode))
               goto no_delete;

       /* unlink from chain in the inode's directory */
       pram_remove_link(inode);
       inode->i_size = 0;
       if (inode->i_blocks)
               pram_truncate(inode);
       pram_free_inode(inode);

       unlock_kernel();
       return;
 no_delete:
       unlock_kernel();
       clear_inode(inode);  /* We must guarantee clearing of inode... */
}


struct inode *pram_new_inode(const struct inode *dir, int mode)
{
       struct super_block *sb;
       struct pram_super_block *ps;
       struct inode *inode;
       struct pram_inode *pi = NULL;
       int i, errval;
       ino_t ino = 0;
       unsigned long flags;

       flags = 0;

       sb = dir->i_sb;
       inode = new_inode(sb);
       if (!inode)
               return ERR_PTR(-ENOMEM);

       lock_super(sb);
       ps = pram_get_super(sb);

       if (ps->s_free_inodes_count) {
               /* find the oldest unused pram inode */
               for (i = ps->s_free_inode_hint; i < ps->s_inodes_count; i++) {
                       ino = PRAM_ROOT_INO + (i << PRAM_INODE_BITS);
                       pi = pram_get_inode(sb, ino);
                       /* check if the inode is active. */
                       if (pi->i_links_count == 0 && (pi->i_mode == 0 ||
                                                      pi->i_dtime)) {
                               /* this inode is deleted */
                               break;
                       }
               }

               if (i >= ps->s_inodes_count) {
                       pram_err("s_free_inodes_count!=0 but none free!?\n");
                       errval = -ENOSPC;
                       goto fail;
               }

               pram_dbg("allocating inode %lu\n", ino);
               pram_lock_super(ps, flags);
               ps->s_free_inodes_count--;
               ps->s_free_inode_hint = (i < ps->s_inodes_count-1) ? i+1 : 0;
               pram_unlock_super(ps, flags);
       } else {
               pram_err("no space left to create new inode!\n");
               errval = -ENOSPC;
               goto fail;
       }

       /* chosen inode is in ino */

       inode->i_ino = ino;
       inode->i_uid = current_fsuid();

       if (dir->i_mode & S_ISGID) {
               inode->i_gid = dir->i_gid;
               if (S_ISDIR(mode))
                       mode |= S_ISGID;
       } else
               inode->i_gid = current_fsgid();
       inode->i_mode = mode;

       inode->i_blocks = inode->i_size = 0;
       inode->i_mtime = inode->i_atime = inode->i_ctime = CURRENT_TIME;

       pram_lock_inode(pi, flags);
       pi->i_d.d_next = 0;
       pi->i_d.d_prev = 0;
       pram_unlock_inode(pi, flags);

       insert_inode_hash(inode);
       pram_write_inode(inode, 0);

       unlock_super(sb);

       if (vfs_dq_alloc_inode(inode)) {
               vfs_dq_drop(inode);
               inode->i_flags |= S_NOQUOTA;
               inode->i_nlink = 0;
               iput(inode);
               return ERR_PTR(-EDQUOT);
       }
       return inode;

fail:
       unlock_super(sb);
       make_bad_inode(inode);
       iput(inode);
       return ERR_PTR(errval);
}

void pram_truncate(struct inode *inode)
{
       int blocksize, blocksize_bits;
       int blocknr;

       if (!(S_ISREG(inode->i_mode) || S_ISDIR(inode->i_mode) ||
             S_ISLNK(inode->i_mode)))
               return;
       if (IS_APPEND(inode) || IS_IMMUTABLE(inode))
               return;

       blocksize_bits = inode->i_sb->s_blocksize_bits;
       blocksize = 1 << blocksize_bits;
       blocknr = (inode->i_size + blocksize-1) >> blocksize_bits;

       lock_kernel();
       pram_truncate_blocks(inode, blocknr);
       inode->i_mtime = inode->i_ctime = CURRENT_TIME;
       pram_update_inode(inode);
       unlock_kernel();
}

int pram_write_inode(struct inode *inode, int wait)
{
       int ret = 0;

       lock_kernel();
       ret = pram_update_inode(inode);
       unlock_kernel();

       return ret;
}

/*
 * dirty_inode() is called from __mark_inode_dirty()
 */
void pram_dirty_inode(struct inode *inode)
{
       pram_write_inode(inode, 0);
}

int pram_get_and_update_block(struct inode *inode, sector_t iblock,
                                    struct buffer_head *bh, int create)
{
       struct super_block *sb = inode->i_sb;
       unsigned int blocksize = 1 << inode->i_blkbits;
       int err = -EIO;
       off_t block;
       void *bp;
       unsigned long flags;

       flags = 0;

       lock_kernel();

       block = pram_find_data_block(inode, iblock);

       if (!block) {
               if (!create)
                       goto out;

               err = pram_alloc_blocks(inode, iblock, 1);
               if (err)
                       goto out;
               block = pram_find_data_block(inode, iblock);
               if (!block) {
                       err = -EIO;
                       goto out;
               }
               set_buffer_new(bh);
       }

       bh->b_blocknr = block;
       set_buffer_mapped(bh);

       /* now update the buffer synchronously */
       bp = pram_get_block(sb, block);
       if (buffer_new(bh)) {
               pram_lock_block(sb, bp, flags);
               memset(bp, 0, blocksize);
               pram_unlock_block(sb, bp, flags);
               memset(bh->b_data, 0, blocksize);
       } else {
               memcpy(bh->b_data, bp, blocksize);
       }

       set_buffer_uptodate(bh);
       err = 0;

 out:
       unlock_kernel();
       return err;
}

struct address_space_operations pram_aops = {
       .readpage       = pram_readpage,
       .direct_IO      = pram_direct_IO,
       .get_xip_mem    = pram_get_xip_mem,
};
