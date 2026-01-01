# Project structure

This repo is intentionally split into `client`, `server`, and `shared` so the protocol/framing code can be reused.

```
ChatApp/
  client/               Win32 GUI client (pure C)
  server/               Console server (pure C)
  shared/               Shared C code (protocol, framing, utils)
  CMakeLists.txt        CMake build (MSVC recommended)
  docs/                 Design docs and diagrams
    diagrams/            Mermaid sources
```

Recommended module responsibilities:
- `shared/`
  - Frame encoding/decoding (`uint32 length` + payload)
  - Command parsing/formatting (command-text schema)
  - Common constants and validation (username, room name)
- `server/`
  - Accept sockets, authenticate clients, manage rooms/users
  - Route/broadcast frames to correct recipients
- `client/`
  - Win32 UI (window, controls, input)
  - Background network thread and UI notifications
