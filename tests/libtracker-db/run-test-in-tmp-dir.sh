#!/bin/bash

GLIB_DIR=`pkg-config --variable=prefix glib-2.0`
. ../scripts/xdg_dirs.source

# Ensure we have gtester in PATH
export PATH=$PATH:$GLIB_DIR/bin

make test
./tracker-db-manager

. ../scripts/xdg_dirs.unsource
