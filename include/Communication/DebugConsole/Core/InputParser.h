#ifndef INPUT_PARSER_H
#define INPUT_PARSER_H
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* All calls belong to the console input worker; external callers must serialize. */
typedef enum {
    INPUT_EVENT_NONE = 0,
    INPUT_EVENT_LINE_READY,
    INPUT_EVENT_TAB,
    INPUT_EVENT_HISTORY_PREV,
    INPUT_EVENT_HISTORY_NEXT,
    INPUT_EVENT_CANCEL,
} InputParserEvent;
void InputParser_Init(void);
/* A successful line copy belongs to the caller, which must erase it after use.
 * Private input remains masked through editing until reset/cancel/submission.
 * NULL output or zero capacity leaves the editor unchanged. */
InputParserEvent InputParser_ProcessChar(char c, char *out_line, size_t max_len);
void InputParser_SetLine(const char *line, char *out_line, size_t max_len);
void InputParser_Reset(void);
#endif
