CFLAGS=-Wall -ggdb
LDFLAGS=-lSDL

# For OSX
#LDFLAGS=`/opt/local/bin/sdl-config --libs`

all: spcplayer spcdisasm

spcplayer: spcplayer.o opcodes.o

opcodes.o: opcodes.c opcodes.h

spcplayer.o: spcplayer.c opcodes.h

spcdisasm.o: spcdisasm.c

spcdisasm: spcdisasm.o opcodes.o

clean:
	rm -f spcplayer spcdisasm *.o

test:
	./spcplayer spc/srb-02.spc
