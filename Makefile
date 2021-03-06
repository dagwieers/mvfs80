#
# Copyright (C) 2003, 2013 IBM Corporation.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2, or (at your option)
# any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301, USA
#
# Author: IBM Corporation
# This module is part of the IBM (R) Rational (R) ClearCase (R)
# Multi-version file system (MVFS).
# For support, please visit http://www.ibm.com/software/support
#

# Set LINUX_KERNEL_DIR to point at the build tree for your kernel
# (commonly available via a symlink from /lib/modules/`uname -r`/build,
# or you can override it in mvfs_param.mk.config)
#
# If you wish to build for a system other than the one you are currently
# running, provide proper definitions for the following variables in
# mvfs_param.mk.config:
# LINUX_KERNEL_DIR= path to the kernel build tree
# INSTALL_DIR= path to the directory to install the mvfs module for insmod.
# If you are building for a 2.6 kernel with standard source and installation
# directories, you may just define RELEASE to match the target system.
# The command "man modules.conf" will provide information about the 
# default search rules used by modutils.
# As needed you may also redefine RELEASE and MACH.

RELEASE := $(shell uname -r)
MACH := $(shell uname -m)
ARCH=$(shell echo $(MACH) |sed -e s:i.86:i386:)
LINUX_KERNEL_DIR=/lib/modules/$(RELEASE)/build
INSTALL_DIR=/lib/modules/$(RELEASE)/kernel/fs/mvfs
SRCDIR=.
MVFSSRCDIR=.

# The following is supposed to detect kernel ABI changes, at least on Red Hat.
KABI=$(shell rpm -q --provides kernel-$(RELEASE) | grep kABI )

MVFS_DEBUG_FLAGS=
NM=nm
DEPMOD=/sbin/depmod
ifdef RATLHOME
RATL_HOME=${RATLHOME}
else
RATL_HOME=/opt/rational
endif
ifdef CLEARCASE_COMMON
CC_COMMON=${CLEARCASE_COMMON}
else
CC_COMMON=${RATL_HOME}/common
endif
ifdef CLEARCASEHOME
CCHOME=${CLEARCASEHOME}
else
CCHOME=${RATL_HOME}/clearcase
endif

CCINSTALL=${CC_COMMON}/install
JAVA=$(CC_COMMON)/java/jre/bin/java
INSTALL_BASE=com.ibm.rational.team.install.cc
JARFILE=$(INSTALL_BASE).jar
PARAM_METHOD=$(INSTALL_BASE).mvfs.CreateMvfsParams
CLASSPATH=$(CCINSTALL)/$(JARFILE)

ifndef KBUILD_EXTMOD
# The Linux kernel Makefile framework (which sets the KBUILD_EXTMOD variable)
# sets the "obj" variable).
# We execute this Makefile before invoking the kernel Makefile framework
# so we need to define this here.
obj=.
endif

# All values that can be redefined in mvfs_param.mk.config must be defined
# before this point.
# Only include mvfs_param.mk.config if it exists
# This eliminates bogus attemtps to rebuild it on make clean

exists := $(shell cat  $(obj)/mvfs_param.mk.config)
ifneq ($(strip $(exists)),)
include $(obj)/mvfs_param.mk.config
endif

KINC=$(LINUX_KERNEL_DIR)/include
ADAPTER_OBJECTS= \
	mvfs_param.o \
	mvfs_linux_asops.o \
	mvfs_linux_builtins.o \
	mvfs_linux_dops.o \
	mvfs_linux_fops.o \
	mvfs_linux_glue.o \
	mvfs_linux_iops.o \
	mvfs_linux_mvops.o \
	mvfs_linux_rpcglue.o \
	mvfs_linux_shadow.o \
	mvfs_linux_sops.o \
	mvfs_linux_utils.o \
	mvfs_linux_mdki.o

MVFS_OBJECTS= \
	mvfs_vfsops.o \
	mvfs_vnodeops.o \
	mvfs_clearops.o \
	mvfs_vwdirops.o \
	mvfs_procops.o \
	mvfs_utils.o \
	mvfs_mnode.o \
	mvfs_debug.o \
	mvfs_rdc.o \
	mvfs_auditops.o \
	mvfs_dncops.o \
	mvfs_clnt.o \
	mvfs_ntvwops.o \
	mvfs_rpcutl.o \
	mvfs_mioctl.o \
	mvfs_kmem.o \
	mvfs_copy.o \
	mvfs_transtype.o \
	mvfs_acl.o \
	xdr_tbs_kernel.o \
	xdr_view_kernel.o \
	tbs_acl_c_xdr_kernel.o \
	tbs_acl_kernel.o \
	tbs_errno.o \
	xdr_albd_kernel.o \
	xdr_ks_kernel.o \
	credutl_kernel.o \
	mvfs_mdep_linux.o \
	mvfs_sohash_table.o \
	mvfs_tunables.o

MVFS_ARCH_64BIT=-DATRIA_LP64 -DRATL_COMPAT32
MVFS_ARCH_DEF.i386=-DATRIA_LINUX_IA32
# only 64 bits is supported
MVFS_ARCH_DEF.s390=-DATRIA_LINUX_390 $(MVFS_ARCH_64BIT)
MVFS_ARCH_DEF.ppc64=-DATRIA_LINUX_PPC $(MVFS_ARCH_64BIT)
MVFS_ARCH_DEF.s390x=$(MVFS_ARCH_DEF.s390)
MVFS_ARCH_DEF.x86_64=$(MVFS_ARCH_DEF.i386) $(MVFS_ARCH_64BIT)
MVFS_ARCH_DEF=$(MVFS_ARCH_DEF.$(ARCH))

MVFS_DEFS= \
	-DATRIA_ANSI_C \
	-DATRIA_LARGEFILE64_SOURCE \
	-DATRIA_LINUX \
	$(MVFS_ARCH_DEF) \
	-DATRIA_UNIX \
	-DI18N_MSGS \
	-DLINUX \
	-DXREV_CLIENTS_SUPPORT_V8_SERVERS \
	-DXREV_CLIENTS_SUPPORT_V7_SERVERS \
	-D_LARGEFILE64_SOURCE

ifneq (,$(findstring s390,$(ARCH)))
#SLES11 SP3 (3.0.79) kernel has kernel headers that generate a warning
#but only on the S390.
#This is fixed in the 3.0.82 kernel
UPDATE_VER=$(shell echo $(RELEASE) |cut -d - -f 1 | cut -d . -f 1-3)
ifeq (3.0,$(shell echo $(UPDATE_VER) | cut -d . -f 1-2))
BUILD_NUM=$(shell echo $(UPDATE_VER) | cut -d . -f 3)
ifeq (1,$(shell test $(BUILD_NUM) -ge 76 && test $(BUILD_NUM) -lt 82 && echo "1"))
WARNING_FLAGS = -w
endif #ifeq(1,$(shell...
endif #ifeq(3.0...
endif #ifneq (,$(findstring...
ifeq ($(WARNING_FLAGS),)
#Use defaults if we didn't set it above.
WARNING_FLAGS = -Wall -Wstrict-prototypes -Wunused -Werror
endif

all: mvfs_param.mk.config
	$(MAKE) -C $(LINUX_KERNEL_DIR) SUBDIRS=`pwd`

# try to build a reasonable RATL_EXTRAFLAGS if it is set to empty (but not undefined)
ifeq (,$(if $(findstring undefined,$(origin RATL_EXTRAFLAGS)),undefined,$(RATL_EXTRAFLAGS)))
# but only for x86
ifneq (,$(if $(findstring x86_64,$(ARCH)),x86_64,$(findstring i386,$(ARCH))))
# and only for RH with kernel 2.6.33
__KBASE=$(shell uname -r | grep -o '2\.6\.33')
__DIST_R=$(shell if [ -f /etc/redhat-release ] ; then sed -ne 's/^Red Hat.*release \([56]\).*/\1/p' /etc/redhat-release ; fi)
ifneq (,$(if $(__KBASE),$(__DIST_R)))
RATL_EXTRAFLAGS:=-DRATL_REDHAT -DRATL_VENDOR_VER=$(__DIST_R)00 -DRATL_EXTRA_VER=0
ifeq ($(ARCH),x86_64)
RATL_EXTRAFLAGS+=-DRATL_COMPAT32
endif
endif
endif
endif

# To use the new split ordered hash table (on x86 only),
# uncomment the next line
#RATL_EXTRAFLAGS += -DMVFS_USE_SPLIT_ORDERED_HASH=1

OPT_SPACE= -Os

ifeq ($(ARCH), x86_64)
# don't optimize for space in this file--GCC will generate a function
# call to memcmp which is unavailable in some kernel environments.
$(obj)/mvfs_linux_builtins.o : OPT_SPACE=
endif

# On SLES11 s390x the compiler uses the warn-framesize value for a >= check
# rather than the > check the GCC documentation would imply.  Passing in
# this argument will override the kernel build environment -mwarn-framesize=256
# and prevent our -Werror option from generating an error.  The fno-tree-ter
# option is used to deactivate the temporary expression replacement to keep
# the frame size under limit.
ifneq (,$(findstring s390,$(ARCH)))
__SLES11_390:=$(CC) $(RATL_EXTRAFLAGS)
ifneq (,$(if $(findstring RATL_SUSE,$(__SLES11_390)),$(findstring RATL_VENDOR_VER=11,$(__SLES11_390))))
RATL_EXTRAFLAGS += -mwarn-framesize=257 -fno-tree-ter
endif
endif

VNODE_CONSTRUCTED_OBJS=timestamp.o
VNODE_BUILT_OBJECTS=$(addprefix $(obj)/,$(ADAPTER_OBJECTS))
VNODE_GEN_OBJECTS=$(addprefix $(obj)/,$(VNODE_CONSTRUCTED_OBJS))
MVFS_EXTRA_CFLAGS += $(WARNING_FLAGS) $(RATL_EXTRAFLAGS) -I$(obj) -D_KERNEL $(OPT_SPACE) $(MVFS_MOD_FLAGS)
$(VNODE_BUILT_OBJECTS) $(VNODE_GEN_OBJECTS) : MVFS_MOD_FLAGS= -Wunused
$(VNODE_BUILT_OBJECTS) $(VNODE_GEN_OBJECTS) : ccflags-y+=$(MVFS_EXTRA_CFLAGS)
$(VNODE_BUILT_OBJECTS) $(VNODE_GEN_OBJECTS) : EXTRA_CFLAGS:=$(EXTRA_CFLAGS) $(MVFS_EXTRA_CFLAGS)


MVFS_BUILT_OBJECTS=$(addprefix $(obj)/,$(MVFS_OBJECTS))
$(MVFS_BUILT_OBJECTS) : MVFS_MOD_FLAGS=${MVFS_DEFS} -Wno-unused
$(MVFS_BUILT_OBJECTS) : ccflags-y+=$(WARNING_FLAGS) $(RATL_EXTRAFLAGS) -I$(obj) -D_KERNEL $(OPT_SPACE) $(MVFS_MOD_FLAGS)
$(MVFS_BUILT_OBJECTS) : EXTRA_CFLAGS:=$(EXTRA_CFLAGS) $(WARNING_FLAGS) $(RATL_EXTRAFLAGS) -I$(obj) -D_KERNEL $(OPT_SPACE) $(MVFS_MOD_FLAGS)

obj-${CONFIG_MVFS} += mvfs.o
mvfs-objs := $(MVFS_OBJECTS) $(ADAPTER_OBJECTS) $(VNODE_CONSTRUCTED_OBJS)
$(obj)/mvfs_tunables.o: $(obj)/mvfs_when.h

$(obj)/mvfs_param.mk.config:
	$(JAVA) -classpath $(CLASSPATH) $(PARAM_METHOD) $(LINUX_KERNEL_DIR) $(obj)

# Include the debug version string in the timestamp because mvfs_when.h,
# which contains the version string, is only built once for all the variants
# of a particular architecture.
#
$(obj)/timestamp.c: $(VNODE_BUILT_OBJECTS) $(MVFS_BUILT_OBJECTS)
	@echo GENERATE $@
	@TZ=GMT date '+const char mdki_vnode_build_time[] = "$$Date: %Y-%m-%d.%T$(MVFS_DEBUG_FLAGS) (UTC) $$";' >$(obj)/timestamp.c
	@echo 'static const char mdki_vnode_build_host[] = "$$BuiltOn: '`uname -a`' $$";' >>$(obj)/timestamp.c

install: mvfs.ko
	mkdir -p $(INSTALL_DIR)
	install --backup --suffix=.save mvfs.ko $(INSTALL_DIR)/mvfs.ko
	$(DEPMOD)

clean: cleano
	-rm -f mvfs_param.mk.config

cleano:
	-rm -rf *.o *.kobj *.obj *.ko *.mod.? .*.cmd .tmp_versions
# $Id: f3415b8f.3ff711e3.940c.00:01:84:c3:8a:52 $ 
