#!/usr/bin/python

import sys

for line in sys.stdin:
	line = line.strip()

	if len(line) == 0:
		continue

	#print line
	
	(mnemonic, operands, opcode, lenz, rest) = line.split(None, 4)

	opcode = int(opcode, 16)

	print "#{mnemonic:<10} {operands:<10} {opcode:08b} {len}".format(mnemonic = mnemonic, operands = operands, opcode = opcode, len = lenz)
