#pragma once

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

#include <stdint.h>

// Length-prefixed framing helpers shared by client/server.
// Frame format: 32-bit big-endian length followed by payload bytes.

#ifndef CHAT_MAX_FRAME
#define CHAT_MAX_FRAME (64u * 1024u) // Max payload size to cap allocations.
#endif

// Send/receive exactly len bytes; returns 1 on success, 0 on error.
int chat_send_all(SOCKET sock, const void* data, int len);
int chat_recv_all(SOCKET sock, void* data, int len);

// Send a single length-prefixed payload.
int chat_frame_send(SOCKET sock, const void* payload, uint32_t payload_len);
// Receive a length-prefixed payload into a NUL-terminated buffer.
// Caller owns *out_payload.
int chat_frame_recv_alloc(SOCKET sock, uint8_t** out_payload, uint32_t* out_payload_len, uint32_t max_payload_len);
