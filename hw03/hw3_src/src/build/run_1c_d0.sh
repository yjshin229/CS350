#!/bin/bash
for i in {10..19} ; 
do ./server_lim -q 1000 2222 > server-output-1c-${i}-d0.txt & ../../client -a $i -s 20 -n 1500 -d 0 2222;
done
