u!/usr/bin/env bash
umount --force /mnt/pfs 
umount --force /mnt/bb
ps -aef | grep pvfs | awk '{print $2}' | xargs kill -9
killall pvfs2-client
export LD_LIBRARY_PATH=/home/cc/nfs/install/lib:${LD_LIBRARY_PATH}
rmmod /home/cc/nfs/install/lib/modules/`uname -r`/kernel/fs/pvfs2/pvfs2.ko
rm -r /mnt/*
mkdir /mnt/pfs
mkdir /mnt/bb
chown cc:cc -R /mnt
echo "loading kernel module"
rmmod pvfs2.ko
insmod /home/cc/nfs/install/lib/modules/`uname -r`/kernel/fs/pvfs2/pvfs2.ko
echo "loading pvfs2-client"
/home/cc/nfs/install/sbin/pvfs2-client -p /home/cc/nfs/install/sbin/pvfs2-client-core
echo "mounting the pvfs2"
mount -t pvfs2 tcp://tabio-34:3334/orangefs /mnt/pfs
mount -t pvfs2 tcp://tabio-bb-1:3334/orangefs /mnt/bb
mount | grep pvfs2

