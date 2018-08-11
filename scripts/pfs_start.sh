#!/bin/bash

SERVER_NODES=$(cat 4pfs_servers)

for server_node in $SERVER_NODES
do
ssh $server_node "sudo killall pvfs2-server"
ssh $server_node "sudo rm -rf /home/cc/pfs/*"
ssh $server_node "mkdir -p /home/cc/pfs/storage"
ssh $server_node "/home/cc/nfs/install/sbin/pvfs2-server -f -a ${server_node} /home/cc/nfs/aetrio/scripts/conf/multi.conf"
echo "$server_node is starting"
ssh $server_node "/home/cc/nfs/install/sbin/pvfs2-server -a ${server_node}  /home/cc/nfs/aetrio/scripts/conf/multi.conf"
ssh $server_node "ps -aef | grep pvfs2"
echo "$server_node  has started"
done
