#pragma once

#ifdef _WIN32
#include <windows.h>
#endif

// Windows UTF-8 <-> UTF-16 helpers. Caller owns returned buffers.
wchar_t* chat_utf8_to_wide_alloc(const char* s);
char* chat_wide_to_utf8_alloc(const wchar_t* ws);
