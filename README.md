# MiniKDEConnect Prototype

This is a minimal KDE Connect client prototype that implements discovery, pairing, and the ping plugin. It is based on the protocol reference and the Android LAN backend behavior.

## Features
- UDP discovery broadcast/listen on port 1716
- TCP identity handshake and TLS upgrade
- Pairing (`kdeconnect.pair`) with verification key
- Ping send/receive (`kdeconnect.ping`)

## Build

This project uses CMake and FetchContent for `nlohmann/json`, and links with MbedTLS.

```bash
cmake -S . -B build
cmake --build build
```

## Run

```bash
./build/MiniKDEConnect
```

## Commands

- `list`
- `pair <deviceId>`
- `accept <deviceId>`
- `reject <deviceId>`
- `ping <deviceId> [message]`
- `quit`

## Notes
- Pairing stores peer certificates in `~/.config/minikdeconnect/paired/`.
- This prototype does not implement mDNS discovery yet (UDP broadcast only).
- Only paired devices will accept or send ping packets.
