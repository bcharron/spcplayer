CFLAGS=-Wall -ggdb `/usr/local/bin/sdl2-config --cflags`
LDFLAGS=`/usr/local/bin/sdl2-config --libs`

# For OSX
#LDFLAGS=`/opt/local/bin/sdl-config --libs`

all: spcplayer spcdisasm buftest

spcplayer: spcplayer.o opcodes.o buf.o

buf.o: buf.c buf.h

buftest: buf.o

opcodes.o: opcodes.c opcodes.h

spcplayer.o: spcplayer.c opcodes.h

spcdisasm.o: spcdisasm.c

spcdisasm: spcdisasm.o opcodes.o

convert-opcode-table.o: convert-opcode-table.c opcodes.h opcodes.c

convert-opcode-table: convert-opcode-table.o opcodes.o

profile: spcplayer
	sample -wait -file spcplayer.profile.txt spcplayer &
	echo c | ./spcplayer srb-02.spc

clean:
	rm -f spcplayer spcdisasm *.o

dtest: spcdisasm
	./spcdisasm spc/srb-02.spc 65472 

test: spcplayer
	./spcplayer spc/srb-02.spc
