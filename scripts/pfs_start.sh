#!/bin/bash
# Copyright (C) 2019  SCS Lab <scs-help@cs.iit.edu>, Hariharan
# Devarajan <hdevarajan@hawk.iit.edu>, Anthony Kougkas
# <akougkas@iit.edu>, Xian-He Sun <sun@iit.edu>
#
# This file is part of Labios
# 
# Labios is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as
# published by the Free Software Foundation, either version 3 of the
# License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU Lesser General Public License for more details.
#
# You should have received a copy of the GNU General Public
# License along with this program.  If not, see
# <http://www.gnu.org/licenses/>.

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
