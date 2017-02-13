#define _POSIX_C_SOURCE 200809L
#define GL_GLEXT_PROTOTYPES 1
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>
#include <SDL2/SDL.h>
#include <GL/gl.h>
#include <GL/glext.h>
#include <GL/glu.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include "SDL_console.h"

static const GLchar* _Console_vertex_source =
    "#version 130\n"
    "in vec4 vertex; // <vec2 pos, vec2 tex>\n"
    "out vec2 TexCoords;\n"
    "uniform mat4 projection;\n"
    "void main()\n"
    "{\n"
    "   gl_Position = projection * vec4(vertex.xy, 0.0, 1.0);\n"
    "   TexCoords = vertex.zw;\n"
    "}";
static const GLchar* _Console_frag_source =
    "#version 130\n"
    "in vec2 TexCoords;\n"
    "out vec4 outColor;\n"
    "uniform sampler2D text;\n"
    "uniform vec3 textColor;\n"
    "void main()\n"
    "{\n"
    "   vec4 sampled = vec4(1.0, 1.0, 1.0, texture2D(text, TexCoords).r);\n"
    "   outColor = vec4(textColor, 1.0) * sampled;\n"
    "}";

#define DEFAULT_PROMPT       "> "
#define DEFAULT_PROMPT_LEN   2
#define DEFAULT_LINE_LENGTH  128
#define DEFAULT_LINE_CHARS   256
#define CONSOLE_CHARS_LEN    128

static char _Console_errstr[1024] = {0};

#define Console_SetError(cs,es) \
    strcpy(_Console_errstr, cs); \
    strcat(_Console_errstr, es);

const char*
Console_GetError (void)
{
    return _Console_errstr;
}

const char *
FT_GetError (FT_Error err)
{
    #undef __FTERRORS_H__
    #define FT_ERRORDEF(e,v,s)   case e: return s;
    #define FT_ERROR_START_LIST  switch (err) {
    #define FT_ERROR_END_LIST    }
    #include FT_ERRORS_H
    return "(Uknown Error)";

}

typedef struct _Console_Font {
    FT_Library ft;
    FT_Face face;
    GLint font_size;
    GLint char_width;
    GLfloat advance;
    GLfloat line_height;
    GLfloat baseline;
} Console_Font;

typedef struct _SDL_console_line {
    char *input;
    char *output;
    int len;
    GLfloat w;
    GLfloat h;
    GLuint texture;
    struct _SDL_console_line *next;
    struct _SDL_console_line *prev;
} Console_Line;

struct _SDL_console_tty {
    Console_Font font;

    const char *prompt;
    int prompt_len;

    SDL_Window *window;
    int window_width;
    int window_height;

    Console_Color bg_color;
    Console_Color font_color;

    GLuint VAO;
    GLuint VBO;
    GLuint shader_prog;
    GLuint vert_shader;
    GLuint frag_shader;
    /* 1x1 textures which hold the opacity values for the cursor & bg color */
    GLuint cursor_texture;
    GLuint bg_texture;

    /* doubly linked-list of lines */
    Console_Line *lines_head;
    Console_Line *lines_tail;
    Console_Line *curr_line;

    int num_lines;  /* current number of lines */
    int max_lines;  /* max numbers of lines allowed */
    int max_input;  /* max text input length of a line (what user writes) */
    int wrap_len;   /* the number of characters when line should wrap */
    int cursor;     /* position of cursor within curr_line */

    bool rebuild_line;

    /* the function to which the input text is passed */
    Console_InputFunction input_func;
    /* this data is passed along with the input text to the input_func */
    void *input_func_data;
    /* status of the Console (drawing and handling input or not) */
    bool status;
    /* the key to watch that, when pressed, triggers the status above */
    SDL_Keycode trigger_key;
    /* mutex because our event watches can be called from different threads */
    SDL_mutex *mutex;
};

/*
 * Updates the texture given with the input and prompt and also the output, if
 * not null. This *will* changed the values w & h with the width and height of
 * the texture as the input, prompt, and output change lengths to vary the size
 * of the texture.
 */
int
_Console_update_line_texture (
        Console_tty *tty,
        Console_Line *line,
        Console_Line *texture_line)
{
    assert(tty);
    assert(line);
    assert(texture_line);

    Console_Font *font = &tty->font;
    GLfloat advance = font->char_width;
    GLfloat x = 0.0f;
    GLfloat y = 0.0f;
    float offset = 0.0f;
    int len = 0;
    int i;

    if (line->output) {
        /* +1 for the newline character inserted as sentinel */
        len = tty->prompt_len + line->len + 1 + strlen(line->output);
        /* again +1.0f for newline */
        offset = 1.0f;
    } else {
        len = tty->prompt_len + line->len;
    }

    line->w = tty->wrap_len * font->char_width;
    line->h = 
        (ceil((float)len / (float)tty->wrap_len) + offset) * font->line_height;

    /* set of `empty' pixels to clear texture */
    unsigned char empty[(int)(line->w * line->h)];
    memset(empty, 0, (int)(line->w * line->h));

    /* string buffer to output characters from */
    char str[len];
    memset(str, 0, len);
    strcpy(str, tty->prompt);
    strcat(str, line->input);

    /* insert newline sentinel and then output */
    if (line->output) {
        strcat(str, "\n");
        strcat(str, line->output);
    }

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture_line->texture);

    /* Resize the texture if needed and set any attributes */
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, 
            line->w, line->h, 0, GL_RED, GL_UNSIGNED_BYTE, 0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    /* Clear the texture */
    glTexSubImage2D(GL_TEXTURE_2D, 
            0, 0, 0, line->w, line->h, GL_RED, GL_UNSIGNED_BYTE, empty);

    for (i = 0; i < len; i++) {
        if (FT_Load_Char(font->face, str[i], FT_LOAD_RENDER))
            continue;

        GLfloat bearingY = font->face->glyph->bitmap_top;

        if (x + advance > line->w || str[i] == '\n') {
            y += font->line_height;
            x = 0.0f;

            if (str[i] == '\n')
                continue;
        }

        if (str[i] == ' ')
            goto next;

        /*
         * Every character has a different bearing. To account for that we use
         * the current line (y) and add in the line height as a buffer. In that
         * buffer can each character be placed at different y values so they
         * all appear in the same baseline.
         */
        GLfloat ypos = y + font->line_height - bearingY - font->baseline - 1.0f;

        /*
         * We also make sure to use SubImage here because we're actually 
         * appending to the previously created texture.
         */
        glTexSubImage2D(
            GL_TEXTURE_2D, 
            0, 
            x, 
            ypos,
            font->face->glyph->bitmap.width,
            font->face->glyph->bitmap.rows,
            GL_RED,
            GL_UNSIGNED_BYTE, 
            font->face->glyph->bitmap.buffer
        );

    next:
        x += advance;
    }

    glBindTexture(GL_TEXTURE_2D, 0);
    return 0;
}

void
_Console_render_background (Console_tty *tty)
{
    GLfloat xpos = 0.0f;
    GLfloat ypos = 0.0f;
    GLfloat ww   = tty->window_width;
    GLfloat wh   = tty->window_height;
    GLfloat vertices[6][4] = {
        { xpos,      ypos + wh, 0.0f, 0.0f},
        { xpos,      ypos,      0.0f, 1.0f},
        { xpos + ww, ypos,      1.0f, 1.0f},
                                         
        { xpos,      ypos + wh, 0.0f, 0.0f},
        { xpos + ww, ypos,      1.0f, 1.0f},
        { xpos + ww, ypos + wh, 1.0f, 0.0f}
    };
    glUniform3f(glGetUniformLocation(tty->shader_prog, "textColor"),
            tty->bg_color.r, tty->bg_color.g, tty->bg_color.b);
    glBindTexture(GL_TEXTURE_2D, tty->bg_texture);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
    glDrawArrays(GL_TRIANGLES, 0, 6);
}

void
_Console_render_cursor (Console_tty *tty)
{
    /* cursor's position */
    int cursor_len  = tty->cursor + tty->prompt_len;
    GLfloat lh = tty->font.line_height;
    GLfloat cw = tty->font.char_width;
    /* the cursor's length within the line wrapped by max line characters */
    GLfloat cx = (float)((cursor_len % tty->wrap_len) * tty->font.char_width);
    /* The number of total lines minus the lines of cursor times line height */
    GLfloat cy = ((tty->lines_head->h / lh) - 
                 (float)((cursor_len / tty->wrap_len) + 1)) * lh;
    GLfloat cursor_vert[6][4] = {
        { cx,      cy + lh, 0.0f, 0.0f},
        { cx,      cy,      0.0f, 1.0f},
        { cx + cw, cy,      1.0f, 1.0f},

        { cx,      cy + lh, 0.0f, 0.0f},
        { cx + cw, cy,      1.0f, 1.0f},
        { cx + cw, cy + lh, 1.0f, 0.0f}
    };
    /* Draw the cursor */
    glUniform3f(glGetUniformLocation(tty->shader_prog, "textColor"),
            tty->font_color.r, tty->font_color.g, tty->font_color.b);
    glBindTexture(GL_TEXTURE_2D, tty->cursor_texture);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(cursor_vert), cursor_vert);
    glDrawArrays(GL_TRIANGLES, 0, 6);
}

void
_Console_render_lines (Console_tty *tty)
{
    Console_Line *l = tty->curr_line;
    GLfloat xpos = 0.0f;
    GLfloat ypos = 0.0f;
    glUniform3f(glGetUniformLocation(tty->shader_prog, "textColor"),
            1.0f, 1.0f, 1.0f);
    for (l = tty->lines_head; l; l = l->next) {
        GLfloat vertices[6][4] = {
            { xpos,        ypos + l->h, 0.0f, 0.0f},
            { xpos,        ypos,        0.0f, 1.0f},
            { xpos + l->w, ypos,        1.0f, 1.0f},
                                             
            { xpos,        ypos + l->h, 0.0f, 0.0f},
            { xpos + l->w, ypos,        1.0f, 1.0f},
            { xpos + l->w, ypos + l->h, 1.0f, 0.0f}
        };
        glBindTexture(GL_TEXTURE_2D, l->texture);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        ypos += l->h;
    }
}

int
Console_Render (Console_tty *tty)
{
    assert(tty);

    if (tty->rebuild_line) {
        tty->rebuild_line = false;
        if (_Console_update_line_texture(tty, tty->curr_line, tty->lines_head))
            return 1;
    }

    /* set all options, programs, and buffers to draw our lines and cursor */
    glEnable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(tty->shader_prog);
    glBindVertexArray(tty->VAO);
    glBindBuffer(GL_ARRAY_BUFFER, tty->VBO);
    glActiveTexture(GL_TEXTURE0);

    /* set buffer size */
    glBufferData(GL_ARRAY_BUFFER, 
            sizeof(GLfloat) * 6 * 4, NULL, GL_DYNAMIC_DRAW);

    _Console_render_background(tty);
    _Console_render_lines(tty);
    _Console_render_cursor(tty);

    /* unset all of the set we set  */
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
    glUseProgram(0);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);

    return 0;
}

/*
 * Set the current line. We can go UP (next) or DOWN (previous) through the 
 * lines. This function essentially acts as a history viewer. This function
 * will skip lines with zero length. The cursor is always set to the length of
 * the line's input.
 */
enum Console_Line_Dir { LINE_UP, LINE_DOWN }; 
void
Console_SetCurrLine (Console_tty *tty, enum Console_Line_Dir dir)
{
    Console_Line *line;

    for (line = (dir == LINE_UP ? tty->curr_line->next : tty->curr_line->prev);
         line != NULL;
         line = (dir == LINE_UP ? line->next : line->prev))
    {
        if (line == tty->lines_head || line->len > 0)
            break;
    }

    if (line == NULL)
        return;

    tty->curr_line = line;
    tty->curr_line->len = line->len;
    tty->cursor = line->len;
    tty->rebuild_line = true;
}

/*
 * Create a new line and set it to be the head. This function will 
 * automatically cycle-out lines if the number of lines has reached the max.
 * If this function returns 0, the tty->lines_head will be a new line and 
 * tty->lines_head->next is previous line.
 */
int
_Console_create_line (Console_tty *tty)
{
    assert(tty);
    Console_Line *line = NULL;

    line = malloc(sizeof(*line));
    if (!line) {
        Console_SetError("Not enough memory to create line!", "");
        return 1;
    }

    line->input = malloc(sizeof(*line->input) * tty->max_input);
    if (!line->input) {
        Console_SetError("Not enough memory to create line text!", "");
        free(line);
        return 1;
    }

    memset(line->input, 0, tty->max_input);
    glGenTextures(1, &line->texture);
    line->output = NULL;
    line->len = 0;
    line->w = 0;
    line->h = 0;

    /* insert into the doubly-linked list */
    if (tty->lines_head == NULL) {
        line->next = NULL;
        line->prev = NULL;
        tty->lines_head = line;
        tty->lines_tail = line;
        tty->num_lines  = 0;
    } else {
        line->prev = NULL;
        line->next = tty->lines_head;
        tty->lines_head->prev = line;
        tty->lines_head = line;
    }

    /* When the list is too long, start chopping the tail off each new line */
    if (tty->num_lines == tty->max_lines) {
        tty->lines_tail = tty->lines_tail->prev;
        free(tty->lines_tail->next);
        tty->lines_tail->next = NULL;
    } else {
        tty->num_lines++;
    }

    /* make sure the current line becomes the new line */
    tty->curr_line = tty->lines_head;
    tty->cursor = 0;
    tty->rebuild_line = true;

    return 0;
}

/*
 * When a newline occurs, we use the current line's text as input to the input
 * function given in Console(). We then render any output to that line's
 * texture which will be the final time that texture is updated (excluding
 * screen resizes and font changes).
 */
int
Console_NewLine (Console_tty *tty, 
                 Console_InputFunction input_func,
                 void *input_func_data)
{
    char *output = NULL;

    /* Use the text from the current line as input to the function given */
    if (tty->curr_line && tty->curr_line->len > 0 && input_func) {
        if (input_func(tty->curr_line->input, input_func_data, &output)) {
            Console_SetError("Console input function failed: ", output);
            free(output);
            return 1;
        }
        tty->curr_line->output = output;

        /* Then render the line a final time with the output */
        if (_Console_update_line_texture(tty, tty->curr_line, tty->lines_head))
            return 1;
    }

    /*
     * If the newline came from history, copy that command to the current
     * head to keep history consistent.
     */
    if (tty->lines_head != tty->curr_line) {
        strcpy(tty->lines_head->input, tty->curr_line->input);
        tty->lines_head->len = tty->curr_line->len;
    }

    /* Finally, actually create a new line */
    if (_Console_create_line(tty))
        return 1;

    return 0;
}

void
_Console_destroy_ft (Console_tty *tty)
{
    assert(tty);
    FT_Done_Face(tty->font.face);
    FT_Done_FreeType(tty->font.ft);
}

/*
 * Initialize the font.
 * TODO-FEATURE: extract the New_Face and Done_Face functions so we can change
 * fonts at run time.
 */
int
_Console_init_ft (Console_tty* tty, const char *font_path, const int font_size)
{
    assert(tty);

    FT_Library ft;
    FT_Face face;
    FT_Error e;

    if ((e = FT_Init_FreeType(&ft))) {
        Console_SetError("Freetype failed to init: ", FT_GetError(e));
        return 1;
    }
    if ((e = FT_New_Face(ft, font_path, 0, &face))) {
        Console_SetError("Freetype failed open font: ", FT_GetError(e));
        FT_Done_FreeType(ft);
        return 1;
    }

    tty->font.ft   = ft;
    tty->font.face = face;

    if (!FT_IS_FIXED_WIDTH(face)) {
        Console_SetError("Font must be fixed width (monospace)!", "");
        _Console_destroy_ft(tty);
        return 1;
    }

    if (!FT_IS_SCALABLE(face)) {
        Console_SetError("Font isn't scalable!", "");
        _Console_destroy_ft(tty);
        return 1;
    }

    FT_Set_Pixel_Sizes(face, 0, font_size);

    if ((e = FT_Load_Glyph(face, FT_Get_Char_Index(face, 'm'), 
                    FT_LOAD_RENDER))) {
        Console_SetError("Loading glyphs failed: ", FT_GetError(e));
        _Console_destroy_ft(tty);
        return 1;
    }

    /* `>> 6' adjusts values which are based at 1/64th of screen pixel size */
    tty->font.face = face;
    tty->font.ft = ft;
    tty->font.font_size = font_size;
    tty->font.advance = (face->glyph->metrics.horiAdvance >> 6);
    tty->font.char_width = 
        (face->glyph->metrics.horiBearingX + face->glyph->metrics.width) >> 6;
    tty->font.line_height = 
          (FT_MulFix(face->ascender, face->size->metrics.y_scale) >> 6)
        - (FT_MulFix(face->descender, face->size->metrics.y_scale) >> 6)
        + 1;
    tty->font.baseline = abs(face->descender) * font_size / face->units_per_EM;

    return 0;
}

void
_Console_destroy_gl (Console_tty *tty)
{
    assert(tty);
    Console_Line *line; 
    for (line = tty->lines_head; line != NULL; line = line->next)
        glDeleteTextures(1, &line->texture);
	glDeleteTextures(1, &tty->cursor_texture);
	glDeleteTextures(1, &tty->bg_texture);
    glDeleteShader(tty->vert_shader);
    glDeleteShader(tty->frag_shader);
    glDeleteProgram(tty->shader_prog);
	glDeleteBuffers(1, &tty->VBO);
	glDeleteBuffers(1, &tty->VAO);
}

/*
 * Get the window's size and all the vars that are associated with the window
 * size.
 */
void
_Console_set_window_size (Console_tty *tty)
{
    assert(tty);
    assert(tty->window);
    assert(tty->shader_prog > 0);

    SDL_GetWindowSize(tty->window, &tty->window_width, &tty->window_height);
    /* wrap len needs to be updated before updating textures */
    tty->wrap_len = 
        roundf((float)tty->window_width / (float)tty->font.char_width);

    /* 
     * Setup 2D projection matrix for vertex shader.
     * left, right, bottom, top
     */
    GLfloat l = 0.0f;
    GLfloat r = (GLfloat)tty->window_width;
    GLfloat b = 0.0f;
    GLfloat t = (GLfloat)tty->window_height;
    GLfloat orthoMatrix[4*4] = {
        2.0f / (r - l),   0.0f,             0.0f, 0.0f,
        0.0f,             2.0f / (t - b),   0.0f, 0.0f,
        0.0f,             0.0f,            -1.0f, 0.0f,
        -(r + l)/(r - l), -(t + b)/(t - b), 0.0f, 1.0f,
    };
    glUniformMatrix4fv(glGetUniformLocation(tty->shader_prog, "projection"),
            1, GL_FALSE, orthoMatrix);

    Console_Line *li;
    for (li = tty->lines_head; li != NULL; li = li->next)
        _Console_update_line_texture(tty, li, li);
}

/*
 * Load all of the OpenGL specific aspects of the tty. This includes the 
 * cursor's texture. The only things it doesn't handle is the texture of each
 * line (handled by Console_NewLine) and the cursor's texture 
 * (_Console_init_cursor). _Console_destroy_gl cleans up both of those.
 */
int
_Console_init_gl (Console_tty *tty, SDL_Window *window)
{
    assert(tty);
    assert(window);

    GLuint VAO;
    GLuint VBO;
    GLuint shader_prog;
    GLuint vert_shader;
    GLuint frag_shader;

    GLint  maxlength;
    GLint  status;
    GLint  posAttrib;

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
            SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    /*SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);*/

    glGenVertexArrays(1, &VAO);
	glGenBuffers(1, &VBO);

#define _Console_m_compile_shader(s,src,t) \
    s = glCreateShader(t); \
    glShaderSource(s, 1, &src, NULL); \
    glCompileShader(s); \
    glGetShaderiv(s, GL_COMPILE_STATUS, &status); \
    if (status != GL_TRUE) { \
        char buffer[512]; \
        glGetShaderInfoLog(s, 512, NULL, buffer); \
        Console_SetError("Shader failed to compile: ", buffer); \
        return 1; \
    }

    _Console_m_compile_shader(vert_shader,
            _Console_vertex_source, GL_VERTEX_SHADER);
    _Console_m_compile_shader(frag_shader,
            _Console_frag_source, GL_FRAGMENT_SHADER);
    shader_prog = glCreateProgram();

    /* setup values so they can be used to in destroy function if needed */
    tty->VAO = VAO;
    tty->VBO = VBO;
    tty->shader_prog = shader_prog;
    tty->vert_shader = vert_shader;
    tty->frag_shader = frag_shader;
    tty->window      = window;

    /* actually link shaders */
    glAttachShader(shader_prog, vert_shader);
    glAttachShader(shader_prog, frag_shader);
    glBindFragDataLocation(shader_prog, 0, "outColor");
    glLinkProgram(shader_prog);

    /* and check status of link */
    glGetProgramiv(shader_prog, GL_LINK_STATUS, &status);
    if (status != GL_TRUE) {
        glGetProgramiv(shader_prog, GL_INFO_LOG_LENGTH, &maxlength);
        if (maxlength > 0) {
            char buffer[maxlength];
            glGetProgramInfoLog(shader_prog, maxlength, NULL, buffer);
            Console_SetError("OpenGL shader failed to link: ", buffer);
            _Console_destroy_gl(tty);
            return 1;
        }
    }

    glUseProgram(tty->shader_prog);

    /* Setup the buffer and attribute buffers so we can set values */
    glBindVertexArray(VAO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);

    /* reserve size of buffer (one texture at a time) */
    glBufferData(GL_ARRAY_BUFFER, 
            sizeof(GLfloat) * 6 * 4, NULL, GL_DYNAMIC_DRAW);

    /* set the offset of the position in the buffer */
    posAttrib = glGetAttribLocation(shader_prog, "vertex");
    glVertexAttribPointer(posAttrib, 4, GL_FLOAT, GL_FALSE, 
            4 * sizeof(GLfloat), 0);

    glEnableVertexAttribArray(posAttrib);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    /* Set color for fragment shader */
    glUniform3f(glGetUniformLocation(shader_prog, "textColor"),
            1.0f, 1.0f, 1.0f);

    if (SDL_GL_SetSwapInterval(1) < 0)
        fprintf(stderr, "Warning: SwapInterval could not be set: %s\n", 
                SDL_GetError());

    return 0;
}

/*
 * Create a 1x1 texture which is used for its transparency value
 */
void
_Console_create_trans_texture (
        Console_tty *tty, 
        GLuint *texture,
        unsigned char transparency)
{
    assert(tty);
    /* figure out the dimensions of the cursor and create pixel */
    unsigned char pixel[1] = { transparency };
    /* Generate the texture */
    glGenTextures(1, texture);
    glBindTexture(GL_TEXTURE_2D, *texture);
    glTexImage2D(
            GL_TEXTURE_2D, 0, GL_RED, 1, 1, 0, GL_RED, GL_UNSIGNED_BYTE, 0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    /* fill it with the pixel */
    glTexSubImage2D(
            GL_TEXTURE_2D, 0, 0, 0, 1, 1, GL_RED, GL_UNSIGNED_BYTE, pixel);
    glBindTexture(GL_TEXTURE_2D, 0);
}

/*
 * Inserts string `c' into the given text array. This will increase the text's
 * length by then of `c'.
 * Returns 1 if inserting will be past max, else inserts char and returns 0.
 * Expects len to be zero-indexed length of text array.
 */
int
_Console_insert_text (char *text, const char *c, int index, int max, int *len)
{
    int c_len = strlen(c);
    int i;
    if (*len + c_len > max)
        return 1;
    for (i = *len; i >= index; i--)
        text[i + c_len] = text[i];
    for (i = 0; i < c_len; i++)
        text[index + i] = c[i];
    *len += c_len;
    return 0;
}

/*
 * Handle inserting input depending on where the cursor is within the current
 * line.
 */
void
_Console_get_input (Console_tty *tty, const char *input)
{
    assert(tty);
    assert(input);

    int input_length = strlen(input);
    if (tty->curr_line->len + input_length >= tty->max_input)
        return;

    /* if cursor is at end of line, it's a simple concatenation */
    if (tty->cursor == tty->curr_line->len) {
        strncat(tty->curr_line->input, input, input_length);
        tty->curr_line->len += input_length;
    } else {
    /* else insert text into line at cursor's index */
        _Console_insert_text(tty->curr_line->input, input, tty->cursor,
                tty->max_input, &tty->curr_line->len);
    }
    tty->cursor += input_length;
    tty->rebuild_line = true;
}

/* 
 * Shifts a text left, removing a single character from the text array. Text
 * array must have a length greater than 0. This will decrement the length of
 * the array by 1.
 */
int
_Console_shift_text (char *text, int index, int *len)
{
    int i;
    if (*len <= 0)
        return 1;
    for (i = index; i < *len - 1; i++)
        text[i] = text[i + 1];
    *len -= 1;
    return 0;
}

/*
 * Handle removing input with backspace or delete. We currenty only remove 
 * input one character at a time, unlike inserting input.
 */
void
_Console_remove_input (Console_tty *tty)
{
    assert(tty);
    if (tty->cursor == 0 || tty->curr_line->len == 0)
        return;

    /* if cursor is at end of line just mark end of line at cursor */
    if (tty->curr_line->len == tty->cursor) {
        tty->curr_line->len -= 1;
        tty->cursor -= 1;
        tty->curr_line->input[tty->cursor] = '\0';
    } else {
    /* else shift the text from cursor left by one character */
        tty->cursor -= 1;
        _Console_shift_text(tty->curr_line->input, tty->cursor,
                &tty->curr_line->len);
    }
    tty->rebuild_line = true;
}

int
Console_InputWatch (void *data, SDL_Event *e)
{
    assert(data);
    Console_tty *tty = data;

    if (SDL_LockMutex(tty->mutex) != 0) {
        Console_SetError("Mutex failed to lock!", SDL_GetError());
        return 0;
    }

    if (!tty->status)
        goto unlock;

    switch (e->type) {
    case SDL_KEYDOWN:
        switch (e->key.keysym.sym) {
        case SDLK_BACKSPACE:
            _Console_remove_input(tty);
            break;

        case SDLK_RETURN:
            Console_NewLine(tty, tty->input_func, tty->input_func_data);
            break;

        /* copy */
        case SDLK_c:
            if (SDL_GetModState() & KMOD_CTRL) {
                /* SDL_SetClipboardText(tty->curr_line->input) */
            }
            break;

        /* paste */
        case SDLK_v:
            if (SDL_GetModState() & KMOD_CTRL) {
                /* Console_GetInput(tty, SDL_GetClipboardText()); */
            }
            break;

        case SDLK_UP:
            Console_SetCurrLine(tty, LINE_UP);
            break;

        case SDLK_DOWN:
            Console_SetCurrLine(tty, LINE_DOWN);
            break;

        case SDLK_LEFT:
            if (tty->cursor > 0) {
                tty->cursor--;
                tty->rebuild_line = true;
            }
            break;

        case SDLK_RIGHT:
            if (tty->cursor < tty->max_input && 
                    tty->cursor < tty->curr_line->len) {
                tty->cursor++;
                tty->rebuild_line = true;
            }
            break;
        }
        break;

    case SDL_TEXTINPUT:
        _Console_get_input(tty, e->text.text);
        break;
    }

unlock:
    SDL_UnlockMutex(tty->mutex);
    return 0;
}

/*
 * Watch for the trigger to toggle having the console displayed and handling
 * input or off and not handling input.
 */
int
Console_TriggerWatch (void *data, SDL_Event *e)
{
    assert(data);
    Console_tty *tty = data;

    if (SDL_LockMutex(tty->mutex) != 0) {
        Console_SetError("Mutex failed to lock!", SDL_GetError());
        return 0;
    }

    /* watch for window size changes to update our console behind-the-scenes */
    if (e->type == SDL_WINDOWEVENT && 
        e->window.event == SDL_WINDOWEVENT_RESIZED) {
        _Console_set_window_size(tty);
    }

    if (e->type == SDL_KEYDOWN && e->key.keysym.sym == tty->trigger_key) {
        if (tty->status) {
            SDL_DelEventWatch(Console_InputWatch, tty);
            SDL_StopTextInput();
            tty->status = false;
        } else {
            SDL_StartTextInput();
            SDL_AddEventWatch(Console_InputWatch, tty);
            tty->status = true;
        }
    }

    SDL_UnlockMutex(tty->mutex);
    return 0; /* return is ignored */
}

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
                void *input_func_data)
{
    Console_tty *tty = malloc(sizeof(*tty));

    if (!tty) {
        Console_SetError("Not enough memory to create console!", "");
        goto exit;
    }

    tty->window       = NULL;
    tty->lines_head   = NULL;
    tty->lines_tail   = NULL;
    tty->curr_line    = NULL;
    tty->rebuild_line = true;
    tty->cursor       = 0;
    tty->status       = false;
    tty->prompt       = DEFAULT_PROMPT;
    tty->prompt_len   = DEFAULT_PROMPT_LEN;
    tty->max_lines    = DEFAULT_LINE_LENGTH;
    tty->max_input    = DEFAULT_LINE_CHARS;
    tty->trigger_key  = trigger_key;
    tty->input_func   = input_func;
    tty->input_func_data = input_func_data;
    tty->bg_color     = (Console_Color) { 0.0f, 0.0f, 0.0f, 0.9f };
    tty->font_color   = (Console_Color) { 1.0f, 1.0f, 1.0f, 1.0f };

    tty->mutex = SDL_CreateMutex();
    if (!tty->mutex) {
        Console_SetError("Failed to create mutex: ", SDL_GetError());
        free(tty);
        tty = NULL;
        goto exit;
    }

    /*
     * TODO: Nearly Done. This is the final stretch:
     *   - handle background color
     *   - handle text color
     */

    if (_Console_init_ft(tty, font_path, font_size)) {
        free(tty);
        tty = NULL;
        goto exit;
    }

    if (_Console_init_gl(tty, window)) {
        _Console_destroy_ft(tty);
        free(tty);
        tty = NULL;
        goto exit;
    }

    if (_Console_create_line(tty)) {
        _Console_destroy_ft(tty);
        _Console_destroy_gl(tty);
        free(tty);
        tty = NULL;
        goto exit;
    }

    /* handle info that needs both freetype & opengl */
    _Console_create_trans_texture(tty, &tty->cursor_texture, 255 * 0.75f);
    _Console_create_trans_texture(tty, &tty->bg_texture, 255*tty->bg_color.a);
    _Console_set_window_size(tty);

    SDL_AddEventWatch(Console_TriggerWatch, tty);

exit:
    return tty;
}

/*
 * Handle drawing the console if it is toggled.
 * Returns 1 on an error, 0 otherwise.
 */
int
Console_Draw (Console_tty *tty)
{
    int ret = 0;

    if (SDL_LockMutex(tty->mutex) != 0) {
        Console_SetError("Mutex failed to lock!", SDL_GetError());
        ret = 1;
        goto exit;
    }

    if (!tty->status)
        goto unlock;

    /* if something's been written to the error string */
    if ( _Console_errstr[0] != '\0') {
        ret = 1;
        goto unlock;
    }

    if (Console_Render(tty))
        ret = 1;

unlock:
    SDL_UnlockMutex(tty->mutex);
exit:
    return ret;
}

/*
 * In the `input_func`, this function handles memory for output and should
 * be used instead of malloc, realloc, etc.
 */
void
Console_SetOutput (char **out, const char *s)
{
    *out = strdup(s);
}

/*
 * Set the background color of the console.
 * Default is 0.0f, 0.0f, 0.0f, 0.90f.
 */
void
Console_SetBackgroundColor (Console_tty *tty, Console_Color c)
{
    tty->bg_color = c;
    _Console_create_trans_texture(tty, &tty->bg_texture, 255*tty->bg_color.a);
}

/*
 * Handle drawing the console if it is toggled.
 * Returns 1 on an error, 0 otherwise.
 */
void
Console_SetFontColor (Console_tty *tty, Console_Color c)
{
    tty->font_color = c;
}

void
Console_Destroy (Console_tty* tty)
{
    assert(tty);
    Console_Line *line; 

    /* 
     * Make sure to lock the mutex and stop the threads before destroying the
     * mutex and freeing any data the threads may be using.
     */
    if (SDL_LockMutex(tty->mutex) != 0)
        Console_SetError("Mutex failed to lock!", SDL_GetError());
    SDL_DelEventWatch(Console_TriggerWatch, tty);
    if (tty->status)
        SDL_DelEventWatch(Console_InputWatch, tty);
    SDL_UnlockMutex(tty->mutex);
    SDL_DestroyMutex(tty->mutex);

    _Console_destroy_ft(tty);
    _Console_destroy_gl(tty);

    for (line = tty->lines_head; line != NULL; line = line->next) {
        free(line->input);
        if (line->output)
            free(line->output);
        if (line->prev != NULL)
            free(line->prev);
        if (line->next == NULL) {
            free(line);
            break;
        }
    }

    free(tty);
}
