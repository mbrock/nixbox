/* An installed-header C consumer; expected cells are independent VT results. */
#include <ghostty/vt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); \
        exit(1); \
    } \
} while (0)

static void feed(GhosttyTerminal t, const char *s) {
    ghostty_terminal_vt_write(t, (const uint8_t *)s, strlen(s));
}

static uint16_t value(GhosttyTerminal t, GhosttyTerminalData key) {
    uint16_t result = 0;
    CHECK(ghostty_terminal_get(t, key, &result) == GHOSTTY_SUCCESS);
    return result;
}

static uint32_t cell(GhosttyTerminal t, GhosttyPointTag tag, uint16_t x, uint32_t y) {
    GhosttyGridRef ref = GHOSTTY_INIT_SIZED(GhosttyGridRef);
    GhosttyPoint point = {.tag = tag, .value = {.coordinate = {.x = x, .y = y}}};
    GhosttyCell c;
    uint32_t result = 0;
    CHECK(ghostty_terminal_grid_ref(t, point, &ref) == GHOSTTY_SUCCESS);
    CHECK(ghostty_grid_ref_cell(&ref, &c) == GHOSTTY_SUCCESS);
    CHECK(ghostty_cell_get(c, GHOSTTY_CELL_DATA_CODEPOINT, &result) == GHOSTTY_SUCCESS);
    return result;
}

static void row(GhosttyTerminal t, uint32_t y, const char *expected) {
    for (uint16_t x = 0; x < value(t, GHOSTTY_TERMINAL_DATA_COLS); ++x) {
        uint32_t want = x < strlen(expected) ? (unsigned char)expected[x] : 0;
        CHECK(cell(t, GHOSTTY_POINT_TAG_ACTIVE, x, y) == want);
    }
}

int main(void) {
    GhosttyTerminal t;
    CHECK(ghostty_terminal_new(NULL, &t, 8, 3) == GHOSTTY_SUCCESS);

    /* Split within both 2- and 3-byte characters; wide text consumes two cells. */
    feed(t, "A\xc3");
    CHECK(value(t, GHOSTTY_TERMINAL_DATA_CURSOR_X) == 1);
    feed(t, "\xa9\xe7\x95");
    CHECK(value(t, GHOSTTY_TERMINAL_DATA_CURSOR_X) == 2);
    feed(t, "\x8cZ");
    CHECK(cell(t, GHOSTTY_POINT_TAG_ACTIVE, 0, 0) == 'A');
    CHECK(cell(t, GHOSTTY_POINT_TAG_ACTIVE, 1, 0) == 0xe9);
    CHECK(cell(t, GHOSTTY_POINT_TAG_ACTIVE, 2, 0) == 0x754c);
    CHECK(cell(t, GHOSTTY_POINT_TAG_ACTIVE, 4, 0) == 'Z');
    CHECK(value(t, GHOSTTY_TERMINAL_DATA_CURSOR_X) == 5);
    puts("PASS UTF-8 split writes and wide-cell cursor");

    ghostty_terminal_reset(t);
    feed(t, "abcdefgh\r\n123456\r\nXYZ");
    /* Split CSI too; CUP is 1-based and EL erases only from cursor onward. */
    feed(t, "\033[2;");
    feed(t, "4H\033[K");
    row(t, 0, "abcdefgh");
    row(t, 1, "123");
    row(t, 2, "XYZ");
    CHECK(value(t, GHOSTTY_TERMINAL_DATA_CURSOR_X) == 3);
    CHECK(value(t, GHOSTTY_TERMINAL_DATA_CURSOR_Y) == 1);
    feed(t, "\033[1D!");
    row(t, 1, "12!");
    feed(t, "\033[3;2H\033[1J");
    row(t, 0, "");
    row(t, 1, "");
    CHECK(cell(t, GHOSTTY_POINT_TAG_ACTIVE, 0, 2) == 0);
    CHECK(cell(t, GHOSTTY_POINT_TAG_ACTIVE, 1, 2) == 0);
    CHECK(cell(t, GHOSTTY_POINT_TAG_ACTIVE, 2, 2) == 'Z');
    puts("PASS split CSI, asymmetric cursor movement, EL and ED");

    ghostty_terminal_reset(t);
    feed(t, "one\r\ntwo\r\ntri\r\nfour");
    row(t, 0, "two");
    row(t, 1, "tri");
    row(t, 2, "four");
    CHECK(cell(t, GHOSTTY_POINT_TAG_HISTORY, 0, 0) == 'o');
    CHECK(value(t, GHOSTTY_TERMINAL_DATA_CURSOR_Y) == 2);
    puts("PASS scroll and retained history");

    ghostty_terminal_reset(t);
    CHECK(ghostty_terminal_resize(t, 5, 3, 8, 16) == GHOSTTY_SUCCESS);
    feed(t, "abcdefghi");
    row(t, 0, "abcde");
    row(t, 1, "fghi");
    CHECK(ghostty_terminal_resize(t, 3, 4, 8, 16) == GHOSTTY_SUCCESS);
    CHECK(value(t, GHOSTTY_TERMINAL_DATA_COLS) == 3);
    CHECK(value(t, GHOSTTY_TERMINAL_DATA_ROWS) == 4);
    row(t, 0, "abc");
    row(t, 1, "def");
    row(t, 2, "ghi");
    CHECK(ghostty_terminal_resize(t, 8, 4, 8, 16) == GHOSTTY_SUCCESS);
    row(t, 0, "abcdefgh");
    row(t, 1, "i");
    puts("PASS shrinking and growing reflow");

    /* Grow the page pool beyond its initial allocation and recycle pages. */
    ghostty_terminal_reset(t);
    for (unsigned i = 0; i < 3000; ++i) feed(t, "row\r\n");
    feed(t, "END");
    row(t, 2, "row");
    row(t, 3, "END");
    ghostty_terminal_free(t);
    puts("PASS page-pool growth, scroll stress and destruction");
    puts("ghostty-vt installed C consumer: all checks passed");
    return 0;
}
