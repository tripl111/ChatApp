#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "chat_cmd.h"
#include "chat_frame.h"

// Simple threaded chat server for Windows.
// Uses length-prefixed frames and text commands from shared helpers.

#define CHAT_PORT_DEFAULT "5555" // Default TCP port if none provided.
#define CHAT_NAME_MAX 31 // Max username/room length (excluding NUL).

typedef struct Client Client;
typedef struct Room Room;

// Connected client tracked by server state.
struct Client {
    SOCKET sock;
    HANDLE thread;
    int authed; // Set after successful AUTH.
    char username[CHAT_NAME_MAX + 1];
    Client* next; // Linked list of all clients.
};

// Chat room with a fixed-size member list (simple demo structure).
struct Room {
    char name[CHAT_NAME_MAX + 1];
    Client* members[128]; // Fixed-size array of member pointers.
    int member_count;
    Room* next; // Linked list of rooms.
};

typedef struct ServerState {
    CRITICAL_SECTION lock; // Protects clients/rooms.
    Client* clients;
    Room* rooms;
    const char* password; // Plaintext shared password from args.
} ServerState;

static int starts_with(const char* s, const char* pfx) {
    return s && pfx && strncmp(s, pfx, strlen(pfx)) == 0;
}

// Find an authenticated client by username (case-insensitive).
static Client* state_find_client_by_name(ServerState* st, const char* username) {
    for (Client* c = st->clients; c; c = c->next) {
        if (c->authed && _stricmp(c->username, username) == 0) return c;
    }
    return NULL;
}

// Find a room by name (case-insensitive).
static Room* state_find_room(ServerState* st, const char* name) {
    for (Room* r = st->rooms; r; r = r->next) {
        if (_stricmp(r->name, name) == 0) return r;
    }
    return NULL;
}

// Look up or create a room; caller must hold st->lock.
static Room* state_get_or_create_room(ServerState* st, const char* name) {
    Room* r = state_find_room(st, name);
    if (r) return r;

    r = (Room*)calloc(1, sizeof(*r));
    if (!r) return NULL;
    strncpy(r->name, name, CHAT_NAME_MAX);
    r->name[CHAT_NAME_MAX] = 0;
    r->next = st->rooms;
    st->rooms = r;
    return r;
}

// Check if a client is already in the room.
static int room_has_member(Room* r, Client* c) {
    for (int i = 0; i < r->member_count; i++) {
        if (r->members[i] == c) return 1;
    }
    return 0;
}

// Add a client if there's capacity and not already present.
static void room_add_member(Room* r, Client* c) {
    if (!r || !c) return;
    if (room_has_member(r, c)) return;
    if (r->member_count >= (int)(sizeof(r->members) / sizeof(r->members[0]))) return;
    r->members[r->member_count++] = c;
}

// Remove a client by swapping with the last entry.
static void room_remove_member(Room* r, Client* c) {
    if (!r || !c) return;
    for (int i = 0; i < r->member_count; i++) {
        if (r->members[i] == c) {
            r->members[i] = r->members[r->member_count - 1];
            r->members[r->member_count - 1] = NULL;
            r->member_count--;
            return;
        }
    }
}

// Send a raw text payload as a framed message.
static int send_text(SOCKET sock, const char* payload) {
    return chat_frame_send(sock, payload, (uint32_t)strlen(payload));
}

// Send "OK <what>" response.
static int send_ok(SOCKET sock, const char* what) {
    char buf[256];
    if (!chat_cmd_format(buf, sizeof(buf), "OK", what, NULL, NULL)) return 0;
    return send_text(sock, buf);
}

// Send "ERR <code> :reason" response.
static int send_err(SOCKET sock, const char* code, const char* reason) {
    char buf[512];
    if (!chat_cmd_format(buf, sizeof(buf), "ERR", code, NULL, reason)) return 0;
    return send_text(sock, buf);
}

// Broadcast payload to all members of a room.
static void broadcast_room(ServerState* st, Room* r, const char* payload) {
    SOCKET socks[128];
    int count = 0;

    // Snapshot socket list while holding lock; send without lock.
    EnterCriticalSection(&st->lock);
    for (int i = 0; i < r->member_count && count < (int)(sizeof(socks) / sizeof(socks[0])); i++) {
        if (r->members[i]) socks[count++] = r->members[i]->sock;
    }
    LeaveCriticalSection(&st->lock);

    for (int i = 0; i < count; i++) {
        (void)chat_frame_send(socks[i], payload, (uint32_t)strlen(payload));
    }
}

// Remove user from all rooms and notify remaining members.
static void broadcast_user_leave(ServerState* st, Client* c) {
    char payload[256];

    EnterCriticalSection(&st->lock);
    for (Room* r = st->rooms; r; r = r->next) {
        if (room_has_member(r, c)) {
            room_remove_member(r, c);
            LeaveCriticalSection(&st->lock);

            if (chat_cmd_format(payload, sizeof(payload), "USERLEAVE", r->name, c->username, NULL)) {
                broadcast_room(st, r, payload);
            }

            EnterCriticalSection(&st->lock);
        }
    }
    LeaveCriticalSection(&st->lock);
}

typedef struct ThreadCtx {
    ServerState* st;
    Client* client;
} ThreadCtx;

// Per-client worker thread. Handles AUTH and subsequent commands.
static DWORD WINAPI client_thread(LPVOID param) {
    ThreadCtx* ctx = (ThreadCtx*)param;
    ServerState* st = ctx->st;
    Client* c = ctx->client;
    free(ctx);

    // Protocol greeting so the client can confirm server version.
    (void)send_text(c->sock, "HELLO 1");

    for (;;) {
        uint8_t* payload = NULL;
        uint32_t payload_len = 0;
        if (!chat_frame_recv_alloc(c->sock, &payload, &payload_len, CHAT_MAX_FRAME)) break;

        ChatCmd cmd;
        if (!chat_cmd_parse_inplace((char*)payload, &cmd) || !cmd.cmd) {
            free(payload);
            (void)send_err(c->sock, "BAD", "Malformed command");
            continue;
        }

        // First command must be AUTH username password.
        if (!c->authed) {
            if (_stricmp(cmd.cmd, "AUTH") != 0 || !cmd.arg1 || !cmd.arg2) {
                free(payload);
                (void)send_err(c->sock, "AUTH", "Expected AUTH username password");
                continue;
            }

            const char* username = cmd.arg1;
            const char* password = cmd.arg2;
            if (strlen(username) > CHAT_NAME_MAX) {
                free(payload);
                (void)send_err(c->sock, "AUTH", "Username too long");
                continue;
            }
            if (strcmp(password, st->password) != 0) {
                free(payload);
                (void)send_err(c->sock, "AUTH", "Bad password");
                break;
            }

            EnterCriticalSection(&st->lock);
            if (state_find_client_by_name(st, username)) {
                LeaveCriticalSection(&st->lock);
                free(payload);
                (void)send_err(c->sock, "AUTH", "Username already in use");
                break;
            }
            strncpy(c->username, username, CHAT_NAME_MAX);
            c->username[CHAT_NAME_MAX] = 0;
            c->authed = 1;
            LeaveCriticalSection(&st->lock);

            free(payload);
            (void)send_ok(c->sock, "AUTH");
            continue;
        }

        if (_stricmp(cmd.cmd, "JOIN") == 0) {
            if (!cmd.arg1) {
                free(payload);
                (void)send_err(c->sock, "JOIN", "Missing room");
                continue;
            }

            const char* room_name = cmd.arg1;
            if (strlen(room_name) > CHAT_NAME_MAX) {
                free(payload);
                (void)send_err(c->sock, "JOIN", "Room name too long");
                continue;
            }

            char ev[256];
            // Create room if needed and add member under lock.
            EnterCriticalSection(&st->lock);
            Room* r = state_get_or_create_room(st, room_name);
            if (r) room_add_member(r, c);
            LeaveCriticalSection(&st->lock);

            free(payload);
            if (!r) {
                (void)send_err(c->sock, "JOIN", "Server out of memory");
                continue;
            }
            (void)send_ok(c->sock, "JOIN");
            if (chat_cmd_format(ev, sizeof(ev), "USERJOIN", r->name, c->username, NULL)) {
                broadcast_room(st, r, ev);
            }
            continue;
        }

        if (_stricmp(cmd.cmd, "LEAVE") == 0) {
            if (!cmd.arg1) {
                free(payload);
                (void)send_err(c->sock, "LEAVE", "Missing room");
                continue;
            }
            const char* room_name = cmd.arg1;
            char ev[256];

            // Remove member under lock if room exists.
            EnterCriticalSection(&st->lock);
            Room* r = state_find_room(st, room_name);
            if (r) room_remove_member(r, c);
            LeaveCriticalSection(&st->lock);

            free(payload);
            (void)send_ok(c->sock, "LEAVE");
            if (r && chat_cmd_format(ev, sizeof(ev), "USERLEAVE", r->name, c->username, NULL)) {
                broadcast_room(st, r, ev);
            }
            continue;
        }

        if (_stricmp(cmd.cmd, "MSG") == 0) {
            if (!cmd.arg1 || !cmd.text) {
                free(payload);
                (void)send_err(c->sock, "MSG", "Expected MSG room :text");
                continue;
            }

            const char* room_name = cmd.arg1;
            const char* text = cmd.text;

            Room* r = NULL;
            // Validate membership under lock.
            EnterCriticalSection(&st->lock);
            r = state_find_room(st, room_name);
            int allowed = (r && room_has_member(r, c));
            LeaveCriticalSection(&st->lock);

            if (!allowed) {
                free(payload);
                (void)send_err(c->sock, "MSG", "Not in room");
                continue;
            }

            char out[1024];
            if (!chat_cmd_format(out, sizeof(out), "ROOMMSG", room_name, c->username, text)) {
                free(payload);
                (void)send_err(c->sock, "MSG", "Message too long");
                continue;
            }

            free(payload);
            broadcast_room(st, r, out);
            continue;
        }

        if (_stricmp(cmd.cmd, "PM") == 0) {
            if (!cmd.arg1 || !cmd.text) {
                free(payload);
                (void)send_err(c->sock, "PM", "Expected PM user :text");
                continue;
            }
            const char* target = cmd.arg1;
            const char* text = cmd.text;

            Client* dst = NULL;
            // Lookup recipient under lock.
            EnterCriticalSection(&st->lock);
            dst = state_find_client_by_name(st, target);
            LeaveCriticalSection(&st->lock);

            if (!dst) {
                free(payload);
                (void)send_err(c->sock, "PM", "User not found");
                continue;
            }

            char out[1024];
            if (!chat_cmd_format(out, sizeof(out), "PRIVMSG", c->username, NULL, text)) {
                free(payload);
                (void)send_err(c->sock, "PM", "Message too long");
                continue;
            }
            free(payload);
            (void)send_text(dst->sock, out);
            (void)send_ok(c->sock, "PM");
            continue;
        }

        if (_stricmp(cmd.cmd, "PING") == 0) {
            // Keepalive response.
            free(payload);
            (void)send_text(c->sock, "PONG");
            continue;
        }

        free(payload);
        (void)send_err(c->sock, "CMD", "Unknown command");
    }

    shutdown(c->sock, SD_BOTH);
    closesocket(c->sock);

    // Remove from global client list under lock.
    EnterCriticalSection(&st->lock);
    Client** pp = &st->clients;
    while (*pp) {
        if (*pp == c) {
            *pp = c->next;
            break;
        }
        pp = &((*pp)->next);
    }
    LeaveCriticalSection(&st->lock);

    if (c->authed) {
        broadcast_user_leave(st, c);
        printf("Disconnected: %s\n", c->username);
    } else {
        printf("Disconnected (unauth)\n");
    }

    CloseHandle(c->thread);
    free(c);
    return 0;
}

static void usage(void) {
    printf("chat_server --password <pw> [--port <port>]\n");
}

int main(int argc, char** argv) {
    const char* port = CHAT_PORT_DEFAULT;
    const char* password = NULL;

    // Parse command-line args.
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            port = argv[++i];
        } else if (strcmp(argv[i], "--password") == 0 && i + 1 < argc) {
            password = argv[++i];
        } else {
            usage();
            return 2;
        }
    }

    if (!password || password[0] == 0) {
        usage();
        return 2;
    }

    // Initialize Winsock.
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        printf("WSAStartup failed\n");
        return 1;
    }

    // Resolve bind address for listening socket.
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = AI_PASSIVE;

    struct addrinfo* res = NULL;
    int gai_rc = getaddrinfo(NULL, port, &hints, &res);
    if (gai_rc != 0) {
        printf("getaddrinfo failed: %s\n", gai_strerrorA(gai_rc));
        WSACleanup();
        return 1;
    }

    SOCKET listen_sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (listen_sock == INVALID_SOCKET) {
        printf("socket failed\n");
        freeaddrinfo(res);
        WSACleanup();
        return 1;
    }

    int yes = 1;
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&yes, sizeof(yes));

    if (bind(listen_sock, res->ai_addr, (int)res->ai_addrlen) != 0) {
        printf("bind failed\n");
        closesocket(listen_sock);
        freeaddrinfo(res);
        WSACleanup();
        return 1;
    }
    freeaddrinfo(res);

    if (listen(listen_sock, SOMAXCONN) != 0) {
        printf("listen failed\n");
        closesocket(listen_sock);
        WSACleanup();
        return 1;
    }

    ServerState st;
    memset(&st, 0, sizeof(st));
    InitializeCriticalSection(&st.lock);
    st.password = password;

    printf("Server listening on port %s\n", port);

    // Accept clients and spawn worker threads.
    for (;;) {
        SOCKET client_sock = accept(listen_sock, NULL, NULL);
        if (client_sock == INVALID_SOCKET) break;

        Client* c = (Client*)calloc(1, sizeof(*c));
        if (!c) {
            closesocket(client_sock);
            continue;
        }
        c->sock = client_sock;

        EnterCriticalSection(&st.lock);
        c->next = st.clients;
        st.clients = c;
        LeaveCriticalSection(&st.lock);

        ThreadCtx* ctx = (ThreadCtx*)malloc(sizeof(*ctx));
        if (!ctx) {
            closesocket(client_sock);
            continue;
        }
        ctx->st = &st;
        ctx->client = c;
        c->thread = CreateThread(NULL, 0, client_thread, ctx, 0, NULL);
        if (!c->thread) {
            closesocket(client_sock);
            free(ctx);
            continue;
        }

        printf("Client connected\n");
    }

    closesocket(listen_sock);
    DeleteCriticalSection(&st.lock);
    WSACleanup();
    return 0;
}
