#!/bin/bash

WORKERS=$(cat workers)
WORKERS=($WORKERS)
for index in "${!WORKERS[@]}";
do
 echo "WORKER $((index+1)) ${WORKERS[$index]}"
 ssh -q -T ${WORKERS[$index]} "du -hs /home/cc/worker/$((index+1))"
done
