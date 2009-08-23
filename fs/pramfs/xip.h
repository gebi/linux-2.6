/*
 * FILE NAME fs/pramfs/xip.h
 *
 * BRIEF DESCRIPTION
 *
 * XIP operations.
 *
 * Author: Marco Stornelli <marco.stornelli@gmail.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#ifdef CONFIG_PRAMFS_XIP

int pram_get_xip_mem(struct address_space *, pgoff_t, int, void **,
							      unsigned long *);

#else

#define pram_get_xip_mem NULL

#endif

