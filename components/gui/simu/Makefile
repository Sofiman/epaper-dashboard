CFLAGS=-Wall -Wextra -g -fsanitize=address -fsanitize=undefined $(shell pkg-config --cflags sdl2) -Og
CPPFLAGS=-I../include -I../../bitui/include -I.
LDFLAGS=$(shell pkg-config --libs sdl2) -fsanitize=address -fsanitize=undefined

main: main.o ../gui.o ../../bitui/bitui.o
