#include <stdint.h>

#include "nano.h"
#include "terminal.h"
#include "fs.h"
#include "shell.h"

/*
 * ------------------------------------------------------------
 * Buffer sizing
 *
 * FS_MAX_FILE_SIZE is 32 * 512 = 16384 bytes (see fs.h). The
 * line grid below is sized so that the worst case serialized
 * buffer (every line full, one '\n' per line) never exceeds
 * that, leaving headroom for the terminating '\0'.
 *
 * NANO_MAX_LINES * (NANO_MAX_LINE_LEN + 1) <= FS_MAX_FILE_SIZE
 *      200       *          80            = 16000  <= 16384
 * ------------------------------------------------------------
 */
#define NANO_MAX_LINES       200
#define NANO_MAX_LINE_LEN    79
#define NANO_FILENAME_LEN    48

#define NANO_SCREEN_COLS     80
#define NANO_SCREEN_ROWS     25

/*
 * Row 0            : title bar
 * Rows 1..22        : editing area (22 visible lines)
 * Row 23            : status / message line
 * Row 24            : prompt line (used during save)
 */
#define NANO_TEXT_TOP_ROW     1
#define NANO_TEXT_VISIBLE_ROWS 22
#define NANO_STATUS_ROW       23
#define NANO_PROMPT_ROW       24

static char lines[NANO_MAX_LINES][NANO_MAX_LINE_LEN + 1];
static uint32_t line_length[NANO_MAX_LINES];
static uint32_t line_count = 0;

static uint32_t cursor_line = 0;
static uint32_t cursor_col = 0;

/*
 * First line index shown at the top of the editing area,
 * for vertical scrolling.
 */
static uint32_t view_top = 0;

static char current_filename[NANO_FILENAME_LEN];
static int dirty = 0;

static int active = 0;

/*
 * Save-prompt state.
 */
static int prompt_active = 0;
static char prompt_buffer[NANO_FILENAME_LEN];
static uint32_t prompt_length = 0;
static uint32_t prompt_cursor = 0;

/*
 * ------------------------------------------------------------
 * Small helpers
 * ------------------------------------------------------------
 */

static void mem_zero(void* ptr, uint32_t size)
{
    uint8_t* p = (uint8_t*)ptr;

    while (size--)
    {
        *p++ = 0;
    }
}

static void str_copy_bounded(
    char* dst,
    const char* src,
    uint32_t max_len
)
{
    uint32_t i = 0;

    while (src[i] != '\0' && i < max_len)
    {
        dst[i] = src[i];
        i++;
    }

    dst[i] = '\0';
}

static uint32_t str_len(const char* s)
{
    uint32_t n = 0;

    while (s[n])
    {
        n++;
    }

    return n;
}

/*
 * ------------------------------------------------------------
 * Buffer management
 * ------------------------------------------------------------
 */

static void buffer_reset(void)
{
    for (uint32_t i = 0; i < NANO_MAX_LINES; i++)
    {
        line_length[i] = 0;
        lines[i][0] = '\0';
    }

    line_count = 1;
    cursor_line = 0;
    cursor_col = 0;
    view_top = 0;
    dirty = 0;
}

/*
 * Loads a file into the line buffer. Splits on '\n'.
 * If the file does not exist, starts with a single empty line
 * (a fresh, unsaved buffer) rather than failing.
 */
static void buffer_load(const char* filename)
{
    buffer_reset();

    int fd = fs_open(filename);

    if (fd < 0)
    {
        return;
    }

    static char raw[FS_MAX_FILE_SIZE];

    int n = fs_read(fd, raw, sizeof(raw));

    fs_close(fd);

    if (n <= 0)
    {
        return;
    }

    uint32_t line = 0;
    uint32_t col = 0;

    for (int i = 0; i < n; i++)
    {
        char c = raw[i];

        if (c == '\n')
        {
            line_length[line] = col;
            lines[line][col] = '\0';

            line++;
            col = 0;

            if (line >= NANO_MAX_LINES)
            {
                break;
            }

            continue;
        }

        if (col < NANO_MAX_LINE_LEN)
        {
            lines[line][col] = c;
            col++;
        }
    }

    /*
     * Close out whatever line we were mid-way through, unless
     * the file happened to end exactly on a newline.
     */
    if (col > 0 || line == 0)
    {
        line_length[line] = col;
        lines[line][col] = '\0';
        line++;
    }

    if (line == 0)
    {
        line = 1;
    }

    line_count = line;
    dirty = 0;
}

/*
 * Serializes the line buffer into a flat '\n'-joined blob and
 * writes it to disk under `filename`, replacing any existing
 * file of that name (fs_write only ever appends at the current
 * handle position, so a true "truncate" means delete + recreate).
 */
static int buffer_save(const char* filename)
{
    static char raw[FS_MAX_FILE_SIZE];

    uint32_t written = 0;

    for (uint32_t i = 0; i < line_count; i++)
    {
        uint32_t len = line_length[i];

        if (written + len + 1 > sizeof(raw))
        {
            break;
        }

        for (uint32_t c = 0; c < len; c++)
        {
            raw[written++] = lines[i][c];
        }

        /*
         * Separate every line with '\n', including the last,
         * to match how buffer_load splits on '\n'.
         */
        raw[written++] = '\n';
    }

    if (fs_exists(filename))
    {
        if (!fs_delete(filename))
        {
            return 0;
        }
    }

    if (!fs_create(filename))
    {
        return 0;
    }

    int fd = fs_open(filename);

    if (fd < 0)
    {
        return 0;
    }

    int result = fs_write(fd, raw, written);

    fs_close(fd);

    if (result < 0 || (uint32_t)result != written)
    {
        return 0;
    }

    dirty = 0;

    return 1;
}

/*
 * ------------------------------------------------------------
 * Rendering
 * ------------------------------------------------------------
 */

static void draw_clear_row(uint8_t row)
{
    for (uint8_t x = 0; x < NANO_SCREEN_COLS; x++)
    {
        terminal_put_at(x, row, ' ');
    }
}

static void draw_title_bar(void)
{
    draw_clear_row(0);

    const char* title = "SpectreOS nano  ";

    uint8_t x = 0;

    for (uint32_t i = 0; title[i] && x < NANO_SCREEN_COLS; i++)
    {
        terminal_put_at(x, 0, title[i]);
        x++;
    }

    for (uint32_t i = 0;
         current_filename[i] && x < NANO_SCREEN_COLS;
         i++)
    {
        terminal_put_at(x, 0, current_filename[i]);
        x++;
    }

    if (dirty && x < NANO_SCREEN_COLS)
    {
        terminal_put_at(x, 0, '*');
        x++;
    }
}

static void draw_status_bar(void)
{
    draw_clear_row(NANO_STATUS_ROW);

    const char* help =
        "^O Save (then Enter)   ^X Exit";

    uint8_t x = 0;

    for (uint32_t i = 0; help[i] && x < NANO_SCREEN_COLS; i++)
    {
        terminal_put_at(x, NANO_STATUS_ROW, help[i]);
        x++;
    }
}

static void draw_text_area(void)
{
    for (uint32_t row = 0; row < NANO_TEXT_VISIBLE_ROWS; row++)
    {
        uint8_t screen_row =
            (uint8_t)(NANO_TEXT_TOP_ROW + row);

        draw_clear_row(screen_row);

        uint32_t line_index = view_top + row;

        if (line_index >= line_count)
        {
            continue;
        }

        uint32_t len = line_length[line_index];

        for (uint32_t col = 0;
             col < len && col < NANO_SCREEN_COLS;
             col++)
        {
            terminal_put_at(
                (uint8_t)col,
                screen_row,
                lines[line_index][col]
            );
        }
    }
}

static void draw_prompt_bar(void)
{
    draw_clear_row(NANO_PROMPT_ROW);

    const char* label = "Save as: ";

    uint8_t x = 0;

    for (uint32_t i = 0; label[i] && x < NANO_SCREEN_COLS; i++)
    {
        terminal_put_at(x, NANO_PROMPT_ROW, label[i]);
        x++;
    }

    for (uint32_t i = 0;
         i < prompt_length && x < NANO_SCREEN_COLS;
         i++)
    {
        terminal_put_at(x, NANO_PROMPT_ROW, prompt_buffer[i]);
        x++;
    }
}

static void update_cursor_position(void)
{
    if (prompt_active)
    {
        uint8_t x =
            (uint8_t)(9 + prompt_cursor);

        if (x >= NANO_SCREEN_COLS)
        {
            x = NANO_SCREEN_COLS - 1;
        }

        terminal_set_cursor(x, NANO_PROMPT_ROW);
        return;
    }

    uint32_t screen_line = cursor_line - view_top;

    uint8_t x = (uint8_t)cursor_col;
    uint8_t y = (uint8_t)(NANO_TEXT_TOP_ROW + screen_line);

    if (x >= NANO_SCREEN_COLS)
    {
        x = NANO_SCREEN_COLS - 1;
    }

    terminal_set_cursor(x, y);
}

static void ensure_cursor_visible(void)
{
    if (cursor_line < view_top)
    {
        view_top = cursor_line;
    }
    else if (cursor_line >= view_top + NANO_TEXT_VISIBLE_ROWS)
    {
        view_top =
            cursor_line - NANO_TEXT_VISIBLE_ROWS + 1;
    }
}

static void redraw(void)
{
    ensure_cursor_visible();

    draw_title_bar();
    draw_text_area();

    if (prompt_active)
    {
        draw_prompt_bar();
    }
    else
    {
        draw_status_bar();
    }

    update_cursor_position();
    terminal_update_cursor();
}

/*
 * ------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------
 */

int nano_active(void)
{
    return active;
}

void nano_open(const char* filename)
{
    str_copy_bounded(
        current_filename,
        filename,
        NANO_FILENAME_LEN - 1
    );

    buffer_load(current_filename);

    prompt_active = 0;
    prompt_length = 0;
    prompt_cursor = 0;
    mem_zero(prompt_buffer, sizeof(prompt_buffer));

    active = 1;

    terminal_clear();
    redraw();
}

static void nano_close(void)
{
    active = 0;
    terminal_clear();
    shell_resume_prompt();
}

/*
 * ------------------------------------------------------------
 * Editing operations
 * ------------------------------------------------------------
 */

void nano_keyboard_character(char c)
{
    if (!active || prompt_active)
    {
        return;
    }

    if (c < 32 && c != '\t')
    {
        return;
    }

    uint32_t len = line_length[cursor_line];

    if (len >= NANO_MAX_LINE_LEN)
    {
        return;
    }

    for (uint32_t i = len; i > cursor_col; i--)
    {
        lines[cursor_line][i] =
            lines[cursor_line][i - 1];
    }

    lines[cursor_line][cursor_col] = c;

    line_length[cursor_line] = len + 1;
    lines[cursor_line][len + 1] = '\0';

    cursor_col++;
    dirty = 1;

    redraw();
}

void nano_keyboard_enter(void)
{
    if (!active)
    {
        return;
    }

    if (prompt_active)
    {
        prompt_buffer[prompt_length] = '\0';

        if (prompt_length > 0)
        {
            str_copy_bounded(
                current_filename,
                prompt_buffer,
                NANO_FILENAME_LEN - 1
            );
        }

        prompt_active = 0;

        buffer_save(current_filename);

        redraw();
        return;
    }

    if (line_count >= NANO_MAX_LINES)
    {
        redraw();
        return;
    }

    uint32_t len = line_length[cursor_line];
    uint32_t tail_len = len - cursor_col;

    /*
     * Shift every line below the cursor down by one to make
     * room for the new line.
     */
    for (uint32_t i = line_count; i > cursor_line + 1; i--)
    {
        line_length[i] = line_length[i - 1];

        for (uint32_t c = 0; c <= line_length[i]; c++)
        {
            lines[i][c] = lines[i - 1][c];
        }
    }

    /*
     * Move the tail of the current line onto the new line.
     */
    for (uint32_t c = 0; c < tail_len; c++)
    {
        lines[cursor_line + 1][c] =
            lines[cursor_line][cursor_col + c];
    }

    lines[cursor_line + 1][tail_len] = '\0';
    line_length[cursor_line + 1] = tail_len;

    lines[cursor_line][cursor_col] = '\0';
    line_length[cursor_line] = cursor_col;

    line_count++;

    cursor_line++;
    cursor_col = 0;
    dirty = 1;

    redraw();
}

void nano_keyboard_backspace(void)
{
    if (!active)
    {
        return;
    }

    if (prompt_active)
    {
        if (prompt_cursor > 0)
        {
            for (uint32_t i = prompt_cursor - 1;
                 i < prompt_length - 1;
                 i++)
            {
                prompt_buffer[i] = prompt_buffer[i + 1];
            }

            prompt_length--;
            prompt_cursor--;
            prompt_buffer[prompt_length] = '\0';

            redraw();
        }

        return;
    }

    if (cursor_col > 0)
    {
        uint32_t len = line_length[cursor_line];

        for (uint32_t i = cursor_col - 1; i < len - 1; i++)
        {
            lines[cursor_line][i] =
                lines[cursor_line][i + 1];
        }

        line_length[cursor_line] = len - 1;
        lines[cursor_line][len - 1] = '\0';

        cursor_col--;
        dirty = 1;

        redraw();
        return;
    }

    /*
     * At column 0: merge this line into the previous one,
     * unless this is already the first line.
     */
    if (cursor_line == 0)
    {
        return;
    }

    uint32_t prev_len = line_length[cursor_line - 1];
    uint32_t cur_len = line_length[cursor_line];

    if (prev_len + cur_len > NANO_MAX_LINE_LEN)
    {
        /*
         * Not enough room to merge; refuse silently rather
         * than truncate data.
         */
        return;
    }

    for (uint32_t c = 0; c < cur_len; c++)
    {
        lines[cursor_line - 1][prev_len + c] =
            lines[cursor_line][c];
    }

    line_length[cursor_line - 1] = prev_len + cur_len;
    lines[cursor_line - 1][prev_len + cur_len] = '\0';

    /*
     * Shift every following line up by one, closing the gap.
     */
    for (uint32_t i = cursor_line; i < line_count - 1; i++)
    {
        line_length[i] = line_length[i + 1];

        for (uint32_t c = 0; c <= line_length[i]; c++)
        {
            lines[i][c] = lines[i + 1][c];
        }
    }

    line_count--;

    cursor_line--;
    cursor_col = prev_len;
    dirty = 1;

    redraw();
}

void nano_keyboard_delete(void)
{
    if (!active || prompt_active)
    {
        return;
    }

    uint32_t len = line_length[cursor_line];

    if (cursor_col < len)
    {
        for (uint32_t i = cursor_col; i < len - 1; i++)
        {
            lines[cursor_line][i] =
                lines[cursor_line][i + 1];
        }

        line_length[cursor_line] = len - 1;
        lines[cursor_line][len - 1] = '\0';

        dirty = 1;
        redraw();
        return;
    }

    /*
     * At end of line: pull the next line up into this one,
     * unless this is already the last line.
     */
    if (cursor_line + 1 >= line_count)
    {
        return;
    }

    uint32_t next_len = line_length[cursor_line + 1];

    if (len + next_len > NANO_MAX_LINE_LEN)
    {
        return;
    }

    for (uint32_t c = 0; c < next_len; c++)
    {
        lines[cursor_line][len + c] =
            lines[cursor_line + 1][c];
    }

    line_length[cursor_line] = len + next_len;
    lines[cursor_line][len + next_len] = '\0';

    for (uint32_t i = cursor_line + 1; i < line_count - 1; i++)
    {
        line_length[i] = line_length[i + 1];

        for (uint32_t c = 0; c <= line_length[i]; c++)
        {
            lines[i][c] = lines[i + 1][c];
        }
    }

    line_count--;
    dirty = 1;

    redraw();
}

void nano_keyboard_left(void)
{
    if (!active)
    {
        return;
    }

    if (prompt_active)
    {
        if (prompt_cursor > 0)
        {
            prompt_cursor--;
            redraw();
        }

        return;
    }

    if (cursor_col > 0)
    {
        cursor_col--;
    }
    else if (cursor_line > 0)
    {
        cursor_line--;
        cursor_col = line_length[cursor_line];
    }

    redraw();
}

void nano_keyboard_right(void)
{
    if (!active)
    {
        return;
    }

    if (prompt_active)
    {
        if (prompt_cursor < prompt_length)
        {
            prompt_cursor++;
            redraw();
        }

        return;
    }

    uint32_t len = line_length[cursor_line];

    if (cursor_col < len)
    {
        cursor_col++;
    }
    else if (cursor_line + 1 < line_count)
    {
        cursor_line++;
        cursor_col = 0;
    }

    redraw();
}

void nano_keyboard_up(void)
{
    if (!active || prompt_active)
    {
        return;
    }

    if (cursor_line == 0)
    {
        return;
    }

    cursor_line--;

    if (cursor_col > line_length[cursor_line])
    {
        cursor_col = line_length[cursor_line];
    }

    redraw();
}

void nano_keyboard_down(void)
{
    if (!active || prompt_active)
    {
        return;
    }

    if (cursor_line + 1 >= line_count)
    {
        return;
    }

    cursor_line++;

    if (cursor_col > line_length[cursor_line])
    {
        cursor_col = line_length[cursor_line];
    }

    redraw();
}

void nano_keyboard_home(void)
{
    if (!active)
    {
        return;
    }

    if (prompt_active)
    {
        prompt_cursor = 0;
        redraw();
        return;
    }

    cursor_col = 0;
    redraw();
}

void nano_keyboard_end(void)
{
    if (!active)
    {
        return;
    }

    if (prompt_active)
    {
        prompt_cursor = prompt_length;
        redraw();
        return;
    }

    cursor_col = line_length[cursor_line];
    redraw();
}

void nano_keyboard_ctrl(char letter)
{
    if (!active)
    {
        return;
    }

    /*
     * Ctrl+O: begin (or confirm) the save prompt.
     * Pressing Ctrl+O while the prompt is already open has no
     * special effect; the user presses Enter to confirm, as
     * documented in the status bar.
     */
    if (letter == 'o')
    {
        if (!prompt_active)
        {
            prompt_active = 1;

            str_copy_bounded(
                prompt_buffer,
                current_filename,
                NANO_FILENAME_LEN - 1
            );

            prompt_length = str_len(prompt_buffer);
            prompt_cursor = prompt_length;
        }

        redraw();
        return;
    }

    /*
     * Ctrl+X: exit. If a save prompt is open, this cancels it
     * instead of exiting nano outright (mirrors real nano's
     * "cancel the current prompt first" behavior).
     */
    if (letter == 'x')
    {
        if (prompt_active)
        {
            prompt_active = 0;
            redraw();
            return;
        }

        nano_close();
        return;
    }
}
