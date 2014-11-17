#!/bin/bash

BINARIES="src/autopiper-backend src/autopiper"

mkdir /build
cd /build
git clone $SOURCEREPO autopiper || exit 1
cd autopiper
mkdir build
cd build

# TODO: get a static build working.
cmake .. || exit 1

make -j 8 || exit 1
cp $BINARIES $DESTPATH
