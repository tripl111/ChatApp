#include "chat_utf8.h"

#include <stdlib.h>

wchar_t* chat_utf8_to_wide_alloc(const char* s) {
    if (!s) return NULL;
    int needed = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    if (needed <= 0) return NULL;
    wchar_t* out = (wchar_t*)malloc((size_t)needed * sizeof(wchar_t));
    if (!out) return NULL;
    if (MultiByteToWideChar(CP_UTF8, 0, s, -1, out, needed) <= 0) {
        free(out);
        return NULL;
    }
    return out;
}

char* chat_wide_to_utf8_alloc(const wchar_t* ws) {
    if (!ws) return NULL;
    int needed = WideCharToMultiByte(CP_UTF8, 0, ws, -1, NULL, 0, NULL, NULL);
    if (needed <= 0) return NULL;
    char* out = (char*)malloc((size_t)needed);
    if (!out) return NULL;
    if (WideCharToMultiByte(CP_UTF8, 0, ws, -1, out, needed, NULL, NULL) <= 0) {
        free(out);
        return NULL;
    }
    return out;
}

