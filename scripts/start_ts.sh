#!/bin/bash

TASK_SCHEDULERS=$(cat ts_nodes)
CLIENT_NODES=$(cat clients)
TASK_SCHEDULERS=($TASK_SCHEDULERS) 
CLIENT_NODES=($CLIENT_NODES)

ssh ${TASK_SCHEDULERS[0]} "killall aetrio_task_scheduler"
ssh ${TASK_SCHEDULERS[0]} "rm -rf /home/cc/nfs/aetrio/logs/ts/ts*.out"
for index in "${!CLIENT_NODES[@]}";
do
 echo "Starting TS ${index} ${TASK_SCHEDULERS[$index]}"
 ssh ${TASK_SCHEDULERS[$index]} << EOF
export LD_LIBRARY_PATH=/home/cc/nfs/install/lib:${LD_LIBRARY_PATH}
if [ $(($index % 2)) -eq 0 ]; then 
/home/cc/nfs/aetrio/build/aetrio_task_scheduler "-anats://${CLIENT_NODES[$index]}:4222/" "-bnats://tabio-33:4222" > /home/cc/nfs/aetrio/logs/ts/ts${index}.out &
else
/home/cc/nfs/aetrio/build/aetrio_task_scheduler "-anats://${CLIENT_NODES[$index]}:4222/" "-bnats://tabio-33:4223" > /home/cc/nfs/aetrio/logs/ts/ts${index}.out &
fi
EOF
done
