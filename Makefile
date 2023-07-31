cc=gcc
cflags=-Wall -g -ggdb --std=c99 $(shell pkg-config freetype2 --cflags)
ldflags=-lSDL2 -lGL -lm $(shell pkg-config freetype2 --libs)

all: example
example:
	$(cc) -o example example.c SDL_console.c $(cflags) $(ldflags) 
lib:
	$(cc) -fPIC -shared -o libSDL_console.so SDL_console.c $(cflags) $(ldflags)
