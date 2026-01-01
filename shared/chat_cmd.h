#pragma once

#include <stdint.h>

typedef struct ChatCmd {
    char* buf;
    char* cmd;
    char* arg1;
    char* arg2;
    char* text;
} ChatCmd;

int chat_cmd_parse_inplace(char* payload, ChatCmd* out);
void chat_cmd_free(ChatCmd* cmd);

int chat_cmd_format(char* out, uint32_t out_cap, const char* cmd, const char* arg1, const char* arg2, const char* text);

