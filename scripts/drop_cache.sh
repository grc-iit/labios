#!/usr/bin/env bash
CLIENT_NODES=$(cat /home/cc/nfs/aetrio/scripts/workers)
for node in $CLIENT_NODES
do
        ssh $node "sudo sh /home/cc/nfs/aetrio/scripts/drop.sh"
done
	echo "All nodes have dropped all caches"
