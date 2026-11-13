#include "Communication/DebugConsole/Core/InputParser.h"
#include "Communication/DebugConsole/DebugConsoleConfig.h"
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include "Communication/DebugConsole/Core/ConsolePrivacy.h"

/* Single-owner terminal editor; the console worker serializes all calls. */

typedef enum {
    STATE_NORMAL,
    STATE_ESC,
    STATE_CSI,
    STATE_CSI_PARAM   /* CSI numeric parameter, e.g. Delete. */
} ParserState;

typedef struct {
    ParserState state;
    char        buffer[DEBUG_CONSOLE_LINE_BUFFER_SIZE];
    int         length;     /* Number of buffered characters. */
    int         cursor;     /* Cursor position in [0, length]. */
    int         csi_param;  /* CSI parameter. */
    bool        private_line; /* Sticky until submission, cancellation or reset. */
    bool        initialized;
} InputParserState;

static InputParserState s_parser = {0};

/* ─────────────────────────────────────────────
 * Internal helpers
 * ───────────────────────────────────────────── */

static void print_range(int start, int count) {
    if (s_parser.private_line) {
        for (int i = 0; i < count; ++i) putchar('*');
    } else {
        printf("%.*s", count, &s_parser.buffer[start]);
    }
}

static void redraw_from_cursor(void) {
    /* Redraw the suffix and erase the stale terminal character. */
    print_range(s_parser.cursor, s_parser.length - s_parser.cursor);
    printf(" \b");
    /* Restore the cursor position. */
    int back = s_parser.length - s_parser.cursor;
    for (int i = 0; i < back; i++) {
        printf("\b");
    }
    fflush(stdout);
}

static void clear_line_display(void) {
    /* Move to the start and clear the displayed line. */
    for (int i = 0; i < s_parser.cursor; i++) {
        printf("\b");
    }
    for (int i = 0; i < s_parser.length; i++) {
        printf(" ");
    }
    for (int i = 0; i < s_parser.length; i++) {
        printf("\b");
    }
    fflush(stdout);
}

static void copy_to_output(char *out_line, size_t max_len) {
    size_t copy_len = (size_t)s_parser.length;
    if (copy_len >= max_len) {
        copy_len = max_len - 1;
    }
    memcpy(out_line, s_parser.buffer, copy_len);
    out_line[copy_len] = '\0';
}

/* ─────────────────────────────────────────────
 * Public API
 * ───────────────────────────────────────────── */

void InputParser_Init(void) {
    if (s_parser.initialized) {
        InputParser_Reset();
        return;
    }
    memset(&s_parser, 0, sizeof(s_parser));
    s_parser.initialized = true;
}

void InputParser_Reset(void) {
    s_parser.state     = STATE_NORMAL;
    s_parser.length    = 0;
    s_parser.cursor    = 0;
    s_parser.csi_param = 0;
    ConsolePrivacy_Clear(s_parser.buffer, sizeof(s_parser.buffer));
    s_parser.private_line = false;
}

InputParserEvent InputParser_ProcessChar(char c, char *out_line, size_t max_len) {
    InputParserEvent event = INPUT_EVENT_NONE;
    if (!out_line || max_len == 0) return event;

    switch (s_parser.state) {
        case STATE_NORMAL:
            if (c == '\x1b') {
                /* ESC */
                s_parser.state = STATE_ESC;

            } else if (c == '\r' || c == '\n') {
                /* Enter */
                if (s_parser.length > 0) {
                    s_parser.buffer[s_parser.length] = '\0';
                    copy_to_output(out_line, max_len);
                    event = INPUT_EVENT_LINE_READY;
                    InputParser_Reset();
                }

            } else if (c == '\b' || c == 0x7F) {
                /* Backspace. */
                if (s_parser.cursor > 0) {
                    s_parser.cursor--;
                    memmove(&s_parser.buffer[s_parser.cursor],
                            &s_parser.buffer[s_parser.cursor + 1],
                            (size_t)(s_parser.length - s_parser.cursor - 1));
                    s_parser.length--;
                    s_parser.buffer[s_parser.length] = '\0';
                    printf("\b");
                    redraw_from_cursor();
                }

            } else if (c == '\t') {
                /* Tab */
                s_parser.buffer[s_parser.length] = '\0';
                copy_to_output(out_line, max_len);
                event = INPUT_EVENT_TAB;

            } else if (c == 0x03) {
                /* Ctrl+C */
                InputParser_Reset();
                event = INPUT_EVENT_CANCEL;

            } else if (c == 0x01) {
                /* Ctrl+A: line start. */
                while (s_parser.cursor > 0) {
                    printf("\b");
                    s_parser.cursor--;
                }
                fflush(stdout);

            } else if (c == 0x05) {
                /* Ctrl+E: line end. */
                print_range(s_parser.cursor, s_parser.length - s_parser.cursor);
                s_parser.cursor = s_parser.length;
                fflush(stdout);

            } else if (c == 0x0B) {
                /* Ctrl+K: erase the suffix. */
                int del = s_parser.length - s_parser.cursor;
                for (int i = 0; i < del; i++) printf(" ");
                for (int i = 0; i < del; i++) printf("\b");
                s_parser.length = s_parser.cursor;
                ConsolePrivacy_Clear(s_parser.buffer + s_parser.length, sizeof(s_parser.buffer) - (size_t)s_parser.length);
                fflush(stdout);

            } else if (c == 0x15) {
                /* Ctrl+U: erase the line. */
                clear_line_display();
                InputParser_Reset();

            } else if (isprint((int)(unsigned char)c)) {
                /* Printable character. */
                if (s_parser.length < (int)(sizeof(s_parser.buffer) - 1) &&
                    (size_t)s_parser.length < max_len - 1) {
                    /* Make space for insertion. */
                    if (s_parser.cursor < s_parser.length) {
                        memmove(&s_parser.buffer[s_parser.cursor + 1],
                                &s_parser.buffer[s_parser.cursor],
                                (size_t)(s_parser.length - s_parser.cursor));
                    }
                    s_parser.buffer[s_parser.cursor] = c;
                    s_parser.length++;
                    s_parser.cursor++;
                    s_parser.buffer[s_parser.length] = '\0';

                    s_parser.private_line |= ConsolePrivacy_IsPrivate(s_parser.buffer);
                    if (s_parser.cursor == s_parser.length) {
                        putchar(s_parser.private_line ? '*' : c);
                    } else {
                        /* Redraw the suffix after insertion. */
                        putchar(s_parser.private_line ? '*' : c);
                        redraw_from_cursor();
                    }
                    fflush(stdout);
                }
            }
            break;

        case STATE_ESC:
            if (c == '[') {
                s_parser.state = STATE_CSI;
                s_parser.csi_param = 0;
            } else {
                s_parser.state = STATE_NORMAL;
            }
            break;

        case STATE_CSI:
            if (c >= '0' && c <= '9') {
                /* Numeric CSI parameter. */
                s_parser.csi_param = s_parser.csi_param * 10 + (c - '0');
                s_parser.state = STATE_CSI_PARAM;
            } else {
                s_parser.state = STATE_NORMAL;
                if (c == 'A') {
                    event = INPUT_EVENT_HISTORY_PREV;
                } else if (c == 'B') {
                    event = INPUT_EVENT_HISTORY_NEXT;
                } else if (c == 'C') {
                    /* Right arrow. */
                    if (s_parser.cursor < s_parser.length) {
                        printf("\033[C");
                        fflush(stdout);
                        s_parser.cursor++;
                    }
                } else if (c == 'D') {
                    /* Left arrow. */
                    if (s_parser.cursor > 0) {
                        printf("\033[D");
                        fflush(stdout);
                        s_parser.cursor--;
                    }
                } else if (c == 'H') {
                    /* Home */
                    while (s_parser.cursor > 0) {
                        printf("\b");
                        s_parser.cursor--;
                    }
                    fflush(stdout);
                } else if (c == 'F') {
                    /* End */
                    print_range(s_parser.cursor, s_parser.length - s_parser.cursor);
                    s_parser.cursor = s_parser.length;
                    fflush(stdout);
                }
            }
            break;

        case STATE_CSI_PARAM:
            s_parser.state = STATE_NORMAL;
            if (c == '~') {
                if (s_parser.csi_param == 3) {
                    /* Delete key. */
                    if (s_parser.cursor < s_parser.length) {
                        memmove(&s_parser.buffer[s_parser.cursor],
                                &s_parser.buffer[s_parser.cursor + 1],
                                (size_t)(s_parser.length - s_parser.cursor - 1));
                        s_parser.length--;
                        s_parser.buffer[s_parser.length] = '\0';
                        redraw_from_cursor();
                    }
                }
                /* Ignore unsupported CSI parameters. */
            }
            s_parser.csi_param = 0;
            break;
    }

    return event;
}

void InputParser_SetLine(const char *line, char *out_line, size_t max_len) {
    if (!line || !out_line || max_len == 0) return;

    /* Clear the displayed line. */
    clear_line_display();

    size_t len = strlen(line);
    if (len >= sizeof(s_parser.buffer)) {
        len = sizeof(s_parser.buffer) - 1;
    }
    if (len >= max_len) len = max_len - 1;
    ConsolePrivacy_Clear(s_parser.buffer, sizeof(s_parser.buffer));
    memcpy(s_parser.buffer, line, len);
    s_parser.buffer[len] = '\0';
    s_parser.length = (int)len;
    s_parser.cursor = (int)len;

    s_parser.private_line |= ConsolePrivacy_IsPrivate(s_parser.buffer);
    print_range(0, s_parser.length);
    fflush(stdout);

    copy_to_output(out_line, max_len);
}
