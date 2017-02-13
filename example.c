#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>
#include "SDL_console.h"

/* Forward declare our input handling function */
int input_function (const char *text, void *data, char **output);

int
main (int argc, char **argv)
{
    SDL_Window   *window = NULL;
    SDL_GLContext glContext = NULL;
    Console_tty  *tty = NULL;
    SDL_Event     e;

    if (argc < 3) {
        fprintf(stderr, "Usage: %s <monospaced font path> <font size>\n", 
                argv[0]);
        exit(1);
    }

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL Failed to init: %s\n", SDL_GetError());
        exit(1);
    }

    /* Create a window that is resizeable. SDL_Console can handle resizing. */
    window = SDL_CreateWindow("Console", 
        SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 
        640, 480, 
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_OPENGL);

    if (!window) {
        fprintf(stderr, "Window could not be created: %s\n", SDL_GetError());
        SDL_Quit();
        exit(1);
    }

    /* 
     * SDL Console is meant to be used with OpenGL. Mix OpenGL and SDL draw 
     * calls at your own peril.
     */
    glContext = SDL_GL_CreateContext(window);
    if (!glContext) {
        printf("%s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        exit(1);
    }

    tty = Console_Create(
            window,         /* SDL_Window */
            argv[1],        /* font path */
            atoi(argv[2]),  /* font size */
            SDLK_ESCAPE,    /* key that toggles console on/off */
            input_function, /* function that handles text input to console */
            NULL);          /* userdata to the above function */

    if (!tty) {
        fprintf(stderr, "Console could not init: %s\n", Console_GetError());
        SDL_GL_DeleteContext(glContext);
        SDL_DestroyWindow(window);
        SDL_Quit();
        exit(1);
    }

    while(1) { 
        while (SDL_PollEvent(&e)) {
            switch (e.type) {
            case SDL_QUIT:
                goto quit;
                break;
            }
        }

        glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        /* Draw your program here */

        if (Console_Draw(tty)) { /* handle drawing the console if toggled */
            fprintf(stderr, "%s\n", Console_GetError());
            /* handle fatal console error */
            goto quit;
        }

        SDL_GL_SwapWindow(window);
    }

quit:
    Console_Destroy(tty);
    SDL_GL_DeleteContext(glContext);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}

/* 
 * An input function which has three `commands': foo, bar, and baz.
 */
int
input_function (const char *text, void *data, char **output)
{
    /* simulate an absurd amount of text to test out the console. */
    if (strcmp(text, "foo") == 0) {
        Console_SetOutput(output, "Really long output! Lorem ipsum dolor sit amet, consectetur adipiscing elit. Sed tincidunt, odio quis pulvinar suscipit, dolor nibh lobortis massa, quis sollicitudin ipsum sapien nec leo. Donec id sem sapien. Quisque dignissim eget sem ac bibendum. Suspendisse aliquam est finibus tellus molestie faucibus. Vestibulum volutpat feugiat nulla ut pharetra. Etiam facilisis, nunc in ullamcorper tempus, velit ante molestie turpis, at aliquet orci odio in arcu. Aenean dignissim dolor libero, et rhoncus felis elementum hendrerit. Donec aliquam accumsan nunc, vitae tempor sem tristique non. Duis at velit libero. Fusce ac justo vel leo lacinia vehicula sed vel felis. Nullam lacus orci, faucibus eu dapibus nec, gravida quis dui. Fusce faucibus, eros eu dignissim pharetra, velit velit imperdiet urna, gravida commodo est arcu eget lectus. Nunc leo ipsum, maximus vel dictum sit amet, maximus vitae arcu. Donec suscipit elit nec dolor lobortis rhoncus.");
    }

    /* simulate what normal output might be like */
    if (strcmp(text, "bar") == 0) {
        Console_SetOutput(output, 
                "Short output that you'd expect on the console.");
    }

    /* simulate a fatal error */
    if (strcmp(text, "baz") == 0) {
        Console_SetOutput(output, "A fatal error!");
        return 1;
    }

    return 0;
}
