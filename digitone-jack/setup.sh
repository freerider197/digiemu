#!/usr/bin/env bash
# Recreates the build environment after a sandbox reset. Run from digitone-jack/.
set -e
command -v m68k-linux-gnu-gcc >/dev/null || { apt-get update -q && apt-get install -y -q binutils-m68k-linux-gnu gcc-m68k-linux-gnu; }
[ -d .deps/elekloader ] || git clone -q https://github.com/irpina/elekloader .deps/elekloader
git -C .deps/elekloader checkout -q 69707b3
echo "Ready. Put the stock Digitone_and_Digitone_Keys_OS1.44.syx (sha256 d4f200d0...) in this folder, then follow docs/BUILD_AND_TEST.md section 2 with PYTHONPATH=.deps/elekloader"
