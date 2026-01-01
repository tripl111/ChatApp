# Protocol

Transport:
- TCP sockets (Winsock)
- Frames: `[uint32 length, network byte order][UTF-8 payload bytes]`
- Max payload size: 64 KiB (`CHAT_MAX_FRAME`)

Command-text payload format:
- Tokens are space-separated
- Free text is introduced by ` :` and continues to the end of the payload

Examples:
- `HELLO 1`
- `AUTH alice pw`
- `JOIN lobby`
- `MSG lobby :hello everyone`
- `PM bob :hi`

Server events:
- `OK <what>`
- `ERR <code> :reason`
- `ROOMMSG <room> <fromUser> :text`
- `PRIVMSG <fromUser> :text`
- `USERJOIN <room> <user>`
- `USERLEAVE <room> <user>`

