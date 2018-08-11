ps -aef | grep pvfs | awk '{print $2}' | xargs kill -9
rm -rf /home/cc/pfs/*
rm -rf /home/cc/bb/*
#rm -rf /mnt/disk/*
