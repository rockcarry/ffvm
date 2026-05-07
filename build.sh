#!/bin/bash

# environment needs:
# TOOLCHAIN_NAME       - indicate toolchain name
# BUILD_PACKAGE_OUTDIR - indicate package binary output dir, if empty $PWD/out will be used

set -e

TOPDIR=$PWD

if [ "$BUILD_PACKAGE_OUTDIR"x = ""x ]; then
    BUILD_PACKAGE_OUTDIR=$PWD/out
fi

build_package()
{
    local CFLAGS="-Wall -Wno-strict-aliasing -Wno-stringop-truncation -Ofast -g -DWITH_LIBHW -I$PWD/libpcap/include -I$BUILD_PACKAGE_OUTDIR/$TOOLCHAIN_NAME/include"
    local LDFLAGS="-L$BUILD_PACKAGE_OUTDIR/$TOOLCHAIN_NAME/lib -lhw -lfuncs -lgdi32 -lwinmm -lws2_32"
    if [ "$1"x = "--with-libpcap"x ]; then
        ${CROSS_COMPILE}gcc --static $CFLAGS utils.c ethphy-libpcap.c  ffvm.c $LDFLAGS -o ffvm
    else
        ${CROSS_COMPILE}gcc --static $CFLAGS utils.c ethphy-tapwin32.c ffvm.c $LDFLAGS -o ffvm
    fi
    ${CROSS_COMPILE}strip --strip-unneeded ffvm.exe
}

case "$1" in
"")
    build_package
    ;;
--with-libpcap)
    build_package --with-libpcap
    ;;
clean)
    make clean || true
    ;;
distclean)
    make distclean || true
    rm -rf $TOPDIR/Makefile
    ;;
esac

