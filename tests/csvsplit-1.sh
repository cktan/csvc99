# Test Case : split by -r nrecs 
set -e

rm -f x??
../csvsplit -r 2 in/csvsplit.csv 

for f in x0{0..2}; do
	echo "# File: $f"
	cat $f	
done
