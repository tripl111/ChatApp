#include "chat_frame.h"

#include <stdlib.h>
#include <string.h>

int chat_send_all(SOCKET sock, const void* data, int len) {
    const char* p = (const char*)data;
    int remaining = len;
    // send() may transmit fewer bytes; loop until all bytes are sent.
    while (remaining > 0) {
        int n = send(sock, p, remaining, 0);
        if (n <= 0) return 0;
        p += n;
        remaining -= n;
    }
    return 1;
}

int chat_recv_all(SOCKET sock, void* data, int len) {
    char* p = (char*)data;
    int remaining = len;
    // recv() may return partial data; loop until we have len bytes.
    while (remaining > 0) {
        int n = recv(sock, p, remaining, 0);
        if (n <= 0) return 0;
        p += n;
        remaining -= n;
    }
    return 1;
}

int chat_frame_send(SOCKET sock, const void* payload, uint32_t payload_len) {
    // Prefix payload with a 32-bit length in network byte order.
    uint32_t net_len = htonl(payload_len);
    if (!chat_send_all(sock, &net_len, (int)sizeof(net_len))) return 0;
    if (payload_len == 0) return 1;
    return chat_send_all(sock, payload, (int)payload_len);
}

int chat_frame_recv_alloc(SOCKET sock, uint8_t** out_payload, uint32_t* out_payload_len, uint32_t max_payload_len) {
    uint32_t net_len = 0;
    // Read length prefix, then allocate payload (+1 for NUL).
    if (!chat_recv_all(sock, &net_len, (int)sizeof(net_len))) return 0;
    uint32_t payload_len = ntohl(net_len);
    if (payload_len > max_payload_len) return 0;

    uint8_t* payload = NULL;
    if (payload_len > 0) {
        payload = (uint8_t*)malloc(payload_len + 1u);
        if (!payload) return 0;
        if (!chat_recv_all(sock, payload, (int)payload_len)) {
            free(payload);
            return 0;
        }
        payload[payload_len] = 0;
    } else {
        payload = (uint8_t*)malloc(1u);
        if (!payload) return 0;
        payload[0] = 0;
    }

    *out_payload = payload;
    *out_payload_len = payload_len;
    return 1;
}
