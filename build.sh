#!/bin/sh

set -e

CFLAGS="-Wall -Os -g -I$PWD/libavdev/include"
LDFLAGS="-L$PWD/libavdev/lib -lavdev -lgdi32"

gcc --static $CFLAGS ffvm.c $LDFLAGS -o ffvm

