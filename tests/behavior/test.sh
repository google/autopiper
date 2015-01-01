#!/bin/bash

ap=../../build/src/autopiper
if [ $# -gt 0 ]; then
    ap=$1
fi

for t in *.ap; do
    echo $t
    python3 ./test.py $ap $t
    if [ $? -ne 0 ]; then
        exit 1
    else
        echo "    Passed."
    fi
done
