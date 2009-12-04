#
# Makefile for misc devices that really don't fit anywhere else.
#

ifeq ($(CONFIG_BLK_DEV_LOOP_AES),y)
AES_X86_ASM=n
ifeq ($(CONFIG_X86),y)
ifneq ($(CONFIG_X86_64),y)
  AES_X86_ASM=y
endif
endif
ifeq ($(AES_X86_ASM),y)
  obj-y       += aes-x86.o md5-x86.o crypto-ksym.o
  AFLAGS_aes-x86.o := -DUSE_UNDERLINE=1
else
ifeq ($(CONFIG_X86_64),y)
  obj-y       += aes-amd64.o md5-amd64.o crypto-ksym.o
  AFLAGS_aes-amd64.o := -DUSE_UNDERLINE=1
else
  obj-y       += aes.o md5.o crypto-ksym.o
  CFLAGS_aes.o := -DDATA_ALWAYS_ALIGNED=1
endif
endif
endif

obj-$(CONFIG_IBM_ASM)		+= ibmasm/
obj-$(CONFIG_HDPU_FEATURES)	+= hdpuftrs/
obj-$(CONFIG_AD525X_DPOT)	+= ad525x_dpot.o
obj-$(CONFIG_ATMEL_PWM)		+= atmel_pwm.o
obj-$(CONFIG_ATMEL_SSC)		+= atmel-ssc.o
obj-$(CONFIG_ATMEL_TCLIB)	+= atmel_tclib.o
obj-$(CONFIG_ICS932S401)	+= ics932s401.o
obj-$(CONFIG_LKDTM)		+= lkdtm.o
obj-$(CONFIG_TIFM_CORE)       	+= tifm_core.o
obj-$(CONFIG_TIFM_7XX1)       	+= tifm_7xx1.o
obj-$(CONFIG_PHANTOM)		+= phantom.o
obj-$(CONFIG_SGI_IOC4)		+= ioc4.o
obj-$(CONFIG_ENCLOSURE_SERVICES) += enclosure.o
obj-$(CONFIG_KGDB_TESTS)	+= kgdbts.o
obj-$(CONFIG_SGI_XP)		+= sgi-xp/
obj-$(CONFIG_SGI_GRU)		+= sgi-gru/
obj-$(CONFIG_CS5535_MFGPT)	+= cs5535-mfgpt.o
obj-$(CONFIG_HP_ILO)		+= hpilo.o
obj-$(CONFIG_ISL29003)		+= isl29003.o
obj-$(CONFIG_EP93XX_PWM)	+= ep93xx_pwm.o
obj-$(CONFIG_DS1682)		+= ds1682.o
obj-$(CONFIG_TI_DAC7512)	+= ti_dac7512.o
obj-$(CONFIG_C2PORT)		+= c2port/
obj-$(CONFIG_IWMC3200TOP)      += iwmc3200top/
obj-y				+= eeprom/
obj-y				+= cb710/
