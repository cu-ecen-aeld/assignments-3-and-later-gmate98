#!/bin/bash
# Script outline to install and build kernel.
# Author: Siddhant Jajoo.

set -e
set -u

OUTDIR=/tmp/aeld
KERNEL_REPO=https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux-stable.git
KERNEL_VERSION=v5.15.163
BUSYBOX_VERSION=1_33_1
FINDER_APP_DIR=$(realpath $(dirname $0))
ARCH=arm64
CROSS_COMPILE=aarch64-none-linux-gnu-
SYSROOT=$(${CROSS_COMPILE}gcc -print-sysroot)

echo ${SYSROOT}

export ARCH=${ARCH}
export CROSS_COMPILE=${CROSS_COMPILE}

# Some colors
BLUE='\033[0;34m'
NC='\033[0m' # No Color

if [ $# -lt 1 ]
then
	echo "Using default directory ${OUTDIR} for output"
else
	OUTDIR=$1
	echo "Using passed directory ${OUTDIR} for output"
fi

mkdir -p ${OUTDIR}

cd "$OUTDIR"
if [ ! -d "${OUTDIR}/linux-stable" ]; then
    #Clone only if the repository does not exist.
	echo "CLONING GIT LINUX STABLE VERSION ${KERNEL_VERSION} IN ${OUTDIR}"
	git clone ${KERNEL_REPO} --depth 1 --single-branch --branch ${KERNEL_VERSION}
fi
if [ ! -e ${OUTDIR}/linux-stable/arch/${ARCH}/boot/Image ]; then
    cd linux-stable
    echo "Checking out version ${KERNEL_VERSION}"
    git checkout ${KERNEL_VERSION}

    echo -e "${BLUE}Cleaning kernel build tree...${NC}"
    make mrproper
    echo -e "${BLUE}Making kernel defconfig...${NC}"
    make defconfig
    echo -e "${BLUE}Building kernel...${NC}"
    make -j$(nproc) all
    echo -e "${BLUE}Building modules..${NC}."
    make modules
    echo -e "${BLUE}Compiling device trees...${NC}"
    make dtbs
fi

# Clean Image in outdir if exist and copy the Image from outdir/linux-stable/arch/arm64/boot
echo "Adding the Image in outdir"

if [ -e ${OUTDIR}/Image ]; then
    rm Image
    cp ${OUTDIR}/linux-stable/arch/${ARCH}/boot/Image ${OUTDIR}
else 
    cp ${OUTDIR}/linux-stable/arch/${ARCH}/boot/Image ${OUTDIR}
fi

echo "Creating the staging directory for the root filesystem"
cd "$OUTDIR"
if [ -d "${OUTDIR}/rootfs" ]
then
	echo "Deleting rootfs directory at ${OUTDIR}/rootfs and starting over"
    sudo rm  -rf ${OUTDIR}/rootfs
fi

# Making the rootfs tree
mkdir rootfs && cd rootfs
mkdir bin dev etc home lib lib64 proc sbin sys tmp usr var
mkdir -p usr/bin usr/lib usr/sbin var/log

cd "$OUTDIR"
if [ ! -d "${OUTDIR}/busybox" ]
then
git clone https://github.com/mirror/busybox.git
# git clone git://busybox.net/busybox.git
    cd busybox
    git checkout ${BUSYBOX_VERSION}
    make distclean
    make defconfig
else
    cd busybox
fi

# TODO: Make and install busybox
make
make CONFIG_PREFIX=${OUTDIR}/rootfs install

echo "Library dependencies"
cd ${OUTDIR}/rootfs
${CROSS_COMPILE}readelf -a bin/busybox | grep "program interpreter"
${CROSS_COMPILE}readelf -a bin/busybox | grep "Shared library"

# TODO: Add library dependencies to rootfs
cp ${SYSROOT}/lib/ld-linux-aarch64.so.1 ${OUTDIR}/rootfs/lib
cp ${SYSROOT}/lib64/libm.so.6 ${SYSROOT}/lib64/libresolv.so.2 ${SYSROOT}/lib64/libc.so.6 ${OUTDIR}/rootfs/lib64

# TODO: Make device nodes
echo "Adding device nodes"
cd ${OUTDIR}/rootfs
sudo mknod -m 666 dev/null c 1 3
sudo mknod -m 600 dev/console c 5 1

# TODO: Clean and build the writer utility
echo "Building writer app"
cd ${FINDER_APP_DIR}
make clean
make all
cp writer ${OUTDIR}/rootfs/home

# TODO: Copy the finder related scripts and executables to the /home directory
# on the target rootfs
cd ${FINDER_APP_DIR}
cp -r conf/ ${OUTDIR}/rootfs/home
cp autorun-qemu.sh finder.sh finder-test.sh ${OUTDIR}/rootfs/home

# TODO: Chown the root directory
sudo chown -hR root:root ${OUTDIR}/rootfs 

# TODO: Create initramfs.cpio.gz
echo "Creating initramfs"
cd ${OUTDIR}/rootfs
find . | cpio -H newc -ov --owner root:root > ${OUTDIR}/initramfs.cpio
cd ${OUTDIR}
gzip -f initramfs.cpio
