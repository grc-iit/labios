cd /home/cc/nfs/aetrio/logs/ts
rm ts.csv
cat ts*.out >> ts.csv
sum=0;
while IFS=, read -r col1 col2;  
do
     sum=`echo $sum+$col2 | bc`
done <  ts.csv

echo $sum | bc -l
