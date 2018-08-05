#!/bin/bash
echo "Restarting Memcached..."
sh /home/cc/nfs/aetrio/scripts/start_memcached.sh

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
rm -rf /home/cc/nfs/aetrio/logs/worker/worker${index}.out
export LD_LIBRARY_PATH=/home/cc/nfs/install/lib:${LD_LIBRARY_PATH}
/home/cc/nfs/aetrio/build/aetrio_worker $((index+1)) > /home/cc/nfs/aetrio/logs/worker/worker${index}.out 2> /home/cc/nfs/aetrio/logs/worker/worker${index}.err < /dev/null &
ps -ef |grep aetrio_worker
EOF
 echo "Worker $((index+1)) started"
done
