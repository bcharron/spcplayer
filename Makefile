CFLAGS=-Wall -ggdb
LDFLAGS=-lSDL

# For OSX
#LDFLAGS=`/opt/local/bin/sdl-config --libs`

all: spcplayer

spcplayer: spcplayer.o

spcplayer.o: spcplayer.c opcode_table.h

spc-disasm: spc-disasm.c

clean:
	rm -f spcplayer spc-disasm *.o

test:
	./spcplayer spc/srb-02.spc
