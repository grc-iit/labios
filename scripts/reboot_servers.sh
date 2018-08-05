#!/usr/bin/env bash
CLIENT_NODES=$(cat servers_reboot)
for node in $CLIENT_NODES
do
	echo "$node is starting"
        ssh $node "sudo reboot"
done
