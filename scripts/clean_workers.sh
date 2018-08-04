#!/bin/bash

WORKERS=$(cat workers)
WORKERS=($WORKERS)
for index in "${!WORKERS[@]}";
do
 echo "Cleaning WORKER $((index+1)) ${WORKERS[$index]}"
 ssh ${WORKERS[$index]} << EOF
rm -rf /home/cc/worker/$((index+1))/*
du -hs /home/cc/worker/$((index+1))
EOF
 echo "Worker $((index+1)) cleaned"
done
