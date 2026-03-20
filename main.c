#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/wait.h>

#if defined(__APPLE__)
#include <util.h>
#else
#include <pty.h>
#endif

#include "raylib.h"
#include <ghostty/vt.h>

// Embed the font file directly into the binary at compile time using C23
// #embed so we don't need to locate it at runtime.
static const unsigned char font_jetbrains_mono[] = {
    #embed "fonts/JetBrainsMono-Regular.ttf"
};

// ---------------------------------------------------------------------------
// PTY helpers
// ---------------------------------------------------------------------------

// Spawn /bin/sh in a new pseudo-terminal.
//
// Creates a pty pair via forkpty(), sets the initial window size, execs the
// shell in the child, and puts the master fd into non-blocking mode so we
// can poll it each frame without stalling the render loop.
//
// Returns the master fd on success (>= 0) and stores the child pid in
// *child_out.  Returns -1 on failure.
static int pty_spawn(pid_t *child_out, uint16_t cols, uint16_t rows)
{
    int pty_fd;
    struct winsize ws = { .ws_row = rows, .ws_col = cols };

    // forkpty() combines openpty + fork + login_tty into one call.
    // In the child it sets up the slave side as stdin/stdout/stderr.
    pid_t child = forkpty(&pty_fd, NULL, NULL, &ws);
    if (child < 0) {
        perror("forkpty");
        return -1;
    }
    if (child == 0) {
        // Child process — replace ourselves with the shell.
        // TERM tells programs what escape sequences we understand.
        setenv("TERM", "xterm-256color", 1);
        execl("/bin/sh", "sh", NULL);
        _exit(127); // execl only returns on error
    }

    // Parent — make the master fd non-blocking so read() returns EAGAIN
    // instead of blocking when there's no data, letting us poll each frame.
    int flags = fcntl(pty_fd, F_GETFL);
    fcntl(pty_fd, F_SETFL, flags | O_NONBLOCK);

    *child_out = child;
    return pty_fd;
}

// Drain all available output from the pty master and feed it into the
// ghostty terminal.  The terminal's VT parser will process any escape
// sequences and update its internal screen/cursor/style state.
//
// Because the fd is non-blocking, read() returns -1 with EAGAIN once
// the kernel buffer is empty, at which point we stop.
static void pty_read(int pty_fd, GhosttyTerminal terminal)
{
    uint8_t buf[4096];
    for (;;) {
        ssize_t n = read(pty_fd, buf, sizeof(buf));
        if (n > 0)
            ghostty_terminal_vt_write(terminal, buf, (size_t)n);
        else
            break;
    }
}

// ---------------------------------------------------------------------------
// Input handling
// ---------------------------------------------------------------------------

// Map a raylib key constant to a GhosttyKey code.
// Returns GHOSTTY_KEY_UNIDENTIFIED for keys we don't handle.
static GhosttyKey raylib_key_to_ghostty(int rl_key)
{
    // Letters — raylib KEY_A..KEY_Z are contiguous, and so are
    // GHOSTTY_KEY_A..GHOSTTY_KEY_Z.
    if (rl_key >= KEY_A && rl_key <= KEY_Z)
        return GHOSTTY_KEY_A + (rl_key - KEY_A);

    // Digits — raylib KEY_ZERO..KEY_NINE are contiguous.
    if (rl_key >= KEY_ZERO && rl_key <= KEY_NINE)
        return GHOSTTY_KEY_DIGIT_0 + (rl_key - KEY_ZERO);

    // Function keys — raylib KEY_F1..KEY_F12 are contiguous.
    if (rl_key >= KEY_F1 && rl_key <= KEY_F12)
        return GHOSTTY_KEY_F1 + (rl_key - KEY_F1);

    switch (rl_key) {
    case KEY_SPACE:       return GHOSTTY_KEY_SPACE;
    case KEY_ENTER:       return GHOSTTY_KEY_ENTER;
    case KEY_TAB:         return GHOSTTY_KEY_TAB;
    case KEY_BACKSPACE:   return GHOSTTY_KEY_BACKSPACE;
    case KEY_DELETE:      return GHOSTTY_KEY_DELETE;
    case KEY_ESCAPE:      return GHOSTTY_KEY_ESCAPE;
    case KEY_UP:          return GHOSTTY_KEY_ARROW_UP;
    case KEY_DOWN:        return GHOSTTY_KEY_ARROW_DOWN;
    case KEY_LEFT:        return GHOSTTY_KEY_ARROW_LEFT;
    case KEY_RIGHT:       return GHOSTTY_KEY_ARROW_RIGHT;
    case KEY_HOME:        return GHOSTTY_KEY_HOME;
    case KEY_END:         return GHOSTTY_KEY_END;
    case KEY_PAGE_UP:     return GHOSTTY_KEY_PAGE_UP;
    case KEY_PAGE_DOWN:   return GHOSTTY_KEY_PAGE_DOWN;
    case KEY_INSERT:      return GHOSTTY_KEY_INSERT;
    case KEY_MINUS:       return GHOSTTY_KEY_MINUS;
    case KEY_EQUAL:       return GHOSTTY_KEY_EQUAL;
    case KEY_LEFT_BRACKET:  return GHOSTTY_KEY_BRACKET_LEFT;
    case KEY_RIGHT_BRACKET: return GHOSTTY_KEY_BRACKET_RIGHT;
    case KEY_BACKSLASH:   return GHOSTTY_KEY_BACKSLASH;
    case KEY_SEMICOLON:   return GHOSTTY_KEY_SEMICOLON;
    case KEY_APOSTROPHE:  return GHOSTTY_KEY_QUOTE;
    case KEY_COMMA:       return GHOSTTY_KEY_COMMA;
    case KEY_PERIOD:      return GHOSTTY_KEY_PERIOD;
    case KEY_SLASH:       return GHOSTTY_KEY_SLASH;
    case KEY_GRAVE:       return GHOSTTY_KEY_BACKQUOTE;
    default:              return GHOSTTY_KEY_UNIDENTIFIED;
    }
}

// Build a GhosttyMods bitmask from the current raylib modifier key state.
static GhosttyMods get_ghostty_mods(void)
{
    GhosttyMods mods = 0;
    if (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT))
        mods |= GHOSTTY_MODS_SHIFT;
    if (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL))
        mods |= GHOSTTY_MODS_CTRL;
    if (IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT))
        mods |= GHOSTTY_MODS_ALT;
    if (IsKeyDown(KEY_LEFT_SUPER) || IsKeyDown(KEY_RIGHT_SUPER))
        mods |= GHOSTTY_MODS_SUPER;
    return mods;
}

// Return the unshifted Unicode codepoint for a raylib key, i.e. the
// character the key produces with no modifiers on a US layout.  The
// Kitty keyboard protocol requires this to identify keys.  Returns 0
// for keys that don't have a natural codepoint (arrows, F-keys, etc.).
static uint32_t raylib_key_unshifted_codepoint(int rl_key)
{
    if (rl_key >= KEY_A && rl_key <= KEY_Z)
        return 'a' + (uint32_t)(rl_key - KEY_A);
    if (rl_key >= KEY_ZERO && rl_key <= KEY_NINE)
        return '0' + (uint32_t)(rl_key - KEY_ZERO);

    switch (rl_key) {
    case KEY_SPACE:          return ' ';
    case KEY_MINUS:          return '-';
    case KEY_EQUAL:          return '=';
    case KEY_LEFT_BRACKET:   return '[';
    case KEY_RIGHT_BRACKET:  return ']';
    case KEY_BACKSLASH:      return '\\';
    case KEY_SEMICOLON:      return ';';
    case KEY_APOSTROPHE:     return '\'';
    case KEY_COMMA:          return ',';
    case KEY_PERIOD:         return '.';
    case KEY_SLASH:          return '/';
    case KEY_GRAVE:          return '`';
    default:                 return 0;
    }
}

// Encode a single Unicode codepoint into a UTF-8 byte buffer.
// Returns the number of bytes written (1–4).  Used by handle_input to
// supply the event's UTF-8 text from GetCharPressed().
static int input_utf8_encode(int cp, char out[4])
{
    if (cp < 0x80) {
        out[0] = (char)cp;
        return 1;
    } else if (cp < 0x800) {
        out[0] = (char)(0xC0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    } else if (cp < 0x10000) {
        out[0] = (char)(0xE0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    } else {
        out[0] = (char)(0xF0 | (cp >> 18));
        out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
        out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[3] = (char)(0x80 | (cp & 0x3F));
        return 4;
    }
}

// Poll raylib for keyboard events and use the libghostty key encoder
// to produce the correct VT escape sequences, which are then written
// to the pty.  The encoder respects terminal modes (cursor key
// application mode, Kitty keyboard protocol, etc.) so we don't need
// to maintain our own escape-sequence tables.
static void handle_input(int pty_fd, GhosttyKeyEncoder encoder,
                         GhosttyKeyEvent event, GhosttyTerminal terminal)
{
    // Sync encoder options from the terminal so mode changes (e.g.
    // application cursor keys, Kitty keyboard protocol) are honoured.
    ghostty_key_encoder_setopt_from_terminal(encoder, terminal);

    // Drain printable characters from raylib's input queue.  We collect
    // them into a single UTF-8 buffer so the encoder can attach text
    // to the key event.
    char char_utf8[64];
    int char_utf8_len = 0;
    int ch;
    while ((ch = GetCharPressed()) != 0) {
        char u8[4];
        int n = input_utf8_encode(ch, u8);
        if (char_utf8_len + n < (int)sizeof(char_utf8)) {
            memcpy(&char_utf8[char_utf8_len], u8, n);
            char_utf8_len += n;
        }
    }

    // All raylib keys we want to check for press/repeat events.
    // Letters and digits are handled via ranges; everything else is
    // enumerated explicitly.
    static const int special_keys[] = {
        KEY_SPACE, KEY_ENTER, KEY_TAB, KEY_BACKSPACE, KEY_DELETE,
        KEY_ESCAPE, KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT,
        KEY_HOME, KEY_END, KEY_PAGE_UP, KEY_PAGE_DOWN, KEY_INSERT,
        KEY_MINUS, KEY_EQUAL, KEY_LEFT_BRACKET, KEY_RIGHT_BRACKET,
        KEY_BACKSLASH, KEY_SEMICOLON, KEY_APOSTROPHE, KEY_COMMA,
        KEY_PERIOD, KEY_SLASH, KEY_GRAVE,
        KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6,
        KEY_F7, KEY_F8, KEY_F9, KEY_F10, KEY_F11, KEY_F12,
    };

    // Build the set of raylib keys to scan: letters + digits + specials.
    int keys_to_check[26 + 10 + sizeof(special_keys) / sizeof(special_keys[0])];
    int num_keys = 0;
    for (int k = KEY_A; k <= KEY_Z; k++)
        keys_to_check[num_keys++] = k;
    for (int k = KEY_ZERO; k <= KEY_NINE; k++)
        keys_to_check[num_keys++] = k;
    for (size_t i = 0; i < sizeof(special_keys) / sizeof(special_keys[0]); i++)
        keys_to_check[num_keys++] = special_keys[i];

    GhosttyMods mods = get_ghostty_mods();

    for (int i = 0; i < num_keys; i++) {
        int rl_key = keys_to_check[i];
        bool pressed  = IsKeyPressed(rl_key);
        bool repeated = IsKeyPressedRepeat(rl_key);
        bool released = IsKeyReleased(rl_key);
        if (!pressed && !repeated && !released)
            continue;

        GhosttyKey gkey = raylib_key_to_ghostty(rl_key);
        if (gkey == GHOSTTY_KEY_UNIDENTIFIED)
            continue;

        GhosttyKeyAction action = released  ? GHOSTTY_KEY_ACTION_RELEASE
                                : pressed   ? GHOSTTY_KEY_ACTION_PRESS
                                            : GHOSTTY_KEY_ACTION_REPEAT;

        ghostty_key_event_set_key(event, gkey);
        ghostty_key_event_set_action(event, action);
        ghostty_key_event_set_mods(event, mods);

        // The unshifted codepoint is the character the key produces
        // with no modifiers.  The Kitty protocol needs it to identify
        // keys independent of the current shift state.
        uint32_t ucp = raylib_key_unshifted_codepoint(rl_key);
        ghostty_key_event_set_unshifted_codepoint(event, ucp);

        // Consumed mods are modifiers the platform's text input
        // already accounted for when producing the UTF-8 text.
        // For printable keys, shift is consumed (it turns 'a' → 'A').
        // For non-printable keys nothing is consumed.
        GhosttyMods consumed = 0;
        if (ucp != 0 && (mods & GHOSTTY_MODS_SHIFT))
            consumed |= GHOSTTY_MODS_SHIFT;
        ghostty_key_event_set_consumed_mods(event, consumed);

        // Attach any UTF-8 text that raylib produced for this frame.
        // For unmodified printable keys this is the character itself;
        // for special keys or ctrl combos there's typically no text.
        // Release events never carry text.
        if (char_utf8_len > 0 && !released) {
            ghostty_key_event_set_utf8(event, char_utf8, (size_t)char_utf8_len);
            // Only attach the text to the first key event this frame
            // to avoid duplicating it.
            char_utf8_len = 0;
        } else {
            ghostty_key_event_set_utf8(event, NULL, 0);
        }

        char buf[128];
        size_t written = 0;
        GhosttyResult res = ghostty_key_encoder_encode(
            encoder, event, buf, sizeof(buf), &written);
        if (res == GHOSTTY_SUCCESS && written > 0)
            write(pty_fd, buf, written);
    }
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------

// Resolve a style color to an RGB value using the render-state palette.
// Falls back to the given default if the color is unset.
static GhosttyColorRgb resolve_color(GhosttyStyleColor color,
                                     const GhosttyRenderStateColors *colors,
                                     GhosttyColorRgb fallback)
{
    switch (color.tag) {
    case GHOSTTY_STYLE_COLOR_RGB:     return color.value.rgb;
    case GHOSTTY_STYLE_COLOR_PALETTE: return colors->palette[color.value.palette];
    default:                          return fallback;
    }
}

// Encode a single Unicode codepoint into a UTF-8 byte buffer.
// Returns the number of bytes written (1–4).
static int utf8_encode(uint32_t cp, char out[4])
{
    if (cp < 0x80) {
        out[0] = (char)cp;
        return 1;
    } else if (cp < 0x800) {
        out[0] = (char)(0xC0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    } else if (cp < 0x10000) {
        out[0] = (char)(0xE0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    } else {
        out[0] = (char)(0xF0 | (cp >> 18));
        out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
        out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[3] = (char)(0x80 | (cp & 0x3F));
        return 4;
    }
}

// Render the current terminal screen using the RenderState API.
//
// For each row/cell we read the grapheme codepoints and the cell's style,
// resolve foreground/background colors via the palette, and draw each
// character individually with DrawTextEx.  This supports per-cell colors
// from SGR sequences (bold, 256-color, 24-bit RGB, etc.).
//
// cell_width and cell_height are the measured dimensions of a single
// monospace glyph at the current font size, in screen (logical) pixels.
// font_size is the logical font size (before DPI scaling).
static void render_terminal(GhosttyRenderState render_state,
                            GhosttyRenderStateRowIterator row_iter,
                            GhosttyRenderStateRowCells cells,
                            Font font,
                            int cell_width, int cell_height,
                            int font_size)
{
    // Grab colors (palette, default fg/bg) from the render state so we
    // can resolve palette-indexed cell colors.
    GhosttyRenderStateColors colors = GHOSTTY_INIT_SIZED(GhosttyRenderStateColors);
    if (ghostty_render_state_colors_get(render_state, &colors) != GHOSTTY_SUCCESS)
        return;

    // The bare terminal has no config, so the default fg/bg may both be
    // (0,0,0).  Fall back to standard terminal defaults (white on black)
    // so text is actually visible.
    // https://github.com/ghostty-org/ghostty/issues/11704
    if (colors.foreground.r == 0 && colors.foreground.g == 0 && colors.foreground.b == 0 &&
        colors.background.r == 0 && colors.background.g == 0 && colors.background.b == 0) {
        colors.foreground = (GhosttyColorRgb){ 255, 255, 255 };
    }

    // Populate the row iterator from the current render state snapshot.
    if (ghostty_render_state_get(render_state,
            GHOSTTY_RENDER_STATE_DATA_ROW_ITERATOR, &row_iter) != GHOSTTY_SUCCESS)
        return;

    // Small padding from the window edges.
    const int pad = 4;
    int y = pad;

    while (ghostty_render_state_row_iterator_next(row_iter)) {
        // Get the cells for this row (reuses the same cells handle).
        if (ghostty_render_state_row_get(row_iter,
                GHOSTTY_RENDER_STATE_ROW_DATA_CELLS, &cells) != GHOSTTY_SUCCESS)
            continue;

        int x = pad;

        while (ghostty_render_state_row_cells_next(cells)) {
            // How many codepoints make up the grapheme? 0 = empty cell.
            uint32_t grapheme_len = 0;
            ghostty_render_state_row_cells_get(cells,
                GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_GRAPHEMES_LEN, &grapheme_len);

            if (grapheme_len == 0) {
                // The cell has no text, but it might be a bg-color-only cell.
                // Check the content tag on the raw cell to find out.
                GhosttyCell raw_cell = 0;
                ghostty_render_state_row_cells_get(cells,
                    GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_RAW, &raw_cell);

                GhosttyCellContentTag content_tag = 0;
                ghostty_cell_get(raw_cell, GHOSTTY_CELL_DATA_CONTENT_TAG, &content_tag);

                if (content_tag == GHOSTTY_CELL_CONTENT_BG_COLOR_PALETTE) {
                    // Palette index is stored in bits [2:9] of the packed cell.
                    uint8_t palette_idx = (uint8_t)((raw_cell >> 2) & 0xFF);
                    GhosttyColorRgb bg = colors.palette[palette_idx];
                    DrawRectangle(x, y, cell_width, cell_height,
                                  (Color){ bg.r, bg.g, bg.b, 255 });
                } else if (content_tag == GHOSTTY_CELL_CONTENT_BG_COLOR_RGB) {
                    // RGB is stored in bits [2:25] of the packed cell (r, g, b
                    // each 8 bits, little-endian packed struct order).
                    uint8_t r = (uint8_t)((raw_cell >> 2) & 0xFF);
                    uint8_t g = (uint8_t)((raw_cell >> 10) & 0xFF);
                    uint8_t b = (uint8_t)((raw_cell >> 18) & 0xFF);
                    DrawRectangle(x, y, cell_width, cell_height,
                                  (Color){ r, g, b, 255 });
                }

                x += cell_width;
                continue;
            }

            // Read the grapheme codepoints.
            uint32_t codepoints[16];
            uint32_t len = grapheme_len < 16 ? grapheme_len : 16;
            ghostty_render_state_row_cells_get(cells,
                GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_GRAPHEMES_BUF, codepoints);

            // Build a UTF-8 string from the grapheme codepoints.
            char text[64];
            int pos = 0;
            for (uint32_t i = 0; i < len && pos < 60; i++) {
                char u8[4];
                int n = utf8_encode(codepoints[i], u8);
                memcpy(&text[pos], u8, n);
                pos += n;
            }
            text[pos] = '\0';

            // Read the style and resolve the foreground color.
            GhosttyStyle style = GHOSTTY_INIT_SIZED(GhosttyStyle);
            ghostty_render_state_row_cells_get(cells,
                GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_STYLE, &style);

            GhosttyColorRgb fg = resolve_color(style.fg_color, &colors, colors.foreground);
            Color ray_fg = { fg.r, fg.g, fg.b, 255 };

            // Draw a background rectangle if the cell has a non-default bg.
            if (style.bg_color.tag != GHOSTTY_STYLE_COLOR_NONE) {
                GhosttyColorRgb bg = resolve_color(style.bg_color, &colors, colors.background);
                DrawRectangle(x, y, cell_width, cell_height, (Color){ bg.r, bg.g, bg.b, 255 });
            }

            DrawTextEx(font, text, (Vector2){x, y}, font_size, 0, ray_fg);
            x += cell_width;
        }

        // Clear per-row dirty flag after rendering it.
        bool clean = false;
        ghostty_render_state_row_set(row_iter,
            GHOSTTY_RENDER_STATE_ROW_OPTION_DIRTY, &clean);

        y += cell_height;
    }

    // Draw the cursor.
    bool cursor_visible = false;
    ghostty_render_state_get(render_state,
        GHOSTTY_RENDER_STATE_DATA_CURSOR_VISIBLE, &cursor_visible);
    bool cursor_in_viewport = false;
    ghostty_render_state_get(render_state,
        GHOSTTY_RENDER_STATE_DATA_CURSOR_VIEWPORT_HAS_VALUE, &cursor_in_viewport);

    if (cursor_visible && cursor_in_viewport) {
        uint16_t cx = 0, cy = 0;
        ghostty_render_state_get(render_state,
            GHOSTTY_RENDER_STATE_DATA_CURSOR_VIEWPORT_X, &cx);
        ghostty_render_state_get(render_state,
            GHOSTTY_RENDER_STATE_DATA_CURSOR_VIEWPORT_Y, &cy);

        // Draw the cursor using the foreground color (or explicit cursor
        // color if the terminal set one).
        GhosttyColorRgb cur_rgb = colors.foreground;
        if (colors.cursor_has_value)
            cur_rgb = colors.cursor;
        int cur_x = pad + cx * cell_width;
        int cur_y = pad + cy * cell_height;
        DrawRectangle(cur_x, cur_y, cell_width, cell_height, (Color){ cur_rgb.r, cur_rgb.g, cur_rgb.b, 128 });
    }

    // Reset global dirty state so the next update reports changes accurately.
    GhosttyRenderStateDirty clean_state = GHOSTTY_RENDER_STATE_DIRTY_FALSE;
    ghostty_render_state_set(render_state,
        GHOSTTY_RENDER_STATE_OPTION_DIRTY, &clean_state);
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main(void)
{
    // Desired font size in logical (screen) points — the actual texture
    // will be rasterized at font_size * dpi_scale so glyphs stay crisp on
    // HiDPI / Retina displays.
    const int font_size = 16;

    // Enable HiDPI *before* creating the window so raylib can set up the
    // framebuffer at the native display resolution.
    SetConfigFlags(FLAG_WINDOW_HIGHDPI);

    // Initialize window
    InitWindow(800, 600, "ghostling");
    SetWindowState(FLAG_WINDOW_RESIZABLE);
    SetTargetFPS(60);

    // Query the DPI scale so we can rasterize the font at the true pixel
    // size.  On a 2× Retina display this returns {2.0, 2.0}.
    Vector2 dpi_scale = GetWindowScaleDPI();

    // Load the embedded monospace font at the native pixel size so every
    // glyph maps 1:1 to screen pixels — no texture scaling, no blur.
    int font_size_px = (int)(font_size * dpi_scale.y);
    Font mono_font = LoadFontFromMemory(".ttf", font_jetbrains_mono,
                         (int)sizeof(font_jetbrains_mono), font_size_px, NULL, 0);

    // Use bilinear filtering; the texture is already at native resolution
    // so there's no magnification blur, and this avoids jagged edges when
    // fractional positioning occurs.
    SetTextureFilter(mono_font.texture, TEXTURE_FILTER_BILINEAR);

    // Measure a representative glyph to derive the monospace cell size.
    // MeasureTextEx returns logical-pixel dimensions (already accounts for
    // the font's internal scaling), so divide by the DPI scale to get the
    // screen-space cell size we use for layout.
    Vector2 glyph_size = MeasureTextEx(mono_font, "M", font_size_px, 0);
    int cell_width  = (int)(glyph_size.x / dpi_scale.x);
    int cell_height = (int)(glyph_size.y / dpi_scale.y);

    // Small padding from window edges — must match the constant in
    // render_terminal().
    const int pad = 4;

    // Compute the initial grid from the window size and measured cell
    // metrics.
    int scr_w = GetScreenWidth();
    int scr_h = GetScreenHeight();
    uint16_t term_cols = (uint16_t)((scr_w - 2 * pad) / cell_width);
    uint16_t term_rows = (uint16_t)((scr_h - 2 * pad) / cell_height);
    if (term_cols < 1) term_cols = 1;
    if (term_rows < 1) term_rows = 1;

    // Create a ghostty virtual terminal with the computed grid and 1000
    // lines of scrollback.  This holds all the parsed screen state (cells,
    // cursor, styles, modes) but knows nothing about the pty or the window.
    GhosttyTerminal terminal;
    GhosttyTerminalOptions opts = { .cols = term_cols, .rows = term_rows, .max_scrollback = 1000 };
    GhosttyResult err = ghostty_terminal_new(NULL, &terminal, opts);
    assert(err == GHOSTTY_SUCCESS);

    // Spawn a child shell connected to a pseudo-terminal.  The master fd
    // is what we read/write; the child's stdin/stdout/stderr are wired to
    // the slave side.
    pid_t child;
    int pty_fd = pty_spawn(&child, term_cols, term_rows);
    if (pty_fd < 0)
        return 1;

    // Create the key encoder and a reusable key event for input handling.
    // The encoder translates key events into the correct VT escape
    // sequences, respecting terminal modes like application cursor keys
    // and the Kitty keyboard protocol.
    GhosttyKeyEncoder key_encoder = NULL;
    err = ghostty_key_encoder_new(NULL, &key_encoder);
    assert(err == GHOSTTY_SUCCESS);

    GhosttyKeyEvent key_event = NULL;
    err = ghostty_key_event_new(NULL, &key_event);
    assert(err == GHOSTTY_SUCCESS);

    // Create the render state and its reusable iterator/cells handles once
    // up front.  These are updated each frame rather than recreated.
    GhosttyRenderState render_state = NULL;
    err = ghostty_render_state_new(NULL, &render_state);
    assert(err == GHOSTTY_SUCCESS);

    GhosttyRenderStateRowIterator row_iter = NULL;
    err = ghostty_render_state_row_iterator_new(NULL, &row_iter);
    assert(err == GHOSTTY_SUCCESS);

    GhosttyRenderStateRowCells row_cells = NULL;
    err = ghostty_render_state_row_cells_new(NULL, &row_cells);
    assert(err == GHOSTTY_SUCCESS);

    // Track window size so we only recalculate the grid on actual changes.
    int prev_width = scr_w;
    int prev_height = scr_h;

    // Each frame: handle resize → read pty → process input → render.
    while (!WindowShouldClose()) {
        // Recalculate grid dimensions when the window is resized.
        // We update both the ghostty terminal (so it reflows text) and the
        // pty's winsize (so the child shell knows about the new size and
        // can send SIGWINCH to its foreground process group).
        if (IsWindowResized()) {
            int w = GetScreenWidth();
            int h = GetScreenHeight();
            if (w != prev_width || h != prev_height) {
                int cols = (w - 2 * pad) / cell_width;
                int rows = (h - 2 * pad) / cell_height;
                if (cols < 1) cols = 1;
                if (rows < 1) rows = 1;
                term_cols = (uint16_t)cols;
                term_rows = (uint16_t)rows;
                ghostty_terminal_resize(terminal, term_cols, term_rows);
                struct winsize new_ws = { .ws_row = term_rows, .ws_col = term_cols };
                ioctl(pty_fd, TIOCSWINSZ, &new_ws);
                prev_width = w;
                prev_height = h;
            }
        }

        // Drain any pending output from the shell and update terminal state.
        pty_read(pty_fd, terminal);

        // Forward keyboard input to the shell.
        handle_input(pty_fd, key_encoder, key_event, terminal);

        // Snapshot the terminal state into our render state.  This is the
        // only point where we need access to the terminal; after this the
        // render state owns everything we need to draw the frame.
        ghostty_render_state_update(render_state, terminal);

        // Get the terminal's background color from the render state.
        GhosttyRenderStateColors bg_colors = GHOSTTY_INIT_SIZED(GhosttyRenderStateColors);
        ghostty_render_state_colors_get(render_state, &bg_colors);
        Color win_bg = { bg_colors.background.r, bg_colors.background.g, bg_colors.background.b, 255 };

        // Draw the current terminal screen.
        BeginDrawing();
        ClearBackground(win_bg);
        render_terminal(render_state, row_iter, row_cells, mono_font,
                        cell_width, cell_height, font_size);
        EndDrawing();
    }

    // Cleanup
    UnloadFont(mono_font);
    CloseWindow();
    close(pty_fd);
    kill(child, SIGHUP);    // signal the child shell to exit
    waitpid(child, NULL, 0); // reap the child to avoid a zombie
    ghostty_key_event_free(key_event);
    ghostty_key_encoder_free(key_encoder);
    ghostty_render_state_row_cells_free(row_cells);
    ghostty_render_state_row_iterator_free(row_iter);
    ghostty_render_state_free(render_state);
    ghostty_terminal_free(terminal);
    return 0;
}
