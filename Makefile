LDFLAGS=-lX11 -lvdpau
CFLAGS=-g -Wall
MESA_DIR=../mesa
GALLIUM_DIR=$(MESA_DIR)/src/gallium

all: h264_player bsp_test decode_frame

h264_player: h264_player.o
bsp_test: bsp_test.o
	$(CC) -o $@ $^ $(LDFLAGS) -ldrm -ldrm_nouveau -lxcb -lxcb-dri2

decode_frame: decode_frame.o
	$(CC) -o $@ $^ $(LDFLAGS) -ldrm -ldrm_nouveau -lxcb -lxcb-dri2

bsp_test.o: bsp_test.c
	$(CC) -c $^ $(CFLAGS) -I$(GALLIUM_DIR)/drivers -I$(GALLIUM_DIR)/include -I$(MESA_DIR)/include -I$(GALLIUM_DIR)/auxiliary -I/usr/include/libdrm

decode_frame.o: decode_frame.c
	$(CC) -c $^ $(CFLAGS) -I$(GALLIUM_DIR)/drivers -I$(GALLIUM_DIR)/include -I$(MESA_DIR)/include -I$(GALLIUM_DIR)/auxiliary -I/usr/include/libdrm

.PHONY = clean

clean:
	-rm -rf *.o h264_player bsp_test decode_frame
