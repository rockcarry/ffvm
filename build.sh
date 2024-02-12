#!/bin/sh

set -e

CFLAGS="-Wall -Os -I$PWD/libavdev/include"
LDFLAGS="-L$PWD/libavdev/lib -lavdev -lgdi32"

gcc $CFLAGS ffvm.c $LDFLAGS -o ffvm

