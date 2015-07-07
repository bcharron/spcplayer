#!/usr/bin/python

SAMPLES_PER_SECOND = 32000

# The Sustain Level - when to stop decay
SL = [ (x + 1.0) / 8.0 for x in range(8) ]

SR = [ 
	   0,		# 0
	38.0,		# 1
	28.0,		# 2
	24.0,		# 3
	19.0,		# 4
	14.0,		# 5
	12.0,		# 6
	 9.4,		# 7
	 7.1,		# 8
	 5.9,		# 9
	 4.7,		# 10
	 3.5,		# 11
	 2.9,		# 12
	 2.4,		# 13
	 1.8,		# 14
	 1.5,		# 15
	 1.2,		# 16
	 0.880,		# 17
	 0.740,		# 18
	 0.590,		# 19
	 0.440,		# 20
	 0.370,		# 21
	 0.290,		# 22
	 0.220,		# 23
	 0.180,		# 24
	 0.150,		# 25
	 0.110,		# 26
	 0.092,		# 27
	 0.074,		# 28
	 0.055,		# 29
	 0.037,		# 30
	 0.018		# 31
]

if len(SR) != 32 or len(SL) != 8:
	print "ERROR"

result = []

for sr in SR:
	for sl in SL:
		#print "%d, // %0.3f" % ((rate * SAMPLES_PER_SECOND) / 0x800, rate)
                start   = int(2048 * sl)
		end = 5	# asymptotic, will never reach 0
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
                        seconds_per_step = sr / nb_steps
                else:
                        # Division by zero. No steps needed.
                        seconds_per_step = 0

                # We only adjust enveloppe once per sample. How many samples
                # does step_time represent?
                sample_rate = seconds_per_step * SAMPLES_PER_SECOND

                print "SR:%0.3f  SL:%0.3f  Steps to go from %d to %d: %d   seconds/step:%0.3f  sample rate:%d" % ( 
			sr, sl, start, end, nb_steps, seconds_per_step, sample_rate )

                #print "%d, // %0.3f" % ((rate * SAMPLES_PER_SECOND) / 0x800, rate)

                # print "This decay needs to happen over %d samples." % nb_samples
                result.append(sample_rate)

print len(result)

print "int SUSTAIN_RATE[32][8] = {"

for x in range(len(result) / 8):
	array = ["%4d" % i for i in result[x * 8:x * 8 + 8]]
	string = ", ".join(array)
	print "\t{ %s }," % string

print "};"

