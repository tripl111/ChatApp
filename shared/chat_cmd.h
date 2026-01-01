#pragma once

#include <stdint.h>

// Text command format:
// "CMD arg1 arg2 :optional free text"
// Only cmd is required; args/text are optional.

// Parsed command; fields point into the original buffer (no copies).
typedef struct ChatCmd {
    char* buf;
    char* cmd;
    char* arg1;
    char* arg2;
    char* text;
} ChatCmd;

// Parse payload in place by inserting NULs between tokens.
int chat_cmd_parse_inplace(char* payload, ChatCmd* out);
// Free the original buffer referenced by cmd->buf.
void chat_cmd_free(ChatCmd* cmd);

// Format a command into out; returns 1 if it fits in out_cap.
int chat_cmd_format(char* out, uint32_t out_cap, const char* cmd, const char* arg1, const char* arg2, const char* text);
