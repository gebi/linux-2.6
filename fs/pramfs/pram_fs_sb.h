/*
 * FILE NAME include/linux/pram_fs_sb.h
 *
 * Definitions for the PRAM filesystem.
 *
 * Copyright 2009 Marco Stornelli <marco.stornelli@gmail.com>
 * Copyright 2003 Sony Corporation
 * Copyright 2003 Matsushita Electric Industrial Co., Ltd.
 * 2003-2004 (c) MontaVista Software, Inc. , Steve Longerbeam
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#ifndef _LINUX_PRAM_FS_SB
#define _LINUX_PRAM_FS_SB

/*
 * PRAM filesystem super-block data in memory
 */
struct pram_sb_info {
       /*
        * base physical and virtual address of PRAMFS (which is also
        * the pointer to the super block)
        */
       phys_addr_t phys_addr;
       void *virt_addr;

       /* Mount options */
       unsigned long bpi;
       unsigned long num_inodes;
       unsigned long blocksize;
       unsigned long initsize;
       uid_t uid;                  /* Mount uid for root directory */
       gid_t gid;                  /* Mount gid for root directory */
       mode_t mode;                /* Mount mode for root directory */
};

#endif /* _LINUX_PRAM_FS_SB */

