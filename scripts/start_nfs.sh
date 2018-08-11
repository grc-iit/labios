NODES=$(cat nodes)

for server_node in $NODES

do    
echo "Starting $server_node"
ssh -t -T $server_node << EOF
sudo umount --force -l /home/cc/nfs
sudo mount 10.52.0.185:/ /home/cc/nfs
EOF

done
