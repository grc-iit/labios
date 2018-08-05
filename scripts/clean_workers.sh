#!/bin/bash

WORKERS=$(cat workers)
WORKERS=($WORKERS)
for index in "${!WORKERS[@]}";
do
 echo "Cleaning WORKER $((index+1)) ${WORKERS[$index]}"
 ssh -q -T ${WORKERS[$index]} "rm -rf /home/cc/worker/$((index+1))/*"
 ssh -q -T ${WORKERS[$index]} "du -hs /home/cc/worker/$((index+1))"
 echo "Worker $((index+1)) cleaned"
done
