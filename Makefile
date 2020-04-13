.PHONY: all clean help
.PHONY: u-boot kernel kernel-config
.PHONY: linux pack

include chosen_board.mk

U_CROSS_COMPILE=$(U_COMPILE_TOOL)/arm-linux-gnueabi-
K_CROSS_COMPILE=$(K_COMPILE_TOOL)/arm-linux-gnueabihf-

ROOTFS=$(CURDIR)/rootfs/linux/default_linux_rootfs.tar.gz

Q=
J=$(shell expr `grep ^processor /proc/cpuinfo  | wc -l` \* 2)

all: bsp
bsp: u-boot kernel

u-boot: 
	$(Q)$(MAKE) -C u-boot-sunxi $(UBOOT_CONFIG) CROSS_COMPILE=$(U_CROSS_COMPILE) -j$J
	$(Q)$(MAKE) -C u-boot-sunxi all CROSS_COMPILE=$(U_CROSS_COMPILE) -j$J

u-boot-clean:
	rm -f sunxi-pack/chips/sun8iw7p1/bin/u-boot-sun8iw7p1.bin
	rm -f u-boot-sunxi/tools/sunxi_env_gen
	$(Q)$(MAKE) -C u-boot-sunxi CROSS_COMPILE=$(U_CROSS_COMPILE) -j$J distclean

kernel:
	$(Q)$(MAKE) -C linux-sunxi ARCH=$(ARCH) $(KERNEL_CONFIG)
	$(Q)$(MAKE) -C linux-sunxi ARCH=$(ARCH) CROSS_COMPILE=${K_CROSS_COMPILE} -j$J INSTALL_MOD_PATH=output uImage modules
	$(Q)$(MAKE) -C linux-sunxi ARCH=$(ARCH) CROSS_COMPILE=${K_CROSS_COMPILE} -j$J INSTALL_MOD_PATH=output modules_install
	$(Q)$(MAKE) -C linux-sunxi ARCH=$(ARCH) CROSS_COMPILE=${K_CROSS_COMPILE} -j$J headers_install

kernel-clean:
	$(Q)$(MAKE) -C linux-sunxi ARCH=$(ARCH) CROSS_COMPILE=${K_CROSS_COMPILE} -j$J distclean
	rm -rf linux-sunxi/output/
	rm -f linux-sunxi/bImage

kernel-config:
	$(Q)$(MAKE) -C linux-sunxi ARCH=$(ARCH) CROSS_COMPILE=${K_CROSS_COMPILE} -j$J menuconfig
	cp linux-sunxi/.config linux-sunxi/arch/$(ARCH)/configs/$(KERNEL_CONFIG)

clean: u-boot-clean kernel-clean
	rm -rf output/
	rm -f chosen_board.mk
	rm -f env.sh

distclean: clean
	rm -rf SD/

pack: sunxi-pack
	$(Q)scripts/mk_pack.sh

install:
	$(Q)scripts/mk_install_sd.sh

linux: 
	$(Q)scripts/mk_linux.sh $(ROOTFS)

help:
	@echo ""
	@echo "Usage:"
	@echo "  make bsp             - Default 'make'"
	@echo "  make linux         - Build target for linux platform, as ubuntu, need permisstion confirm during the build process"
	@echo "   Arguments:"
	@echo "    ROOTFS=            - Source rootfs (ie. rootfs.tar.gz with absolute path)"
	@echo ""
	@echo "  make pack            - pack the images and rootfs to a PhenixCard download image."
	@echo "  make clean"
	@echo ""
	@echo "Optional targets:"
	@echo "  make kernel           - Builds linux kernel"
	@echo "  make kernel-config    - Menuconfig"
	@echo "  make u-boot          - Builds u-boot"
	@echo ""

