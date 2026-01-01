#pragma once

#ifdef _WIN32
#include <windows.h>
#endif

wchar_t* chat_utf8_to_wide_alloc(const char* s);
char* chat_wide_to_utf8_alloc(const wchar_t* ws);

