#!/usr/bin/env bash
NODES=$(cat nodes)
for node in $NODES
do
	echo "$node"
        ssh -t $node "sudo sh /home/cc/nfs/aetrio/scripts/setup_limits.sh"
	echo "$node completed"
done

