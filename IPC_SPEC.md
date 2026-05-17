# MiniKDEConnect IPC Specification

## Overview

MiniKDEConnect exposes a system service (`kdec:srv`) over Nintendo Switch HiPC IPC. Clients connect via the service manager (SM), send command requests, and receive synchronous responses. All communication is binary and follows the CMIF (Common Message Interface Format) framing used by libnx.

**API version:** `1` (`KDEC_IPC_API_VERSION`)  
**Service name:** `kdec:srv`  
**Max concurrent sessions:** 2

---

## Transport

The protocol rides the Switch kernel's native HiPC layer:

- The server registers `kdec:srv` with SM via `smRegisterService()`.
- Clients acquire a session handle via `smGetService("kdec:srv")`.
- Each request is a single `svcReplyAndReceive` round-trip (synchronous, blocking).
- Sessions are closed by the client sending a `CmifCommandType_Close` message or by the server on error.

---

## Message Format

Every request and response is wrapped in a 16-byte CMIF header:

```
Offset  Size  Field
------  ----  -----
0       8     magic   : CMIF_IN_HEADER_MAGIC (requests) / CMIF_OUT_HEADER_MAGIC (responses)
8       8     value   : command ID (requests) / result code (responses)
```

Magic values are enforced; a request with the wrong magic is rejected with `LibnxError_BadInput`.

Following the header is the command-specific payload, either inline (≤ 240 bytes) or via HiPC buffer descriptors for larger data.

---

## Result Codes

| Value | Meaning                                                   |
|-------|-----------------------------------------------------------|
| `0x0000` | Success                                                   |
| `MAKERESULT(Module_Libnx, LibnxError_BadInput)` | Invalid parameters / bad magic                            |
| `MAKERESULT(Module_Libnx, LibnxError_OutOfMemory)` | Session limit reached                                     |
| `MAKERESULT(Module_Libnx, LibnxError_NotFound)` | Invalid handle / session                                  |
| `MAKERESULT(Module_Libnx, LibnxError_NotInitialized)` | Service not running                                       |
| `1` | Unknown command ID (TODO: this should be a proper RESULT) |

TODO: use a custom module id instead of libnx.

TODO: add custom error ids if needed.

TODO: investigate on how to avoid polling (device list, player state) and maybe something event-based instead. 

---

## Serialization

Commands that return variable-length data use a single out buffer (`SfBufferAttr_Out`) containing a packed binary payload. Fields are written in the order listed. Primitive types are packed as-is. Strings are length-prefixed: `uint16_t len` followed by `len` bytes (no null terminator). For array responses, entries are serialized back-to-back; the count is returned inline.

---

## Data Types

### `DevicePairState` (uint8_t)

| Value | Name | Description |
|-------|------|-------------|
| `0` | `None` | Not paired |
| `1` | `RequestedByMe` | Outgoing pairing request pending |
| `2` | `RequestedByPeer` | Incoming pairing request pending |
| `3` | `Paired` | Successfully paired |

### `KdecDeviceInfo`

- `string id`
- `string name`
- `DevicePairState pair_state`
- `bool is_connected`
- `bool supports_find_my_phone`
- `int8_t battery_level` -1-100 (-1 = no battery)

### `KdecMediaInfo`

- `string device_id`
- `string player`
- `string title`
- `string artist`
- `string album`
- `bool is_playing`
- `bool can_play`
- `bool can_pause`
- `bool can_go_next`
- `bool can_go_previous`
- `bool can_seek`
- `int32_t volume`: 0–100
- `int64_t position`: current position in ms
- `int64_t length`: total duration in ms

### `KdecMediaAction` (uint8_t)

| Value | Name | `value` field |
|-------|------|---------------|
| `0` | `Play` | ignored |
| `1` | `Pause` | ignored |
| `2` | `PlayPause` | ignored |
| `3` | `Stop` | ignored |
| `4` | `Next` | ignored |
| `5` | `Previous` | ignored |
| `6` | `SetVolume` | new volume, 0–100 |
| `7` | `Seek` | relative offset in ms |
| `8` | `SetPosition` | absolute position in ms |

### `KdecCommandEntry`

- `string id`
- `string name`

### `KdecSettingType` (uint8_t)

| Value | Name |
|-------|------|
| `0`   | `Bool` |
| `1`   | `Int` |
| `2`   | `String` |

### `KdecSettingEntry`

- `string key`
- `KdecSettingType type`
- `value`: `bool` for `Bool`; `int32_t` for `Int`; `string` for `String`

---

## Commands

### `GetApiVersion` (0)

Returns the server's API version number.

**Request:** none

**Response:** `uint32_t version`: API version (currently `1`)

---

### `GetDeviceCount` (1)

Returns the number of known devices.

**Request:** none

**Response:** `uint32_t count`

---

### `GetDevices` (2)

Returns device information for all known devices.

**Request:** Out buffer (`SfBufferAttr_Out`): caller-allocated buffer; server writes serialized `KdecDeviceInfo` entries back-to-back

**Response:** `uint32_t count`: number of entries written

---

### `RequestPair` (3)

Initiates a pairing request to the specified device.

**Request:** In buffer (`SfBufferAttr_In`): null-terminated device ID string

**Response:** none

---

### `AcceptPair` (4)

Accepts an incoming pairing request from the specified device.

**Request:** In buffer (`SfBufferAttr_In`): null-terminated device ID string

**Response:** none

---

### `RejectPair` (5)

Rejects an incoming pairing request from the specified device.

**Request:** In buffer (`SfBufferAttr_In`): null-terminated device ID string

**Response:** none

---

### `Unpair` (6)

Removes the pairing with the specified device.

**Request:** In buffer (`SfBufferAttr_In`): null-terminated device ID string

**Response:** none

---

## Planned Commands

Opcodes are provisional.

### `Ping` (7)

Sends a ping notification to the specified device.

**Request:** In buffer (`SfBufferAttr_In`): null-terminated device ID string

**Response:** none

---

### `GetMediaInfo` (8)

Returns the media player state from whichever paired device most recently updated its player. Currently requires polling; event-based push is a future consideration.

**Request:** Out buffer (`SfBufferAttr_Out`): caller-allocated buffer; server writes one serialized `KdecMediaInfo`

**Response:** `uint32_t bytes_written`

---

### `SendMediaAction` (9)

Sends a media control action to the active device's player.

**Request:**
- `KdecMediaAction action` (inline)
- `int64_t value` (inline): interpretation depends on `action`; ignored if not applicable

**Response:** none

---

### `GetCommandList` (10)

Returns the list of runnable commands registered on the specified device.

**Request:**
- In buffer (`SfBufferAttr_In`): null-terminated device ID string
- Out buffer (`SfBufferAttr_Out`): caller-allocated buffer; server writes serialized `KdecCommandEntry` entries back-to-back

**Response:** `uint32_t count`: number of entries written

---

### `RunCommand` (11)

Runs a command on the specified device.

**Request:**
- In buffer 0 (`SfBufferAttr_In`): null-terminated device ID string
- In buffer 1 (`SfBufferAttr_In`): null-terminated command UUID string

**Response:** none

---

### `ReadSetting` (12)

Reads a setting value by key.

**Request:**
- In buffer (`SfBufferAttr_In`): null-terminated setting key string
- `KdecSettingType type` (inline)

**Response:** Out buffer (`SfBufferAttr_Out`): value bytes; interpretation depends on `type`

---

### `WriteSetting` (13)

Writes a setting value by key.

**Request:**
- In buffer 0 (`SfBufferAttr_In`): null-terminated setting key string
- `KdecSettingType type` (inline)
- In buffer 1 (`SfBufferAttr_In`): value bytes

**Response:** none

---

### `GetAllSettings` (14)

Returns all settings and their current values.

**Request:** Out buffer (`SfBufferAttr_Out`): caller-allocated buffer; server writes serialized `KdecSettingEntry` entries back-to-back

**Response:** `uint32_t count`: number of entries written

---

## Adding New Commands

1. Assign the next sequential opcode in the `KdecIpcCmd` enum in `common/src/kdec/ipc.h`.
2. Add the client-side wrapper in `common/src/kdec/ipc_client.cpp` and its declaration in `ipc_client.h`.
3. Add a case to the dispatch switch in `sysmodule/src/ipc/ipc_service.cpp`.
4. Document the new command in this file following the format above.