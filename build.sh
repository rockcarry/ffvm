#!/bin/sh

set -e

CFLAGS="-Wall -Wno-strict-aliasing -Wno-stringop-truncation -Ofast -g -I$PWD/libavdev/include -I$PWD/libpcap/include"
LDFLAGS="-L$PWD/libavdev/lib -lavdev -lgdi32 -lwinmm"

case "$1" in
--with-libpcap)
    ${CROSS_COMPILE}gcc --static $CFLAGS utils.c ethphy-libpcap.c  ffvm.c $LDFLAGS -o ffvm
    ${CROSS_COMPILE}strip --strip-unneeded ffvm.exe
    ;;
*)
    ${CROSS_COMPILE}gcc --static $CFLAGS utils.c ethphy-tapwin32.c ffvm.c $LDFLAGS -o ffvm
    ${CROSS_COMPILE}strip --strip-unneeded ffvm.exe
    ;;
esac
