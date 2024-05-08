#!/bin/bash

set -e

DIR=$(dirname ${BASH_SOURCE[0]})
DIR=$(realpath ${DIR})

mkdir -p $DIR/out

for i in {1..100}; do
	IN=$DIR/in/$i.csv
        GOOD=$DIR/good/$i.py
        OUT=$DIR/out/$i.py

        [ -f $IN ] || continue

        echo test ${i}

	$DIR/../csv2py $IN > $OUT ||
                { echo '--- csvnorm FAILED ---'; exit 1; }
        python3 $DIR/pydiff.py $OUT $GOOD ||
                { echo '--- pydiff FAILED ---'; exit 1; }
done
