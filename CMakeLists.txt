cmake_minimum_required(VERSION 3.24)
project(ChatApp C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_library(chat_shared
    shared/chat_cmd.c
    shared/chat_frame.c
    shared/chat_utf8.c
)

target_include_directories(chat_shared PUBLIC shared)
target_compile_definitions(chat_shared PUBLIC UNICODE _UNICODE WIN32_LEAN_AND_MEAN)

add_executable(chat_server
    server/main.c
)
target_link_libraries(chat_server PRIVATE chat_shared ws2_32)

add_executable(chat_client WIN32
    client/main.c
)
target_link_libraries(chat_client PRIVATE chat_shared ws2_32 user32 gdi32 comctl32)

