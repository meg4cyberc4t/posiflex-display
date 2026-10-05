#include "render.h"

#include <string.h>

#define CANVAS_WIDTH 50
#define CANVAS_HEIGHT 14
#define CELL_WIDTH 5
#define CELL_HEIGHT 7

static const char *const tiny_font[10][5] = {
    {"###", "#.#", "#.#", "#.#", "###"},
    {".#.", "##.", ".#.", ".#.", "###"},
    {"###", "..#", "###", "#..", "###"},
    {"###", "..#", "###", "..#", "###"},
    {"#.#", "#.#", "###", "..#", "..#"},
    {"###", "#..", "###", "..#", "###"},
    {"###", "#..", "###", "#.#", "###"},
    {"###", "..#", "..#", "..#", "..#"},
    {"###", "#.#", "###", "#.#", "###"},
    {"###", "#.#", "###", "..#", "###"},
};

static const char *const icons[4][7] = {
    {".....", ".....", ".....", ".....", ".....", ".....", "....."},
    {".....", "#...#", ".#.#.", "..#..", ".#.#.", "#...#", "....."},
    {".....", ".....", ".##..", "#..##", ".....", ".....", "....."},
    {".....", "....#", "...#.", "#.#..", ".#...", ".....", "....."},
};

static const char *const segments[10] = {
    "abcdef", "bc", "abged", "abgcd", "fgbc", "afgcd", "afgecd", "abc",
    "abcdefg", "abfgcd",
};

typedef struct { int x1, y1, x2, y2; } rectangle_t;
static const rectangle_t segment_rects[7] = {
    {2, 0, 7, 1}, {7, 1, 8, 6}, {7, 7, 8, 12}, {2, 12, 7, 13},
    {1, 7, 2, 12}, {1, 1, 2, 6}, {2, 6, 7, 7},
};

static bool send_bytes(const posiflex_output_t *output, const uint8_t *bytes, size_t size)
{
    return output->send(output->context, bytes, size);
}

static bool cursor(const posiflex_output_t *output, uint8_t column, uint8_t row)
{
    const uint8_t command[] = {0x1f, 0x24, column, row};
    return send_bytes(output, command, sizeof(command));
}

static bool draw_cell(const posiflex_output_t *output, const char rows[7][6], uint16_t *code)
{
    uint8_t packed[5] = {0};
    bool blank = true;
    for (int y = 0; y < CELL_HEIGHT; ++y) {
        for (int x = 0; x < CELL_WIDTH; ++x) {
            if (rows[y][x] == '#') {
                const int pixel = y * CELL_WIDTH + x;
                packed[pixel / 8] |= (uint8_t)(1U << (pixel % 8));
                blank = false;
            }
        }
    }
    if (blank) {
        const uint8_t space = ' ';
        return send_bytes(output, &space, 1);
    }
    if (*code > 0xff) {
        return false;
    }
    uint8_t definition[11] = {0x1b, 0x26, (uint8_t)*code};
    memcpy(&definition[3], packed, sizeof(packed));
    definition[8] = 0x1b;
    definition[9] = 0x25;
    definition[10] = 0x01;
    if (!send_bytes(output, definition, sizeof(definition))) {
        return false;
    }
    output->pause_ms(output->context, 50);
    const uint8_t character = (uint8_t)*code;
    if (!send_bytes(output, &character, 1)) {
        return false;
    }
    output->pause_ms(output->context, 50);
    ++*code;
    return true;
}

static void tiny_date_cell(char rows[7][6], int digit, bool leading_dot)
{
    memset(rows, '.', 7 * 6);
    for (int y = 0; y < 7; ++y) rows[y][5] = '\0';
    const int offset = leading_dot ? 2 : 1;
    for (int y = 0; y < 5; ++y) {
        for (int x = 0; x < 3; ++x) {
            rows[y + 1][offset + x] = tiny_font[digit][y][x];
        }
    }
    if (leading_dot) rows[5][0] = '#';
}

static void icon_cell(char rows[7][6], posiflex_network_status_t status)
{
    if (status > POSIFLEX_INTERNET_OK) status = POSIFLEX_CHECKING;
    for (int y = 0; y < 7; ++y) {
        memcpy(rows[y], icons[status][y], 5);
        rows[y][5] = '\0';
    }
}

static void paint_rectangle(bool canvas[CANVAS_HEIGHT][CANVAS_WIDTH], int left,
                            rectangle_t rect)
{
    for (int y = rect.y1; y <= rect.y2; ++y) {
        for (int x = rect.x1; x <= rect.x2; ++x) {
            canvas[y][left + x] = true;
        }
    }
}

static void draw_digit(bool canvas[CANVAS_HEIGHT][CANVAS_WIDTH], int digit, int left)
{
    for (const char *segment = segments[digit]; *segment; ++segment) {
        paint_rectangle(canvas, left, segment_rects[*segment - 'a']);
    }
}

static void make_canvas(bool canvas[CANVAS_HEIGHT][CANVAS_WIDTH],
                        const struct tm *local, bool colon_on)
{
    memset(canvas, 0, sizeof(bool) * CANVAS_HEIGHT * CANVAS_WIDTH);
    const int digits[] = {
        local->tm_hour / 10, local->tm_hour % 10,
        local->tm_min / 10, local->tm_min % 10,
    };
    const int lefts[] = {0, 10, 30, 40};
    for (int i = 0; i < 4; ++i) draw_digit(canvas, digits[i], lefts[i]);
    if (colon_on) {
        for (int y = 4; y <= 9; ++y) {
            if (y == 6 || y == 7) continue;
            for (int x = 23; x <= 26; ++x) canvas[y][x] = true;
        }
    }
}

static void canvas_cell(char rows[7][6],
                        const bool canvas[CANVAS_HEIGHT][CANVAS_WIDTH],
                        int cell_x, int cell_y)
{
    for (int y = 0; y < 7; ++y) {
        for (int x = 0; x < 5; ++x) {
            rows[y][x] = canvas[cell_y * 7 + y][cell_x * 5 + x] ? '#' : '.';
        }
        rows[y][5] = '\0';
    }
}

bool posiflex_render_full(const posiflex_output_t *output, const struct tm *local,
                          bool colon_on, posiflex_network_status_t status,
                          uint16_t *next_code)
{
    if (!output || !output->send || !output->pause_ms || !local || !next_code) return false;
    if (local->tm_hour < 0 || local->tm_hour > 23 || local->tm_min < 0 ||
        local->tm_min > 59 || local->tm_mday < 1 || local->tm_mday > 31 ||
        local->tm_mon < 0 || local->tm_mon > 11) return false;

    bool canvas[CANVAS_HEIGHT][CANVAS_WIDTH];
    make_canvas(canvas, local, colon_on);
    const uint8_t clear[] = {0x1b, 0x40};
    if (!send_bytes(output, clear, sizeof(clear))) return false;
    output->pause_ms(output->context, 50);
    uint16_t code = 0xa0;
    char rows[7][6];

    const int month = local->tm_mon + 1;
    const int date_digits[] = {
        local->tm_mday / 10, local->tm_mday % 10, month / 10, month % 10,
    };
    if (!cursor(output, 1, 2)) return false;
    for (int i = 0; i < 4; ++i) {
        tiny_date_cell(rows, date_digits[i], i == 2);
        if (!draw_cell(output, rows, &code)) return false;
    }
    if (!cursor(output, 19, 1)) return false;
    icon_cell(rows, status);
    if (!draw_cell(output, rows, &code)) return false;

    for (int row = 0; row < 2; ++row) {
        if (!cursor(output, 6, row + 1)) return false;
        for (int column = 0; column < 10; ++column) {
            canvas_cell(rows, canvas, column, row);
            if (!draw_cell(output, rows, &code)) return false;
        }
    }

    // This pair occasionally fails to latch during a full redraw. Repeat it
    // after the rest of the frame has reached the display.
    output->pause_ms(output->context, 150);
    if (!cursor(output, 12, 1)) return false;
    for (int column = 6; column <= 7; ++column) {
        canvas_cell(rows, canvas, column, 0);
        if (!draw_cell(output, rows, &code)) return false;
    }
    *next_code = code;
    return true;
}

bool posiflex_render_colon(const posiflex_output_t *output, const struct tm *local,
                           bool colon_on, uint16_t *next_code)
{
    if (!output || !output->send || !output->pause_ms || !local || !next_code) return false;
    bool canvas[CANVAS_HEIGHT][CANVAS_WIDTH];
    make_canvas(canvas, local, colon_on);
    char rows[7][6];
    for (int row = 0; row < 2; ++row) {
        if (!cursor(output, 10, row + 1)) return false;
        for (int column = 4; column <= 5; ++column) {
            canvas_cell(rows, canvas, column, row);
            if (!draw_cell(output, rows, next_code)) return false;
        }
    }
    return true;
}

bool posiflex_render_waiting_for_time(const posiflex_output_t *output)
{
    if (!output || !output->send || !output->pause_ms) return false;
    const uint8_t clear[] = {0x1b, 0x40};
    const uint8_t message[] = "WAIT NTP";
    return send_bytes(output, clear, sizeof(clear)) &&
           cursor(output, 7, 1) &&
           send_bytes(output, message, sizeof(message) - 1);
}
