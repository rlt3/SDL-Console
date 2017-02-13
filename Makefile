cc=gcc
cflags=-Wall -g -ggdb --std=c99 $(shell freetype-config --cflags)
ldflags=-lSDL2 -lGL -lm $(shell freetype-config --libs)

all: example
example:
	$(cc) -o example example.c SDL_console.c $(cflags) $(ldflags) 
lib:
	$(cc) -fPIC -shared -o libSDL_console.so SDL_console.c $(cflags) $(ldflags)
