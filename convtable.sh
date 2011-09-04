#!/bin/bash

grep -v '^$' opcodes.txt | awk '{print "{ \""$1,$2"\", 0x"$3", "$4" },"}' 
