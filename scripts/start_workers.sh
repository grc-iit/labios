#!/bin/bash

CLIENTS=$(cat clients)
CLIENTS=($CLIENTS)
v1=${CLIENTS[@]/#/--SERVER=}
v1=($v1)
MEMCACHED_CLIENT_STR=${v1[@]/%/:11211}

WORKERS=$(cat workers)
WORKERS=($WORKERS)
for index in "${!WORKERS[@]}";
do
 echo "Starting WORKER $((index+1)) ${WORKERS[$index]}"
 ssh ${WORKERS[$index]} << EOF
killall aetrio_worker
rm -rf /home/cc/tabio/logs/worker*
mkdir -p /home/cc/tabio/logs/
export LD_LIBRARY_PATH=/home/cc/nfs/install/lib:${LD_LIBRARY_PATH}
/home/cc/nfs/aetrio/build/aetrio_worker $((index+1)) "-bnats://${WORKERS[$index]}:4222/" "-c$MEMCACHED_CLIENT_STR" "-d--SERVER=tabio-33:11211"  > /home/cc/tabio/logs/worker${index}.out 2> /home/cc/tabio/logs/worker${index}.err < /dev/null &
ps -ef |grep aetrio_worker
EOF
 echo "Worker $((index+1)) started"
done
