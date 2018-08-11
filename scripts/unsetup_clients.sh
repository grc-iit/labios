#!/usr/bin/env bash
umount --force -l /mnt/pfs 
#umount --force -l /mnt/bb
ps -aef | grep pvfs | awk '{print $2}' | xargs kill -9
killall pvfs2-client
export LD_LIBRARY_PATH=/home/cc/nfs/install/lib:${LD_LIBRARY_PATH}
rmmod /home/cc/nfs/install/lib/modules/`uname -r`/kernel/fs/pvfs2/pvfs2.ko
rm -r /mnt/*
mkdir /mnt/pfs
#mkdir /mnt/bb
mount | grep pvfs2

