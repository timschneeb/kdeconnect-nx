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

Following the header is the command-specific payload. Simple parameters fit inline (≤ 240 bytes); large arrays are transferred via HiPC buffer descriptors.

---

## Result Codes

| Value | Meaning                                                   |
|-------|-----------------------------------------------------------|
| `0x0000` | Success                                                   |
| `MAKERESULT(Module_Libnx, LibnxError_BadInput)` | Invalid parameters / bad magic                            |
| `MAKERESULT(Module_Libnx, LibnxError_OutOfMemory)` | Session limit reached                                     |
| `MAKERESULT(Module_Libnx, LibnxError_NotFound)` | Invalid handle / session / key                            |
| `MAKERESULT(Module_Libnx, LibnxError_NotInitialized)` | Service not running                                       |
| `1` | Unknown command ID (TODO: this should be a proper RESULT) |

TODO: use a custom module id instead of libnx.

TODO: add custom error ids if needed.

TODO: investigate on how to avoid polling (device list, player state) and maybe something event-based instead. 

---

## Data Types

All structs are plain POD with fixed-size `char` arrays, 16-byte aligned (`__attribute__((aligned(16)))`). They are suitable for direct `memcpy` across the HiPC buffer boundary with no serialization layer.

### `DevicePairState` (uint8_t)

| Value | Name | Description |
|-------|------|-------------|
| `0` | `None` | Not paired |
| `1` | `RequestedByMe` | Outgoing pairing request pending |
| `2` | `RequestedByPeer` | Incoming pairing request pending |
| `3` | `Paired` | Successfully paired |

### `KdecDeviceInfo` (144 bytes)

| Offset | Size | Field |
|--------|------|-------|
| 0      | 64   | `char id[64]` |
| 64     | 64   | `char name[64]` |
| 128    | 1    | `DevicePairState pair_state` |
| 129    | 1    | `bool is_connected` |
| 130    | 1    | `bool supports_find_my_phone` |
| 131    | 1    | `int8_t battery_level` (-1 = no battery) |
| 132    | 12   | implicit trailing pad (16-byte alignment) |

### `KdecMediaInfo` (1888 bytes)

| Offset | Size | Field |
|--------|------|-------|
| 0      | 64   | `char device_id[64]` |
| 64     | 256  | `char player[256]` |
| 320    | 512  | `char title[512]` |
| 832    | 512  | `char artist[512]` |
| 1344   | 512  | `char album[512]` |
| 1856   | 8    | `int64_t position` (ms) |
| 1864   | 8    | `int64_t length` (ms) |
| 1872   | 4    | `int32_t volume` (0–100) |
| 1876   | 1    | `bool is_playing` |
| 1877   | 1    | `bool can_play` |
| 1878   | 1    | `bool can_pause` |
| 1879   | 1    | `bool can_go_next` |
| 1880   | 1    | `bool can_go_previous` |
| 1881   | 1    | `bool can_seek` |
| 1882   | 6    | `uint8_t _pad[6]` |

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

### `KdecVolumeSinkInfo` (208 bytes)

| Offset | Size | Field |
|--------|------|-------|
| 0      | 64   | `char name[64]` |
| 64     | 128  | `char description[128]` |
| 192    | 4    | `int32_t volume` (0–100) |
| 196    | 1    | `bool is_muted` |
| 197    | 1    | `bool is_default_output` |
| 198    | 10   | implicit trailing pad (16-byte alignment) |

### `KdecCommandEntry` (320 bytes)

| Offset | Size | Field |
|--------|------|-------|
| 0      | 64   | `char id[64]` |
| 64     | 256  | `char name[256]` |

### `KdecBoolSettingKey` (uint8_t enum)

Defined in `common/src/constants.h` as `enum class KdecBoolSettingKey : uint8_t`.

| Value | Name |
|-------|------|
| `0`   | `NotificationShowRemote` |
| `1`   | `NotificationShowOnConnect` |

### `KdecIntSettingKey` (uint8_t enum)

Defined in `common/src/constants.h` as `enum class KdecIntSettingKey : uint8_t`.

| Value | Name |
|-------|------|
| `0`   | `NotificationDuration` |

### `KdecWireWriteBoolSetting` (2 bytes)

| Offset | Size | Field |
|--------|------|-------|
| 0      | 1    | `KdecBoolSettingKey key` |
| 1      | 1    | `bool value` |

### `KdecWireWriteIntSetting` (8 bytes)

| Offset | Size | Field |
|--------|------|-------|
| 0      | 1    | `KdecIntSettingKey key` |
| 1      | 3    | implicit pad (align `value` to 4) |
| 4      | 4    | `int32_t value` |

### `KdecWireSettingEntry` (16 bytes)

Used only in the `GetAllSettings` response buffer. Bool settings are written first (in `KdecBoolSettingKey` order), then int settings (in `KdecIntSettingKey` order); the caller distinguishes types by key.

| Offset | Size | Field |
|--------|------|-------|
| 0      | 1    | `uint8_t key` |
| 1      | 3    | implicit pad |
| 4      | 4    | `union { bool as_bool; int32_t as_int; } value` |
| 8      | 8    | implicit trailing pad (16-byte alignment) |

---

## Inline Request Wire Structs

Small requests are passed inline (no buffer descriptor needed):

| Struct | Size | Used by |
|--------|------|---------|
| `KdecWireDeviceId { char device_id[64]; }` | 64 B | `RequestPair`, `AcceptPair`, `RejectPair`, `Unpair`, `Ping`, `Ring`, `GetCommandList`, `GetVolumeSinks` |
| `KdecWireRunCommand { char device_id[64]; char command_id[64]; }` | 128 B | `RunCommand` |
| `KdecWireWriteBoolSetting { KdecBoolSettingKey key; bool value; }` | 2 B | `WriteBoolSetting` |
| `KdecWireWriteIntSetting { KdecIntSettingKey key; uint8_t _pad[3]; int32_t value; }` | 8 B | `WriteIntSetting` |
| `KdecWireSendMediaAction { uint8_t action; uint8_t _pad[7]; int64_t value; }` | 16 B | `SendMediaAction` |
| `KdecWireSetVolumeSink { char device_id[64]; char sink_name[64]; int32_t volume; bool muted; bool is_default_output; }` | 136 B | `SetVolumeSink` |

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

**Request:** Out buffer (`SfBufferAttr_Out`): caller-allocated; server writes `KdecDeviceInfo` entries back-to-back

**Response:** `uint32_t count`: number of entries written

---

### `RequestPair` (3)

Initiates a pairing request to the specified device.

**Request:** Inline `KdecWireDeviceId`

**Response:** none

---

### `AcceptPair` (4)

Accepts an incoming pairing request from the specified device.

**Request:** Inline `KdecWireDeviceId`

**Response:** none

---

### `RejectPair` (5)

Rejects an incoming pairing request from the specified device.

**Request:** Inline `KdecWireDeviceId`

**Response:** none

---

### `Unpair` (6)

Removes the pairing with the specified device.

**Request:** Inline `KdecWireDeviceId`

**Response:** none

---

## Planned Commands

Opcodes are provisional.

### `Ping` (7)

Sends a ping notification to the specified device.

**Request:** Inline `KdecWireDeviceId`

**Response:** none

---

### `GetMediaInfo` (8)

Returns the media player state from whichever paired device most recently updated its player. Currently requires polling; event-based push is a future consideration.

**Request:** Out buffer (`SfBufferAttr_Out`): caller-allocated; server writes one `KdecMediaInfo`

**Response:** `uint32_t found`: `1` if a player was active and the buffer was written, `0` otherwise

---

### `SendMediaAction` (9)

Sends a media control action to the active device's player.

**Request:** Inline `KdecWireSendMediaAction`

**Response:** none

---

### `GetCommandList` (10)

Returns the list of runnable commands registered on the specified device.

**Request:**
- Inline `KdecWireDeviceId`
- Out buffer (`SfBufferAttr_Out`): caller-allocated; server writes `KdecCommandEntry` entries back-to-back

**Response:** `uint32_t count`: number of entries written

---

### `RunCommand` (11)

Runs a command on the specified device.

**Request:** Inline `KdecWireRunCommand`

**Response:** none

---

### `ReadBoolSetting` (12)

Reads a bool setting by key.

**Request:** Inline `KdecBoolSettingKey` (1 byte)

**Response:** Inline `bool`

---

### `WriteBoolSetting` (13)

Writes a bool setting by key.

**Request:** Inline `KdecWireWriteBoolSetting`

**Response:** none

---

### `ReadIntSetting` (14)

Reads an int setting by key.

**Request:** Inline `KdecIntSettingKey` (1 byte)

**Response:** Inline `int32_t`

---

### `WriteIntSetting` (15)

Writes an int setting by key.

**Request:** Inline `KdecWireWriteIntSetting`

**Response:** none

---

### `GetAllSettings` (16)

Returns all settings (both bool and int) and their current values.

**Request:** Out buffer (`SfBufferAttr_Out`): caller-allocated; server writes `KdecWireSettingEntry` entries back-to-back

**Response:** `uint32_t count`: number of entries written

---

### `Ring` (17)

Triggers the Find My Phone plugin on the specified device, causing it to ring.

**Request:** Inline `KdecWireDeviceId`

**Response:** none

---

### `GetVolumeSinks` (18)

Returns the list of audio output sinks for the specified device.

**Request:**
- Inline `KdecWireDeviceId`
- Out buffer (`SfBufferAttr_Out`): caller-allocated; server writes `KdecVolumeSinkInfo` entries back-to-back

**Response:** `uint32_t count`: number of entries written

---

### `SetVolumeSink` (19)

Sends an updated sink state (volume, muted, default) to the specified device.

**Request:** Inline `KdecWireSetVolumeSink`

**Response:** none

---

## Adding New Commands

1. Assign the next sequential opcode in the `KdecIpcCmd` enum in `common/src/kdec/ipc.h`.
2. Add the client-side wrapper in `common/src/kdec/ipc_client.cpp` and its declaration in `ipc_client.h`.
3. Add a case to the dispatch switch in `sysmodule/src/ipc/ipc_service.cpp`.
4. Document the new command in this file following the format above.
