#!/bin/bash
echo "Calculating TASK SCHEDULER..."
ssh tabio-33 "sh /home/cc/nfs/aetrio/scripts/calc_ts.sh"
echo "Calculating WORKERS..."
ssh tabio-33 "sh /home/cc/nfs/aetrio/scripts/calc_worker.sh"
