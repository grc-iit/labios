#!/usr/bin/env bash
cd /home/cc/nfs/aetrio/logs/ts
for fname in ts*; do     [ -f "$fname" ] || continue;    > $fname; done
cd /home/cc/nfs/aetrio/logs/worker
for fname in worker*; do     [ -f "$fname" ] || continue;    > $fname; done
