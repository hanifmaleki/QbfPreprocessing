#!/bin/bash

if (($# != 1))
then
    echo "expecting QDIMACS formula as single argument!"
    exit 1
fi

FORMULA=$1

./qbce-prepro $FORMULA --print-formula --simplify > tmp/formula-simplify-$$-0.qdimacs 2>/dev/null 
RES=$?

if (($RES))
then
    exit $RES
fi

./qbce-prepro tmp/formula-simplify-$$-0.qdimacs --print-formula --simplify > tmp/formula-simplify-$$-1.qdimacs 2>/dev/null 
RES=$?

if (($RES))
then
    exit $RES
fi

diff tmp/formula-simplify-$$-0.qdimacs tmp/formula-simplify-$$-1.qdimacs
RES=$?

if (($RES))
then
    exit $RES
fi

rm -f tmp/formula-simplify-$$-0.qdimacs
rm -f tmp/formula-simplify-$$-1.qdimacs

exit $RES
