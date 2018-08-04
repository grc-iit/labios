#!/bin/bash

NATS_WORKER_MASTER=$(cat nats_worker_master)
NATS_WORKER_SLAVES=$(cat nats_worker_slaves)
#NATS_CLIENTS=$(cat nats_clients)

#for server_node in $NATS_CLIENTS
#do
	#echo $server_node
	#ssh $server_node "killall gnatsd"
	#ssh $server_node "rm -rf /home/cc/nats/*"
	#ssh $server_node "mkdir -p /home/cc/nats"
	#ssh $server_node "nohup /home/cc/nfs/install/bin/gnatsd -p 4222 -a $server_node -DV -l /home/cc/nats/nats_client.log > /home/cc/nats/nats_client.out 2> /home/cc/nats/nats_client.err < /dev/null &"
#done

echo $NATS_WORKER_MASTER
ssh $NATS_WORKER_MASTER "killall gnatsd"
ssh $NATS_WORKER_MASTER "rm -rf /home/cc/nats/*"
ssh $NATS_WORKER_MASTER "mkdir -p /home/cc/nats"
ssh $NATS_WORKER_MASTER "nohup /home/cc/nfs/install/bin/gnatsd -p 4222 -cluster nats://$NATS_WORKER_MASTER:5222 -DV -l /home/cc/nats/nats_master.log > /home/cc/nats/nats_master.out 2> /home/cc/nats/nats_master.err < /dev/null &"

for server_node in $NATS_WORKER_SLAVES
do
	echo $server_node
        ssh $server_node "killall gnatsd"
        ssh $server_node "rm -rf /home/cc/nats/*"
        ssh $server_node "mkdir -p /home/cc/nats"
        ssh $server_node "nohup /home/cc/nfs/install/bin/gnatsd -p 4222 -cluster nats://$server_node:5222 -routes nats://$NATS_WORKER_MASTER:5222 -DV -l /home/cc/nats/nats_slave.log > /home/cc/nats/nats_slave.out 2> /home/cc/nats/nats_slave.err < /dev/null &"
done






