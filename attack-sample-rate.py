#!/usr/bin/python

SAMPLES_PER_SECOND = 32000

attack_rates = [ 
	4.1, 
	2.6,
	1.5,
	1.0,
	0.640,
	0.380,
	0.260,
	0.160,
	0.096,
	0.064,
	0.040,
	0.024,
	0.016,
	0.010,
	0.006,
	0.000
]

for rate in attack_rates:
	print "%d, // %0.3f" % ((rate * SAMPLES_PER_SECOND) / (0x800 / 32), rate)
