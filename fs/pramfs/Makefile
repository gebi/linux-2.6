#
# Makefile for the linux pram-filesystem routines.
#

obj-$(CONFIG_PRAMFS) += pramfs.o
obj-$(CONFIG_TEST_MODULE) += pramfs_test.o

pramfs-y := balloc.o dir.o file.o inode.o namei.o super.o symlink.o

pramfs-$(CONFIG_PRAMFS_WRITE_PROTECT) += wprotect.o
pramfs-$(CONFIG_PRAMFS_XIP) += xip.o
