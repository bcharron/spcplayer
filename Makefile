CFLAGS=-Wall -ggdb
LDFLAGS=-lSDL

# For OSX
#LDFLAGS=`/opt/local/bin/sdl-config --libs`

all: spcplayer

spcplayer: spcplayer.o

spcplayer.o: spcplayer.c

clean:
	rm -f spcplayer *.o

test:
	./spcplayer srb-02.spc
