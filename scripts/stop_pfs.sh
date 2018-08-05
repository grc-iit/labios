#!/bin/bash

SERVER_NODES=$(cat pfs_servers)

for server_node in $SERVER_NODES
do
ssh $server_node "sudo sh /home/cc/nfs/aetrio/scripts/kill_pfs.sh"
ssh $server_node "ps -aef | grep pvfs2"
echo "$server_node  has stopted"

done
