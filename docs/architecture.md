# Architecture

```mermaid
flowchart LR
    subgraph ClientApp["Client app (Win32)"]
        UI["UI thread (Win32 message loop)"]
        UIState["UI state (active room, DM, scrollback)"]
        Net["Network thread (Winsock TCP client)"]
        ProtoC["Protocol module (framing + command parse)"]

        UI --> UIState
        UI --> ProtoC
        ProtoC --> Net
        Net --> ProtoC
        ProtoC --> UI
    end

    subgraph ServerApp["Server app (console)"]
        Listener["Listener (Winsock TCP server)"]
        Handlers["Client handlers (1 thread per client)"]
        Router["Router (rooms + private messages)"]
        State["In-memory state (users, rooms, memberships)"]

        Listener --> Handlers
        Handlers --> Router
        Router <--> State
    end

    Net <--> Listener
```

Notes:
- Transport is TCP sockets on a LAN.
- Each TCP connection carries a stream of frames: `[uint32 length][UTF-8 payload]`.
- The payload is a command-text schema like `JOIN room` or `MSG room :text`.

