#
# Makefile for the linux pram-filesystem routines.
#

obj-$(CONFIG_PRAMFS) += pramfs.o
obj-$(CONFIG_TEST_MODULE) += pramfs_test.o

pramfs-objs := balloc.o dir.o file.o inode.o namei.o super.o symlink.o

ifneq ($(CONFIG_PRAMFS_NOWP),y)
pramfs-objs += wprotect.o
endif
pramfs-$(CONFIG_PRAMFS_XIP) += xip.o
