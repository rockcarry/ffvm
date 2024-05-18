#!/bin/sh

set -e

CFLAGS="-Wall -Wno-strict-aliasing -Wno-stringop-truncation -Ofast -g -I$PWD/libavdev/include -I$PWD/libpcap/include"
LDFLAGS="-L$PWD/libavdev/lib -L$PWD/libpcap/lib -lavdev -lpcap -lgdi32 -lwinmm"

gcc --static $CFLAGS utils.c ethphy.c ffvm.c $LDFLAGS -o ffvm
strip ffvm.exe

