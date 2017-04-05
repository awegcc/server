#!/bin/bash

value=0

while :
do
	echo ${value}
	./put k_${value} v_${value}
	value=$((value+1))
done
