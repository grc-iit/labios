#!/bin/bash

TASK_SCHEDULERS=$(cat ts_nodes)
CLIENT_NODES=$(cat clients)
TASK_SCHEDULERS=($TASK_SCHEDULERS) 
CLIENT_NODES=($CLIENT_NODES)

ssh ${TASK_SCHEDULERS[0]} "killall aetrio_task_scheduler"
ssh ${TASK_SCHEDULERS[0]} "rm -rf /home/cc/tabio/logs/ts*"
ssh ${TASK_SCHEDULERS[0]} "mkdir -p /home/cc/tabio/logs/"
for index in "${!CLIENT_NODES[@]}";
do
 echo "Starting TS ${index} ${TASK_SCHEDULERS[$index]}"
 ssh ${TASK_SCHEDULERS[$index]} << EOF
export LD_LIBRARY_PATH=/home/cc/nfs/install/lib:${LD_LIBRARY_PATH}
/home/cc/nfs/aetrio/build/aetrio_task_scheduler "-anats://${CLIENT_NODES[$index]}:4222/" "-bnats://tabio-33:4222/" "-d--SERVER=tabio-33:11211"  > /home/cc/tabio/logs/ts${index}.out 2> /home/cc/tabio/logs/ts${index}.err < /dev/null &
EOF
done
