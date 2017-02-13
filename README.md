# SDL Console

This is a console written using OpenGL for SDL. I needed a console for debugging
and changing a running program and decided to roll my own.

SDL Console can handle window resizes, has an input history, and handles output
from a user-defined input function.

## Requirements

SDL Console requires [SDL2](https://www.libsdl.org/download-2.0.php), OpenGL
2.1+, and [Freetype](https://www.freetype.org/download.html).  Debian-based
distros can use the following command to get the required packages:

    # apt-get install libfreetype6-dev libsdl2-dev

SDL Console was written for use in OpenGL-only applications. Mixing this
library and SDL's draw calls, e.g.  `SDL_FillRect`, is not advised but may work
depending on the platform.

A monospace font is also required. I recommend 
[SourceCode-Pro](https://github.com/adobe-fonts/source-code-pro) but any
monospace font on your system will work.

## How to Use

An example program can be seen at `example.c`. The example can be compiled with 
`make example` which can then be passed various fonts and sizes to test, e.g.
`./example /path/to/font.ttf 18`.

#### General flow

The library's contract should be short and sweet because that's what I needed.
The basic flow of the contract is to 1) call `Console_Create` with the 
appropriate attributes, 2) call `Console_Draw` just before you flip buffers or
render your frames, 3) call `Console_Destroy` to cleanup.

Here's some mock-code that illustrates this flow:

    SDL_Window *window = program_init();
    Console_tty *tty;
    tty = Console_Create(
            window,         /* SDL_Window */
            "/path/to/monospaced-font.ttf", 
            18,             /* font size */
            SDLK_ESCAPE,    /* key that toggles console on/off */
            input_function, /* function that handles text input to console */
            NULL);          /* userdata to the above function */
    if (!tty) {
        fprintf(stderr, "Console created failed: %s\n", Console_GetError());
        /* handle error */
    }

    while (program_is_on) {
        program_input();
        program_draw();
        if (Console_Draw(tty)) {  /* handle drawing the console if toggled */
            fprintf(stderr, "%s\n", Console_GetError());
            /* handle fatal console error */
        }
        SDL_GL_SwapWindow(window);
    }

    Console_Destroy(tty);   /* cleanup the console */
    program_cleanup();

#### Input handling

The input function needs to be in the following form:

    int 
    input_function (const char *input_text, void *userdata, char **output)
    {
        if (strcmp(input_text, "foo") == 0)
            Console_SetOutput(output, "bar");
        return 0;
    }

`input_text` is null-terminated text from the console, `userdata' is userdata
passed  to `Console_Create` and `output` is output from the function to be 
displayed on the console.

Please use `Console_SetOutput` to set the output.  `Console_SetOutput` has two
arguments: the first is `output` above and the second is a null-terminated
string to be displayed on the console. *If no output needs to occur, then
simply do not set the output.*

The input function should return 0 in almost all cases -- even common errors --
with the output set appropriately to be displayed in the console. In the case
of a fatal error, still set the output normally but return 1. This error will
be caught from `Console_Draw` in the main loop.

#### Defaults

The default color for the font is white with no transparency and the default
color for the background is black at 10% (0.9f) transparency. These can be 
changed via `Console_SetFontColor` and `Console_SetBackgroundColor` 
respectively. They both accept a `Console_tty` pointer as the first argument
and then four floats -- r,g,b,a -- ranging from 0.0f to 1.0f.

## Installation

SDL Console can be used statically apart of your project by just copying 
`SDL_Console.h` and `SDL_Console.c` to your project folder and linking against
OpenGL, Freetype, and an appropriate math library. The `Makefile` shows an
example on how to do this. Protip: Freetype is weird so just use
`freetype-config` seen in the `Makefile`.

SDL Console can also be used as a shared library. Once you have the required 
packages simply enter `make lib` on your favorite console. Make sure your 
`LD_LIBRARY_PATH` (or your respective system's ld path) has
`libSDL_Console.so`'s directory.
