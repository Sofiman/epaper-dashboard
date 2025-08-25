CFLAGS=-Wall -Wextra -g3 -fsanitize=address -fsanitize=undefined $(shell pkg-config --cflags sdl2) -O0 -fPIC -DBITUI_ROTATION
CPPFLAGS=-I../include -I../../bitui/include -I../../sht4x/include -I.
LDFLAGS=$(shell pkg-config --libs sdl2) -fsanitize=address -fsanitize=undefined -lm

all: main libgui.so

main: main.o ../gui.o ../../bitui/bitui.o

libgui.so: CFLAGS=-Wall -Wextra -g3 -O0 -fPIC -DBITUI_ROTATION
libgui.so: LDFLAGS=-shared
libgui.so: ../gui.c ../../bitui/bitui.o
	$(CC) $(CFLAGS) $(CPPFLAGS) $(LDFLAGS) -o $@ $^

hotreload: libgui.so
	pkill -USR1 main

.PHONY: hotreload
