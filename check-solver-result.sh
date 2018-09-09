#!/bin/bash

if (($# != 1))
then
    echo "expecting QDIMACS formula as argument!"
    exit 1
fi

# path to solver, to be adapted
#SOLVER="/home/staff/flonsing/depqbf/depqbf"
SOLVER="/home/e1528895/Programs/depqbf-version-6.03/depqbf"
FORMULA=$1

$SOLVER $FORMULA 2>/dev/null
ORIGRES=$?

./qbce-prepro $FORMULA --print-formula --simplify > /tmp/formula-simplify-$$-0.qdimacs 2>/dev/null 
RES=$?

# prepro failed
if (($RES))
then
    exit $RES
fi

$SOLVER /tmp/formula-simplify-$$-0.qdimacs 2>/dev/null 
PREPRORES=$?

#rm -f /tmp/formula-simplify-$$-0.qdimacs
#rm -f /tmp/formula-simplify-$$-1.qdimacs

if (($ORIGRES != $PREPRORES))
then
    exit 1
else
    exit 0
fi
