#!/bin/bash

TMPFILE=/tmp/tmp-$$.qdimacs
CNT=1

while true
do 
    echo "Running test $CNT"
    ./qbfuzz.py -v 85 -c 120 -r 0.5 --min=4 --max=7 -s 15 > $TMPFILE
    ./check-output.sh $TMPFILE
    RES=$?
    if (($RES)); 
    then 
        echo "FAILURE for test file: $TMPFILE"
        break
    fi
    ((CNT++))
done


