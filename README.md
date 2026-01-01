# ChatApp (C, Win32, Winsock)

MVP chat app with:
- Windows server (console) in C using Winsock (TCP)
- Windows client (Win32 GUI) in C using Winsock (TCP)
- Length-prefixed frames with UTF-8 text command payloads
- Shared server password in plaintext (LAN MVP)

Design docs:
- `docs/architecture.md`
- `docs/sequence.md`
- `docs/project-structure.md`
- `docs/protocol.md`

## Build (Windows)

Prereqs:
- Visual Studio 2022 (or Build Tools) with the Windows SDK
- CMake 3.24+

Commands (Developer Command Prompt for VS):
```bat
cmake -S . -B build
cmake --build build --config Release
```

## Run

Server:
```bat
build\Release\chat_server.exe --password pw --port 5555
```

Client:
```bat
build\Release\chat_client.exe
```

Client commands (type into the input box):
- `/join room`
- `/leave room`
- `/pm user message`
