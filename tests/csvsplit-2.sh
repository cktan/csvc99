# Test Case : split by -b nbytes 
set -e

rm -f x??
../csvsplit -b 8 in/csvsplit.csv 

for f in x0{0..2}; do
	echo "# File: $f"
	cat $f	
done
