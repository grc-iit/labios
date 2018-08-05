#!/bin/bash

MEMCACHED_CLIENTS=$(cat memcached_clients)
MEMCACHED_SERVER=$(cat memcached_server)

ssh $MEMCACHED_SERVER "killall memcached"
ssh $MEMCACHED_SERVER "/home/cc/nfs/install/bin/memcached -p 11211 -l $MEMCACHED_SERVER -d  -I 4M -c 1048576 -m 32768"

for server_node in $MEMCACHED_CLIENTS
do
	echo $server_node
	ssh $server_node "killall memcached"
	ssh $server_node "/home/cc/nfs/install/bin/memcached -p 11211 -l $server_node -d  -I 4M -c 1048576 -m 32768"
	ssh $server_node "/home/cc/nfs/install/bin/memcached -p 11212 -l $server_node -d  -I 4M -c 1048576 -m 32768"
done


