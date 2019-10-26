#!/usr/bin/env bash
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
umount --force -l /mnt/pfs 
#umount --force -l /mnt/bb
ps -aef | grep pvfs | awk '{print $2}' | xargs kill -9
killall pvfs2-client
export LD_LIBRARY_PATH=/home/cc/nfs/install/lib:${LD_LIBRARY_PATH}
rmmod /home/cc/nfs/install/lib/modules/`uname -r`/kernel/fs/pvfs2/pvfs2.ko
rm -r /mnt/*
mkdir /mnt/pfs
#mkdir /mnt/bb
chown cc:cc -R /mnt
echo "loading kernel module"
insmod /home/cc/nfs/install/lib/modules/`uname -r`/kernel/fs/pvfs2/pvfs2.ko
echo "loading pvfs2-client"
/home/cc/nfs/install/sbin/pvfs2-client -p /home/cc/nfs/install/sbin/pvfs2-client-core
echo "mounting the pvfs2"
mount -t pvfs2 tcp://tabio-34:3334/orangefs /mnt/pfs
#mount -t pvfs2 tcp://tabio-bb-1:3334/orangefs /mnt/bb
mount | grep pvfs2

