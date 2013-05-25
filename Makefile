LDFLAGS=-lX11 -lvdpau
CFLAGS=-g -Wall

all: h264_player

h264_player: h264_player.o

.PHONY = clean

clean:
	-rm -rf *.o h264_player
