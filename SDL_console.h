#ifndef SDL_CONSOLE
#define SDL_CONSOLE

struct _SDL_console_tty;
typedef struct _SDL_console_tty Console_tty;
typedef int (*Console_InputFunction)(const char *, void*, char **);

typedef struct _console_color {
    float r, g, b, a;
} Console_Color;

/*
 * Create the console. 
 * The console will load the font at `font_path'. The font path *must* be a
 * monospaced or fixed-width. 
 * The `trigger_key' is the keyboard key that toggles the console on and off.
 * `input_func' is the function that uses the input to the console and 
 * `input_func_data' is userdata given to that function.
 * Returns NULL on error.
 */
Console_tty* 
Console_Create (SDL_Window *window, 
                const char *font_path, 
                const int font_size,
                SDL_Keycode trigger_key,
                Console_InputFunction input_func,
                void *input_func_data);

/*
 * In the `input_func`, this function handles memory for output and should
 * be used instead of malloc, realloc, etc.
 */
void
Console_SetOutput (char **out, const char *s);

/*
 * Set the background color of the console.
 * Default is 0.0f, 0.0f, 0.0f, 0.90f.
 */
void
Console_SetBackgroundColor (Console_tty *tty, Console_Color);

/*
 * Set the font color.
 * Default is 1.0f, 1.0f, 1.0f, 1.0f.
 */
void
Console_SetFontColor (Console_tty *tty, Console_Color);

/*
 * Handle drawing the console if it is toggled.
 * Returns 1 on an error, 0 otherwise.
 */
int
Console_Draw (Console_tty *tty);

/*
 * Clean up the console.
 */
void
Console_Destroy (Console_tty* tty);

/*
 * Get the last error.
 */
const char*
Console_GetError (void);

#endif
