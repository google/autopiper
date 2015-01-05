#!/bin/bash

tmpfile=`mktemp`
../../build/src/autopiper --expand-macros test_macros.ap > $tmpfile
diff -u $tmpfile test_macros_golden.ap
if [ $? -ne 0 ]; then
    echo Output mismatched.
    exit 1
fi
rm -f $tmpfile
