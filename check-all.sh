#!/bin/bash
search_dir=/home/e1528895/Downloads/QBFEVAL_18_DATASET/QBFEVAL_18_DATASET/PCNF

for entry in `ls -rS  $search_dir`; do
    echo $entry
    ./check-solver-result.sh "$search_dir/${entry}"
done
