#!/usr/bin/python

GAIN_LINEAR = [
	0.0,	# 0
	4.1,	# 1
	3.1,	# 2
	2.6,	# 3
	2.0,	# 4
	1.5,	# 5
	1.3,	# 6
	1.0,	# 7	
	0.770,	# 8
	0.640,	# 9
	0.510,	# A
	0.380,	# B
	0.320,	# C
	0.260,	# D
	0.190,	# E
	0.160,	# F
	0.130,	# 10
	0.096,	# 11
	0.080,	# 12
	0.064,	# 13
	0.048,	# 14
	0.040, 	# 15
	0.032,	# 16
	0.024,	# 17
	0.020,	# 18
	0.016,	# 19
	0.012,	# 1A
	0.010,	# 1B
	0.008,	# 1C
	0.006,	# 1D
	0.004,	# 1E
	0.002	# 1F
]

GAIN_BENT = [
	0.0,	# 0
	7.2,	# 1
	5.4,	# 2
	4.6,	# 3
	3.5,	# 4
	2.6,	# 5
	2.3,	# 6
	1.8,	# 7	
	1.3,	# 8
	1.1,	# 9
	0.900,	# A
	0.670,	# B
	0.560,	# C
	0.450,	# D
	0.340,	# E
	0.280,	# F
	0.220,	# 10
	0.170,	# 11
	0.140,	# 12
	0.110,	# 13
	0.084,	# 14
	0.070, 	# 15
	0.056,	# 16
	0.042,	# 17
	0.035,	# 18
	0.028,	# 19
	0.021,	# 1A
	0.018,	# 1B
	0.014,	# 1C
	0.011,	# 1D
	0.007,	# 1E
	0.0035, # 1F
]

def show_rates(rates, name):
	print "int %s[32] = {" % name

	for x in range(len(rates) / 8):
		array = ["%4d" % i for i in rates[x * 8:x * 8 + 8]]
		string = ", ".join(array)
		print "\t%s," % string

	print "};"

# LINEAR
rate_linear = []
steps = 64
for time in GAIN_LINEAR:
	samples = (time * 32000)
	rate = samples / steps

	rate_linear.append(rate)

show_rates(rate_linear, "GAIN_LINEAR")


# BENT LINE
# Steps from 0% to 75% (1536): Add 1/64
steps0 = 1536 / (2048 / 64)

# Steps from 75% to 100% (2048): Add 1/256
steps1 = (2048 - 1536) / (2048 / 256)

total_steps = steps0 + steps1

rate_bent = []

for time in GAIN_BENT:
	samples = (time * 32000)
	rate = samples / total_steps

	rate_bent.append(rate)

show_rates(rate_bent, "GAIN_BENT")

