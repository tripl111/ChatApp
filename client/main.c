#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <commctrl.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "chat_cmd.h"
#include "chat_frame.h"
#include "chat_utf8.h"

#define APP_TITLE L"ChatApp Client"

#define IDC_HOST 101
#define IDC_PORT 102
#define IDC_USER 103
#define IDC_PASS 104
#define IDC_CONNECT 105
#define IDC_ROOM 106
#define IDC_JOIN 107
#define IDC_LOG 108
#define IDC_INPUT 109
#define IDC_SEND 110

#define WM_APP_NET_LINE (WM_APP + 1)
#define WM_APP_NET_STATUS (WM_APP + 2)

typedef struct AppState {
    HWND hwnd;
    HWND host_label;
    HWND host_edit;
    HWND port_label;
    HWND port_edit;
    HWND user_label;
    HWND user_edit;
    HWND pass_label;
    HWND pass_edit;
    HWND connect_btn;
    HWND room_label;
    HWND room_edit;
    HWND join_btn;
    HWND log_edit;
    HWND input_edit;
    HWND send_btn;

    SOCKET sock;
    HANDLE net_thread;
    CRITICAL_SECTION send_lock;

    int connected;
    char current_room[32];
} AppState;

typedef struct NetStart {
    HWND hwnd;
    char host[256];
    char port[16];
    char user[64];
    char pass[64];
} NetStart;

static int starts_with(const char* s, const char* pfx) {
    return s && pfx && strncmp(s, pfx, strlen(pfx)) == 0;
}

static void ui_append_line(AppState* st, const wchar_t* line) {
    if (!line) return;
    int len = GetWindowTextLengthW(st->log_edit);
    SendMessageW(st->log_edit, EM_SETSEL, (WPARAM)len, (LPARAM)len);
    SendMessageW(st->log_edit, EM_REPLACESEL, 0, (LPARAM)line);
    SendMessageW(st->log_edit, EM_REPLACESEL, 0, (LPARAM)L"\r\n");
}

static void ui_set_connected(AppState* st, int connected) {
    st->connected = connected;
    EnableWindow(st->connect_btn, !connected);
    EnableWindow(st->join_btn, connected);
    EnableWindow(st->send_btn, connected);
    EnableWindow(st->input_edit, connected);
}

static int ui_get_text_utf8(HWND hwnd, char* out, int out_cap) {
    if (!out || out_cap <= 0) return 0;
    out[0] = 0;

    int wlen = GetWindowTextLengthW(hwnd);
    wchar_t* wbuf = (wchar_t*)malloc(((size_t)wlen + 1) * sizeof(wchar_t));
    if (!wbuf) return 0;
    GetWindowTextW(hwnd, wbuf, wlen + 1);
    char* utf8 = chat_wide_to_utf8_alloc(wbuf);
    free(wbuf);
    if (!utf8) return 0;
    strncpy(out, utf8, (size_t)out_cap - 1);
    out[out_cap - 1] = 0;
    free(utf8);
    return 1;
}

static int client_send_payload(AppState* st, const char* payload) {
    if (!st->connected || st->sock == INVALID_SOCKET) return 0;
    EnterCriticalSection(&st->send_lock);
    int ok = chat_frame_send(st->sock, payload, (uint32_t)strlen(payload));
    LeaveCriticalSection(&st->send_lock);
    return ok;
}

static int client_send_cmd(AppState* st, const char* cmd, const char* arg1, const char* arg2, const char* text) {
    char buf[1024];
    if (!chat_cmd_format(buf, sizeof(buf), cmd, arg1, arg2, text)) return 0;
    return client_send_payload(st, buf);
}

static DWORD WINAPI net_thread_main(LPVOID param) {
    NetStart* ns = (NetStart*)param;

    PostMessageW(ns->hwnd, WM_APP_NET_LINE, 0, (LPARAM)_strdup("Connecting..."));

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    struct addrinfo* res = NULL;
    int gai_rc = getaddrinfo(ns->host, ns->port, &hints, &res);
    if (gai_rc != 0) {
        PostMessageW(ns->hwnd, WM_APP_NET_LINE, 0, (LPARAM)_strdup("DNS/addr lookup failed"));
        PostMessageW(ns->hwnd, WM_APP_NET_STATUS, 0, 0);
        free(ns);
        return 0;
    }

    SOCKET sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sock == INVALID_SOCKET) {
        freeaddrinfo(res);
        PostMessageW(ns->hwnd, WM_APP_NET_LINE, 0, (LPARAM)_strdup("socket() failed"));
        PostMessageW(ns->hwnd, WM_APP_NET_STATUS, 0, 0);
        free(ns);
        return 0;
    }

    if (connect(sock, res->ai_addr, (int)res->ai_addrlen) != 0) {
        closesocket(sock);
        freeaddrinfo(res);
        PostMessageW(ns->hwnd, WM_APP_NET_LINE, 0, (LPARAM)_strdup("connect() failed"));
        PostMessageW(ns->hwnd, WM_APP_NET_STATUS, 0, 0);
        free(ns);
        return 0;
    }
    freeaddrinfo(res);

    PostMessageW(ns->hwnd, WM_APP_NET_STATUS, 1, (LPARAM)sock);

    uint8_t* payload = NULL;
    uint32_t payload_len = 0;
    if (!chat_frame_recv_alloc(sock, &payload, &payload_len, CHAT_MAX_FRAME)) {
        shutdown(sock, SD_BOTH);
        closesocket(sock);
        PostMessageW(ns->hwnd, WM_APP_NET_LINE, 0, (LPARAM)_strdup("Disconnected during HELLO"));
        PostMessageW(ns->hwnd, WM_APP_NET_STATUS, 0, 0);
        free(ns);
        return 0;
    }

    if (strcmp((char*)payload, "HELLO 1") != 0) {
        free(payload);
        shutdown(sock, SD_BOTH);
        closesocket(sock);
        PostMessageW(ns->hwnd, WM_APP_NET_LINE, 0, (LPARAM)_strdup("Bad server HELLO"));
        PostMessageW(ns->hwnd, WM_APP_NET_STATUS, 0, 0);
        free(ns);
        return 0;
    }
    free(payload);

    char auth[256];
    snprintf(auth, sizeof(auth), "AUTH %s %s", ns->user, ns->pass);
    if (!chat_frame_send(sock, auth, (uint32_t)strlen(auth))) {
        shutdown(sock, SD_BOTH);
        closesocket(sock);
        PostMessageW(ns->hwnd, WM_APP_NET_LINE, 0, (LPARAM)_strdup("AUTH send failed"));
        PostMessageW(ns->hwnd, WM_APP_NET_STATUS, 0, 0);
        free(ns);
        return 0;
    }

    for (;;) {
        uint8_t* p = NULL;
        uint32_t n = 0;
        if (!chat_frame_recv_alloc(sock, &p, &n, CHAT_MAX_FRAME)) break;
        PostMessageW(ns->hwnd, WM_APP_NET_LINE, 0, (LPARAM)p);
    }

    shutdown(sock, SD_BOTH);
    closesocket(sock);
    PostMessageW(ns->hwnd, WM_APP_NET_STATUS, 0, (LPARAM)_strdup("Disconnected"));
    free(ns);
    return 0;
}

static void layout(AppState* st) {
    RECT rc;
    GetClientRect(st->hwnd, &rc);

    int pad = 8;
    int row_h = 24;
    int btn_w = 90;

    int x = pad;
    int y = pad;

    int label_w = 40;
    int host_w = 160;
    int port_w = 70;
    int user_w = 110;
    int pass_w = 110;

    int cx = x;
    MoveWindow(st->host_label, cx, y, label_w, row_h, TRUE);
    cx += label_w;
    MoveWindow(st->host_edit, cx, y, host_w, row_h, TRUE);
    cx += host_w + pad;

    MoveWindow(st->port_label, cx, y, label_w, row_h, TRUE);
    cx += label_w;
    MoveWindow(st->port_edit, cx, y, port_w, row_h, TRUE);
    cx += port_w + pad;

    MoveWindow(st->user_label, cx, y, label_w, row_h, TRUE);
    cx += label_w;
    MoveWindow(st->user_edit, cx, y, user_w, row_h, TRUE);
    cx += user_w + pad;

    MoveWindow(st->pass_label, cx, y, label_w, row_h, TRUE);
    cx += label_w;
    MoveWindow(st->pass_edit, cx, y, pass_w, row_h, TRUE);
    MoveWindow(st->connect_btn, rc.right - pad - btn_w, y, btn_w, row_h, TRUE);

    y += row_h + pad;

    cx = x;
    MoveWindow(st->room_label, cx, y, label_w, row_h, TRUE);
    cx += label_w;
    MoveWindow(st->room_edit, cx, y, host_w, row_h, TRUE);
    cx += host_w + pad;
    MoveWindow(st->join_btn, cx, y, btn_w, row_h, TRUE);

    y += row_h + pad;

    int input_h = row_h;
    int log_h = rc.bottom - y - pad - input_h - pad;
    if (log_h < 50) log_h = 50;

    MoveWindow(st->log_edit, pad, y, rc.right - pad * 2, log_h, TRUE);
    y += log_h + pad;
    MoveWindow(st->input_edit, pad, y, rc.right - pad * 3 - btn_w, input_h, TRUE);
    MoveWindow(st->send_btn, rc.right - pad - btn_w, y, btn_w, input_h, TRUE);
}

static void send_input(AppState* st) {
    char input[512];
    if (!ui_get_text_utf8(st->input_edit, input, (int)sizeof(input))) return;
    if (input[0] == 0) return;

    SetWindowTextW(st->input_edit, L"");

    if (starts_with(input, "/join ")) {
        const char* room = input + 6;
        strncpy(st->current_room, room, sizeof(st->current_room) - 1);
        st->current_room[sizeof(st->current_room) - 1] = 0;
        (void)client_send_cmd(st, "JOIN", room, NULL, NULL);
        return;
    }

    if (starts_with(input, "/leave ")) {
        const char* room = input + 7;
        (void)client_send_cmd(st, "LEAVE", room, NULL, NULL);
        if (_stricmp(st->current_room, room) == 0) st->current_room[0] = 0;
        return;
    }

    if (starts_with(input, "/pm ")) {
        const char* rest = input + 4;
        const char* space = strchr(rest, ' ');
        if (!space) return;
        char user[64];
        size_t ulen = (size_t)(space - rest);
        if (ulen >= sizeof(user)) ulen = sizeof(user) - 1;
        memcpy(user, rest, ulen);
        user[ulen] = 0;
        const char* msg = space + 1;
        (void)client_send_cmd(st, "PM", user, NULL, msg);
        return;
    }

    if (st->current_room[0] == 0) return;
    (void)client_send_cmd(st, "MSG", st->current_room, NULL, input);
}

static LRESULT CALLBACK wndproc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    AppState* st = (AppState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);

    switch (msg) {
    case WM_CREATE: {
        st = (AppState*)((CREATESTRUCTW*)lparam)->lpCreateParams;
        st->hwnd = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)st);

        HFONT font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);

        st->host_label = CreateWindowExW(0, L"STATIC", L"Host",
            WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd, NULL, NULL, NULL);
        st->host_edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"127.0.0.1",
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 0, 0, 0, 0, hwnd, (HMENU)IDC_HOST, NULL, NULL);
        st->port_label = CreateWindowExW(0, L"STATIC", L"Port",
            WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd, NULL, NULL, NULL);
        st->port_edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"5555",
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 0, 0, 0, 0, hwnd, (HMENU)IDC_PORT, NULL, NULL);
        st->user_label = CreateWindowExW(0, L"STATIC", L"User",
            WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd, NULL, NULL, NULL);
        st->user_edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"user",
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 0, 0, 0, 0, hwnd, (HMENU)IDC_USER, NULL, NULL);
        st->pass_label = CreateWindowExW(0, L"STATIC", L"Pass",
            WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd, NULL, NULL, NULL);
        st->pass_edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"pw",
            WS_CHILD | WS_VISIBLE | ES_PASSWORD | ES_AUTOHSCROLL, 0, 0, 0, 0, hwnd, (HMENU)IDC_PASS, NULL, NULL);
        st->connect_btn = CreateWindowExW(0, L"BUTTON", L"Connect",
            WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd, (HMENU)IDC_CONNECT, NULL, NULL);

        st->room_label = CreateWindowExW(0, L"STATIC", L"Room",
            WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd, NULL, NULL, NULL);
        st->room_edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"lobby",
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 0, 0, 0, 0, hwnd, (HMENU)IDC_ROOM, NULL, NULL);
        st->join_btn = CreateWindowExW(0, L"BUTTON", L"Join",
            WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd, (HMENU)IDC_JOIN, NULL, NULL);

        st->log_edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY | WS_VSCROLL,
            0, 0, 0, 0, hwnd, (HMENU)IDC_LOG, NULL, NULL);

        st->input_edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
            0, 0, 0, 0, hwnd, (HMENU)IDC_INPUT, NULL, NULL);
        st->send_btn = CreateWindowExW(0, L"BUTTON", L"Send",
            WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd, (HMENU)IDC_SEND, NULL, NULL);

        SendMessageW(st->host_label, WM_SETFONT, (WPARAM)font, TRUE);
        SendMessageW(st->host_edit, WM_SETFONT, (WPARAM)font, TRUE);
        SendMessageW(st->port_label, WM_SETFONT, (WPARAM)font, TRUE);
        SendMessageW(st->port_edit, WM_SETFONT, (WPARAM)font, TRUE);
        SendMessageW(st->user_label, WM_SETFONT, (WPARAM)font, TRUE);
        SendMessageW(st->user_edit, WM_SETFONT, (WPARAM)font, TRUE);
        SendMessageW(st->pass_label, WM_SETFONT, (WPARAM)font, TRUE);
        SendMessageW(st->pass_edit, WM_SETFONT, (WPARAM)font, TRUE);
        SendMessageW(st->connect_btn, WM_SETFONT, (WPARAM)font, TRUE);
        SendMessageW(st->room_label, WM_SETFONT, (WPARAM)font, TRUE);
        SendMessageW(st->room_edit, WM_SETFONT, (WPARAM)font, TRUE);
        SendMessageW(st->join_btn, WM_SETFONT, (WPARAM)font, TRUE);
        SendMessageW(st->log_edit, WM_SETFONT, (WPARAM)font, TRUE);
        SendMessageW(st->input_edit, WM_SETFONT, (WPARAM)font, TRUE);
        SendMessageW(st->send_btn, WM_SETFONT, (WPARAM)font, TRUE);

        InitializeCriticalSection(&st->send_lock);
        st->sock = INVALID_SOCKET;
        st->current_room[0] = 0;
        ui_set_connected(st, 0);
        ui_append_line(st, L"Commands: /join room, /leave room, /pm user message");
        return 0;
    }
    case WM_SIZE:
        if (st) layout(st);
        return 0;
    case WM_COMMAND: {
        if (!st) break;
        int id = LOWORD(wparam);
        if (id == IDC_CONNECT) {
            if (st->connected) return 0;

            NetStart* ns = (NetStart*)calloc(1, sizeof(*ns));
            if (!ns) return 0;
            ns->hwnd = hwnd;
            ui_get_text_utf8(st->host_edit, ns->host, (int)sizeof(ns->host));
            ui_get_text_utf8(st->port_edit, ns->port, (int)sizeof(ns->port));
            ui_get_text_utf8(st->user_edit, ns->user, (int)sizeof(ns->user));
            ui_get_text_utf8(st->pass_edit, ns->pass, (int)sizeof(ns->pass));

            EnableWindow(st->connect_btn, FALSE);
            st->net_thread = CreateThread(NULL, 0, net_thread_main, ns, 0, NULL);
            if (!st->net_thread) {
                EnableWindow(st->connect_btn, TRUE);
                free(ns);
            }
            return 0;
        }
        if (id == IDC_JOIN) {
            char room[64];
            if (!ui_get_text_utf8(st->room_edit, room, (int)sizeof(room))) return 0;
            if (room[0] == 0) return 0;
            strncpy(st->current_room, room, sizeof(st->current_room) - 1);
            st->current_room[sizeof(st->current_room) - 1] = 0;
            (void)client_send_cmd(st, "JOIN", room, NULL, NULL);
            return 0;
        }
        if (id == IDC_SEND) {
            send_input(st);
            return 0;
        }
        if (id == IDC_INPUT && HIWORD(wparam) == EN_UPDATE) {
            return 0;
        }
        break;
    }
    case WM_APP_NET_STATUS: {
        if (!st) break;
        if (wparam == 1) {
            st->sock = (SOCKET)lparam;
            ui_set_connected(st, 1);
            ui_append_line(st, L"Connected. Waiting for AUTH response...");
        } else {
            char* msg8 = (char*)lparam;
            if (msg8) {
                wchar_t* w = chat_utf8_to_wide_alloc(msg8);
                if (w) {
                    ui_append_line(st, w);
                    free(w);
                }
                free(msg8);
            }
            ui_set_connected(st, 0);
            st->sock = INVALID_SOCKET;
        }
        return 0;
    }
    case WM_APP_NET_LINE: {
        if (!st) break;
        char* line8 = (char*)lparam;
        if (!line8) return 0;

        wchar_t* w = chat_utf8_to_wide_alloc(line8);
        if (w) {
            ui_append_line(st, w);
            free(w);
        }
        free(line8);
        return 0;
    }
    case WM_DESTROY:
        if (st) {
            if (st->sock != INVALID_SOCKET) {
                shutdown(st->sock, SD_BOTH);
                closesocket(st->sock);
                st->sock = INVALID_SOCKET;
            }
            if (st->net_thread) {
                WaitForSingleObject(st->net_thread, 2000);
                CloseHandle(st->net_thread);
                st->net_thread = NULL;
            }
            DeleteCriticalSection(&st->send_lock);
        }
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE hPrev, PWSTR cmdLine, int show) {
    (void)hPrev;
    (void)cmdLine;

    INITCOMMONCONTROLSEX icc;
    memset(&icc, 0, sizeof(icc));
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icc);

    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return 1;

    AppState st;
    memset(&st, 0, sizeof(st));

    WNDCLASSW wc;
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = wndproc;
    wc.hInstance = hInst;
    wc.lpszClassName = L"ChatAppClientWindow";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClassW(&wc);

    HWND hwnd = CreateWindowExW(
        0,
        wc.lpszClassName,
        APP_TITLE,
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 900, 600,
        NULL, NULL, hInst, &st);

    ShowWindow(hwnd, show);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    WSACleanup();
    return 0;
}
