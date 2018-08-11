#!/bin/bash

WORKERS=$(cat workers)
WORKERS=($WORKERS)
for index in "${!WORKERS[@]}";
do
 echo "Cleaning WORKER $((index+1)) ${WORKERS[$index]}"
 ssh -q -T ${WORKERS[$index]} "rm -rf /home/cc/worker/*"
 echo "Worker $((index+1)) cleaned"
done
