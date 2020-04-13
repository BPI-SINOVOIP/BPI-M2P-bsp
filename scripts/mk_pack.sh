#!/bin/bash

die() {
        echo "$*" >&2
        exit 1
}

[ -s "./env.sh" ] || die "please run ./configure first."

set -e

. ./env.sh

echo "pack $board"

BOOTLOADER=${TOPDIR}/SD/${board}/100MB
BOOT=${TOPDIR}/SD/${board}/BPI-BOOT
ROOT=${TOPDIR}/SD/${board}/BPI-ROOT

PACK=${TOPDIR}/sunxi-pack
KERN_DIR=${TOPDIR}/linux-sunxi

if [ -d ${TOPDIR}/SD ]; then
	rm -rf ${TOPDIR}/SD
fi

if [ -d ${TOPDIR}/output ]; then
	rm -rf ${TOPDIR}/output
fi

mkdir -p $BOOTLOADER
mkdir -p $BOOT
mkdir -p $ROOT

pack_bootloader()
{
	PLATFORM=dragonboard
	BOARDS=`(cd sunxi-pack/chips/$MACH/configs ; ls -1d ${BOARD%-*}*)`
	for IN in $BOARDS ; do
		cd $PACK
		echo "pack -c $MACH -p $PLATFORM -b $IN"
		./pack -c $MACH -p $PLATFORM -b $IN
		cd -
		$TOPDIR/scripts/bootloader.sh $IN
	done
	
	cp $TOPDIR/output/100MB/* ${BOOTLOADER}/
}

pack_boot()
{
	echo "pack boot"

	dest_path=${BOOT}/bananapi/${board}/linux

	mkdir -p $dest_path
	cp -a ${PACK}/chips/${MACH}/configs/default/linux/* ${dest_path}/
	cp -a ${PACK}/chips/${MACH}/configs/${BOARD%-*}*/linux/* ${dest_path}/
	cp -a ${KERN_DIR}/arch/${ARCH}/boot/uImage ${dest_path}/uImage
}

pack_root()
{
	echo "pack root"

	# bootloader files
	bootloader_path=${ROOT}/usr/lib/u-boot/bananapi/${board}

	mkdir -p $bootloader_path
	cp -a ${BOOTLOADER}/${BOARD%-*}*.gz ${bootloader_path}/

	# kernel modules files
	modules_path=${ROOT}/lib/modules
	mkdir -p $modules_path
	cp -a ${KERN_DIR}/output/lib/modules/${KERNEL_MODULES} ${modules_path}/

	# kernel headers files
	#headers_path=${ROOT}/usr/src/
	#mkdir -p $headers_path
	#cp -a ${KERN_DIR}/output/usr/src/${KERNEL_HEADERS} ${headers_path}/
}

tar_packages()
{
	echo "tar download packages"

	(cd $BOOT ; tar czvf ${TOPDIR}/SD/${board}/BPI-BOOT-${board}.tgz .)
	(cd $ROOT ; tar czvf ${TOPDIR}/SD/${board}/${KERNEL_MODULES}.tgz lib/modules)
	#(cd $ROOT ; tar czvf ${TOPDIR}/SD/${board}/${KERNEL_HEADERS}.tgz usr/src/${KERNEL_HEADERS})
	(cd $ROOT ; tar czvf ${TOPDIR}/SD/${board}/BOOTLOADER-${board}.tgz usr/lib/u-boot/bananapi)
}

pack_bootloader
pack_boot
pack_root
tar_packages

echo "pack finish"
