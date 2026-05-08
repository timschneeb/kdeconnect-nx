# MiniKDEConnect Prototype

This is a minimal KDE Connect client prototype that implements discovery, pairing, and the ping plugin. It is based on the protocol reference and the Android LAN backend behavior.

## Features
- UDP discovery broadcast/listen on port 1716
- mDNS discovery advertise/query for `_kdeconnect._udp`
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
- Discovery uses both UDP broadcast and mDNS.
- Only paired devices will accept or send ping packets.
