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
sudo chmod -x /etc/update-motd.d/*
cp /home/cc/nfs/limits.conf /etc/security/limits.conf
cp /home/cc/nfs/common-session /etc/pam.d/common-session
cp /home/cc/nfs/common-session-noninteractive /etc/pam.d/common-session-noninteractive
sysctl -w net.core.rmem_max=8388608
sysctl -w net.core.wmem_max=8388608
sysctl -w net.ipv4.tcp_rmem=8388608
sysctl -w net.ipv4.tcp_wmem=8388608
sysctl -w net.ipv4.tcp_keepalive_time=120

