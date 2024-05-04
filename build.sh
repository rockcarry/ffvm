#!/bin/sh

set -e

CFLAGS="-Wall -Wno-strict-aliasing -Wno-stringop-truncation -Ofast -g -I$PWD/libavdev/include"
LDFLAGS="-L$PWD/libavdev/lib -lavdev -lgdi32 -lwinmm"

gcc --static $CFLAGS utils.c ffvm.c $LDFLAGS -o ffvm
strip ffvm.exe

