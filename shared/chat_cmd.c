#include "chat_cmd.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void trim_leading_spaces(char** p) {
    // Move pointer past any leading spaces.
    while (**p == ' ') (*p)++;
}

int chat_cmd_parse_inplace(char* payload, ChatCmd* out) {
    memset(out, 0, sizeof(*out));
    out->buf = payload;

    // Split off trailing free-form text (":" delimiter).
    char* text_start = strstr(payload, " :");
    if (text_start) {
        *text_start = 0;
        out->text = text_start + 2;
    } else {
        char* colon = strchr(payload, ':');
        if (colon == payload) {
            out->text = payload + 1;
            payload[0] = 0;
        }
    }

    // Tokenize command and up to two args by inserting NULs.
    char* p = payload;
    trim_leading_spaces(&p);
    if (*p == 0) return 0;

    out->cmd = p;
    char* space = strchr(p, ' ');
    if (!space) return 1;
    *space = 0;

    p = space + 1;
    trim_leading_spaces(&p);
    if (*p == 0) return 1;

    out->arg1 = p;
    space = strchr(p, ' ');
    if (!space) return 1;
    *space = 0;

    p = space + 1;
    trim_leading_spaces(&p);
    if (*p == 0) return 1;

    out->arg2 = p;
    space = strchr(p, ' ');
    if (!space) return 1;
    *space = 0;

    return 1;
}

void chat_cmd_free(ChatCmd* cmd) {
    if (!cmd) return;
    // Buffer is usually allocated by chat_frame_recv_alloc.
    free(cmd->buf);
    memset(cmd, 0, sizeof(*cmd));
}

int chat_cmd_format(char* out, uint32_t out_cap, const char* cmd, const char* arg1, const char* arg2, const char* text) {
    if (!out || out_cap == 0 || !cmd || cmd[0] == 0) return 0;
    out[0] = 0;

    // Build output based on which optional fields are present.
    int n = 0;
    if (arg1 && arg1[0]) {
        if (arg2 && arg2[0]) {
            if (text && text[0]) n = snprintf(out, out_cap, "%s %s %s :%s", cmd, arg1, arg2, text);
            else n = snprintf(out, out_cap, "%s %s %s", cmd, arg1, arg2);
        } else {
            if (text && text[0]) n = snprintf(out, out_cap, "%s %s :%s", cmd, arg1, text);
            else n = snprintf(out, out_cap, "%s %s", cmd, arg1);
        }
    } else {
        if (text && text[0]) n = snprintf(out, out_cap, "%s :%s", cmd, text);
        else n = snprintf(out, out_cap, "%s", cmd);
    }
    if (n < 0) return 0;
    if ((uint32_t)n >= out_cap) return 0;
    return 1;
}
