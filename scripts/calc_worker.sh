cd /home/cc/nfs/aetrio/logs/worker
rm worker.csv
cat worker*.out >> worker.csv
sum=0;
while IFS=, read -r col1 col2;  
do
     sum=`echo $sum+$col2 | bc`
done <  worker.csv

echo $sum/16 | bc -l
