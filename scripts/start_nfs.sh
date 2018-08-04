NODES=$(cat nodes)

for server_node in $NODES

do    

ssh -t $server_node << EOF

sudo umount --force -l /home/cc/nfs

sudo mount 10.52.0.96:/ /home/cc/nfs

EOF

done
