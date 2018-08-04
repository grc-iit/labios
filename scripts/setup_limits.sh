#!/usr/bin/env bash
cp /home/cc/nfs/limits.conf /etc/security/limits.conf
cp /home/cc/nfs/common-session /etc/pam.d/common-session
cp /home/cc/nfs/common-session-noninteractive /etc/pam.d/common-session-noninteractive
sysctl -w net.core.rmem_max=8388608
sysctl -w net.core.wmem_max=8388608
sysctl -w net.ipv4.tcp_rmem=8388608
sysctl -w net.ipv4.tcp_wmem=8388608
sysctl -w net.ipv4.tcp_keepalive_time=120

