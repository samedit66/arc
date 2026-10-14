#ifndef CLI_OUTPUT_H
#define CLI_OUTPUT_H
#include <stdio.h>

typedef enum { MESSAGE_SUCCESS, MESSAGE_ERROR, MESSAGE_WARNING } MessageKind;

int output_terminal(FILE *out, int *unicode);
void display_text(FILE *out, const char *text, int escape_backslash);
void message(MessageKind kind, const char *label, const char *format, ...);
#endif
