#!/usr/bin/python

SAMPLES_PER_SECOND = 32000

# How long it takes to go from 1 to SL
DR = [
	1.2,	# 0
	0.740,	# 1
	0.440,	# 2,
	0.290,	# 3,
	0.180,	# 4,
	0.110,	# 5,
	0.074,	# 6,
	0.037	# 7
]

# The Sustain Level - when to stop decay
SL = [ (x + 1.0) / 8.0 for x in range(8) ] 

result = []

for dr in DR:
	for sl in SL:
		start = 2048
		end   = int(2048 * sl)
		total = start - end

		# print "Start: %d   End: %d   Total: %d" % ( start, end, total )
		# print "SL[%d]: %d" % ( SL[x], total )
		# print "Decreasing by %0.2f each step" % ( 1.0 - (1.0 / 256.0) )
		# print "1/256: %0.4f" % ( 1.0 / 256.0 )

		nb_steps = 0
		env = start
		# Calculate number of steps
		while env > end:
			env = env * (1.0 - (1.0/256.0))
			nb_steps = nb_steps + 1

		# How long should each step take, in seconds?
		if nb_steps > 0:
			seconds_per_step = dr / nb_steps
		else:
			# Division by zero. No steps needed.
			seconds_per_step = 0

		# We only adjust enveloppe once per sample. How many samples
		# does step_time represent?
		sample_rate = seconds_per_step * SAMPLES_PER_SECOND

		print "DR:%0.3f  SL:%0.3f  Steps to go from %d to %d: %d   seconds/step:%0.3f  sample rate:%d" % ( dr, sl, start, end, nb_steps, seconds_per_step, sample_rate )

		#print "%d, // %0.3f" % ((rate * SAMPLES_PER_SECOND) / 0x800, rate)

		# print "This decay needs to happen over %d samples." % nb_samples
		result.append(sample_rate)

#print "DR = { %s }" % ( ", ".join(["%d" % i for i in result]) )
print "int DECAY_RATE[8][8] = {"

for x in range(len(result) / 8):
        array = ["%4d" % i for i in result[x * 8:x * 8 + 8]]
        string = ", ".join(array)
        print "\t{ %s }," % string

print "};"


