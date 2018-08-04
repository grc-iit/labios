#!/usr/bin/env bash
CLIENT_NODES=$(cat clients)
for node in $CLIENT_NODES
do
	echo "$node is starting"
        ssh $node "sudo sh /home/cc/nfs/aetrio/scripts/setup_clients.sh"
	echo "$node is running"
done
