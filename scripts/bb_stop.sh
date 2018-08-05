#!/bin/bash

SERVER_NODES=$(cat bb_servers)

for server_node in $SERVER_NODES
do
ssh $server_node "sudo killall pvfs2-server"
ssh $server_node "sudo sh /home/cc/nfs/aetrio/scripts/kill_pfs.sh"
ssh $server_node "sudo rm -rf /mnt/disk/*"
ssh $server_node "mkdir -p /mnt/disk/storage"
ssh $server_node "ps -aef | grep pvfs2"
echo "$server_node  has stoped"
done
