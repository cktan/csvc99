#!/bin/bash

mkdir -p out

for i in csv2py-{1..10}.sh ; do
	F=$i
	if [ -f $F ]; then
		echo $F
		./$F > out/$F.out || { echo "FAILED!"; exit 1; }
		diff out/$F.out good/$F.out || { echo "FAILED!"; exit 1; }
	fi
done
