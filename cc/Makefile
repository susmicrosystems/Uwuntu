include $(MAKEDIR)/env.mk

export PREFIX = $(ROOTDIR)/cc/build
export SYSROOT = $(ROOTDIR)/build

ifeq ($(ARCH), i386)
export SYSROOT:= $(SYSROOT)/i386
endif

ifeq ($(ARCH), amd64)
export SYSROOT:= $(SYSROOT)/amd64
endif

ifeq ($(ARCH), arm)
export SYSROOT:= $(SYSROOT)/arm
endif

ifeq ($(ARCH), aarch64)
export SYSROOT:= $(SYSROOT)/aarch64
endif

ifeq ($(ARCH), riscv32)
export SYSROOT:= $(SYSROOT)/riscv32
endif

ifeq ($(ARCH), riscv64)
export SYSROOT:= $(SYSROOT)/riscv64
endif

all: grub gdb

grub: gcc
	@$(MAKE) -C grub

gcc: binutils
	@$(MAKE) -C gcc

binutils:
	@$(MAKE) -C binutils

gdb: gcc
	@$(MAKE) -C gdb

clean:
	@$(MAKE) -C gdb clean
	@$(MAKE) -C grub clean
	@$(MAKE) -C gcc clean
	@$(MAKE) -C binutils clean

.PHONY: gcc binutils grub gdb clean
