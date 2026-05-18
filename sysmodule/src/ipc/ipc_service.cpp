#include "ipc_service.h"
#include <../../../common/src/kdec/ipc.h>
#include <cstring>

#include "../net/kdeconnect_client.h"
#include "utils/logger.h"
#include "ipc_server.h"
#include "../plugins/ping_plugin.h"
#include "../plugins/mpris_plugin.h"
#include "../plugins/run_command_plugin.h"
#include "../net/network_packet.h"

#define MAX_SESSIONS 2

IpcService::IpcService(NxApplication* app) : app_(app), running_(false) {
}

IpcService::~IpcService() {
    stop();
}

void IpcService::start() {
    if (running_) return;
    running_ = true;

    Result rc = ipcServerInit(&srv_, KDEC_IPC_SERVICE_NAME, MAX_SESSIONS);
    if (R_FAILED(rc)) {
        Logger::error("Failed to register IPC service: 0x%x", rc);
        running_ = false;
        return;
    }

    rc = threadCreate(&thread_, thread_func, this, nullptr, 0x4000, 0x20, -2);
    if (R_FAILED(rc)) {
        Logger::error("Failed to create IPC thread: 0x%x", rc);
        ipcServerExit(&srv_);
        running_ = false;
        return;
    }

    rc = threadStart(&thread_);
    if (R_FAILED(rc)) {
        Logger::error("Failed to start IPC thread: 0x%x", rc);
        threadClose(&thread_);
        ipcServerExit(&srv_);
        running_ = false;
    }
}

void IpcService::stop() {
    if (!running_) return;
    running_ = false;

    // Signal the service exit - ipcServerProcess will wake up when service is unregistered
    ipcServerExit(&srv_);

    threadWaitForExit(&thread_);
    threadClose(&thread_);
}

void IpcService::thread_func(void* arg) {
    auto* self = static_cast<IpcService*>(arg);

    Logger::info("IPC service started");
    while (self->running_) {
        Result rc = ipcServerProcess(&self->srv_, handle_command_static, self);
        if (R_FAILED(rc) && rc != KERNELRESULT(TimedOut)) {
            Logger::error("ipcServerProcess failed: 0x%x", rc);
        }
    }
}

Result IpcService::handle_command_static(void* userdata, const IpcServerRequest* r, u8* out_data, size_t* out_size) {
    auto* self = static_cast<IpcService*>(userdata);
    return self->handle_command(r->data.cmdId, r, out_data, out_size);
}

Result IpcService::handle_command(u32 cmd_id, const IpcServerRequest* r, u8* out_data, size_t* out_size) {
    auto client = app_->client();
    if (!client) return MAKERESULT(Module_Libnx, LibnxError_NotInitialized);

    switch (cmd_id) {
        case KdecIpcCmd_GetApiVersion:
            *out_size = sizeof(uint32_t);
            *reinterpret_cast<uint32_t*>(out_data) = KDEC_IPC_API_VERSION;
            return 0;

        case KdecIpcCmd_GetDeviceCount: {
            *out_size = sizeof(uint32_t);
            *reinterpret_cast<uint32_t*>(out_data) = client->devices().size();
            return 0;
        }

        case KdecIpcCmd_GetDevices: {
            const auto& hipc = r->hipc;
            if (hipc.meta.num_recv_buffers < 1) return MAKERESULT(Module_Libnx, LibnxError_BadInput);

            auto* recv_buf  = static_cast<KdecDeviceInfo*>(hipcGetBufferAddress(hipc.data.recv_buffers));
            size_t recv_size = hipcGetBufferSize(hipc.data.recv_buffers);

            auto devices_map = client->devices();
            uint32_t count = 0;
            for (const auto& [id, sess] : devices_map) {
                if ((count + 1) * sizeof(KdecDeviceInfo) > recv_size) break;

                KdecDeviceInfo& info = recv_buf[count];
                memset(&info, 0, sizeof(info));
                strncpy(info.id,   id.c_str(),           KDEC_DEVICE_ID_MAX   - 1);
                strncpy(info.name, sess->info.name.c_str(), KDEC_DEVICE_NAME_MAX - 1);

                if (sess->paired)
                    info.pair_state = DevicePairState::Paired;
                else if (sess->pair_state == PairState::RequestedByPeer)
                    info.pair_state = DevicePairState::RequestedByPeer;
                else if (sess->pair_state == PairState::Requested)
                    info.pair_state = DevicePairState::RequestedByMe;
                else
                    info.pair_state = DevicePairState::None;

                info.is_connected = !sess->disconnected.load();

                for (const auto& cap : sess->info.incoming_capabilities) {
                    if (cap == PacketTypes::FindMyPhoneRequest) {
                        info.supports_find_my_phone = true;
                        break;
                    }
                }

                info.battery_level = -1; // TODO: implement
                count++;
            }

            *out_size = sizeof(uint32_t);
            *reinterpret_cast<uint32_t*>(out_data) = count;
            return 0;
        }

        case KdecIpcCmd_RequestPair:
        case KdecIpcCmd_AcceptPair:
        case KdecIpcCmd_RejectPair:
        case KdecIpcCmd_Unpair: {
            if (r->data.size < sizeof(KdecWireDeviceId))
                return MAKERESULT(Module_Libnx, LibnxError_BadInput);

            KdecWireDeviceId wire{};
            memcpy(&wire, r->data.ptr, sizeof(wire));
            std::string id_str(wire.device_id, strnlen(wire.device_id, KDEC_DEVICE_ID_MAX));

            if (cmd_id == KdecIpcCmd_RequestPair) { client->request_pair(id_str); return 0; }
            if (cmd_id == KdecIpcCmd_AcceptPair)  { client->accept_pair(id_str);  return 0; }
            if (cmd_id == KdecIpcCmd_RejectPair)  { client->reject_pair(id_str);  return 0; }
            if (cmd_id == KdecIpcCmd_Unpair)      { client->unpair(id_str);       return 0; }
            return 0;
        }

        case KdecIpcCmd_Ping: {
            if (r->data.size < sizeof(KdecWireDeviceId))
                return MAKERESULT(Module_Libnx, LibnxError_BadInput);

            KdecWireDeviceId wire{};
            memcpy(&wire, r->data.ptr, sizeof(wire));
            std::string id_str(wire.device_id, strnlen(wire.device_id, KDEC_DEVICE_ID_MAX));

            auto sess = client->device(id_str);
            if (!sess) return MAKERESULT(Module_Libnx, LibnxError_NotFound);

            auto* ping = sess->plugin<PingPlugin>();
            if (!ping) return MAKERESULT(Module_Libnx, LibnxError_NotFound);

            ping->ping("");
            return 0;
        }

        case KdecIpcCmd_GetMediaInfo: {
            const auto& hipc = r->hipc;
            if (hipc.meta.num_recv_buffers < 1) return MAKERESULT(Module_Libnx, LibnxError_BadInput);

            auto* recv_buf  = static_cast<KdecMediaInfo*>(hipcGetBufferAddress(hipc.data.recv_buffers));
            size_t recv_size = hipcGetBufferSize(hipc.data.recv_buffers);

            uint32_t found = 0;
            if (recv_size >= sizeof(KdecMediaInfo)) {
                auto devices_map = client->devices();
                for (const auto& [id, sess] : devices_map) {
                    auto* mpris = sess->plugin<MprisPlugin>();
                    if (!mpris) continue;
                    std::string player = mpris->current_player();
                    if (player.empty()) continue;

                    MprisPlugin::PlayerState state = mpris->player_state();

                    KdecMediaInfo& info = *recv_buf;
                    memset(&info, 0, sizeof(info));
                    strncpy(info.device_id, id.c_str(),           KDEC_DEVICE_ID_MAX - 1);
                    strncpy(info.player,    player.c_str(),        KDEC_PLAYER_MAX    - 1);
                    strncpy(info.title,     state.title.c_str(),   KDEC_TITLE_MAX     - 1);
                    strncpy(info.artist,    state.artist.c_str(),  KDEC_ARTIST_MAX    - 1);
                    strncpy(info.album,     state.album.c_str(),   KDEC_ALBUM_MAX     - 1);
                    info.position       = state.position;
                    info.length         = state.length;
                    info.volume         = static_cast<int32_t>(state.volume);
                    info.is_playing     = state.is_playing;
                    info.can_play       = state.can_play;
                    info.can_pause      = state.can_pause;
                    info.can_go_next    = state.can_go_next;
                    info.can_go_previous = state.can_go_previous;
                    info.can_seek       = state.can_seek;
                    found = 1;
                    break;
                }
            }

            *out_size = sizeof(uint32_t);
            *reinterpret_cast<uint32_t*>(out_data) = found;
            return 0;
        }

        case KdecIpcCmd_SendMediaAction: {
            if (r->data.size < sizeof(KdecWireSendMediaAction))
                return MAKERESULT(Module_Libnx, LibnxError_BadInput);

            KdecWireSendMediaAction wire{};
            memcpy(&wire, r->data.ptr, sizeof(wire));

            auto action = static_cast<KdecMediaAction>(wire.action);

            auto devices_map = client->devices();
            for (const auto& [id, sess] : devices_map) {
                auto* mpris = sess->plugin<MprisPlugin>();
                if (!mpris) continue;
                std::string player = mpris->current_player();
                if (player.empty()) continue;

                switch (action) {
                    case KdecMediaAction::Play:        mpris->send_action(player, "Play");      break;
                    case KdecMediaAction::Pause:       mpris->send_action(player, "Pause");     break;
                    case KdecMediaAction::PlayPause:   mpris->send_action(player, "PlayPause"); break;
                    case KdecMediaAction::Stop:        mpris->send_action(player, "Stop");      break;
                    case KdecMediaAction::Next:        mpris->send_action(player, "Next");      break;
                    case KdecMediaAction::Previous:    mpris->send_action(player, "Previous");  break;
                    case KdecMediaAction::SetVolume:   mpris->set_volume(player, static_cast<int>(wire.value));  break;
                    case KdecMediaAction::Seek:        mpris->seek(player, wire.value);         break;
                    case KdecMediaAction::SetPosition: mpris->set_position(player, wire.value); break;
                }
                return 0;
            }
            return 0;
        }

        case KdecIpcCmd_GetCommandList: {
            const auto& hipc = r->hipc;
            if (hipc.meta.num_recv_buffers < 1) return MAKERESULT(Module_Libnx, LibnxError_BadInput);
            if (r->data.size < sizeof(KdecWireDeviceId)) return MAKERESULT(Module_Libnx, LibnxError_BadInput);

            KdecWireDeviceId wire{};
            memcpy(&wire, r->data.ptr, sizeof(wire));
            std::string id_str(wire.device_id, strnlen(wire.device_id, KDEC_DEVICE_ID_MAX));

            auto* recv_buf  = static_cast<KdecCommandEntry*>(hipcGetBufferAddress(hipc.data.recv_buffers));
            size_t recv_size = hipcGetBufferSize(hipc.data.recv_buffers);

            auto sess = client->device(id_str);
            if (!sess) return MAKERESULT(Module_Libnx, LibnxError_NotFound);

            auto* plugin = sess->plugin<RunCommandPlugin>();
            if (!plugin) return MAKERESULT(Module_Libnx, LibnxError_NotFound);

            auto commands = plugin->remote_command_list();
            uint32_t count = 0;
            for (const auto& [id, name] : commands) {
                if ((count + 1) * sizeof(KdecCommandEntry) > recv_size) break;
                KdecCommandEntry& entry = recv_buf[count];
                memset(&entry, 0, sizeof(entry));
                strncpy(entry.id,   id.c_str(),   KDEC_COMMAND_ID_MAX   - 1);
                strncpy(entry.name, name.c_str(), KDEC_COMMAND_NAME_MAX - 1);
                count++;
            }

            *out_size = sizeof(uint32_t);
            *reinterpret_cast<uint32_t*>(out_data) = count;
            return 0;
        }

        case KdecIpcCmd_RunCommand: {
            if (r->data.size < sizeof(KdecWireRunCommand))
                return MAKERESULT(Module_Libnx, LibnxError_BadInput);

            KdecWireRunCommand wire{};
            memcpy(&wire, r->data.ptr, sizeof(wire));
            std::string id_str(wire.device_id,  strnlen(wire.device_id,  KDEC_DEVICE_ID_MAX));
            std::string cmd_str(wire.command_id, strnlen(wire.command_id, KDEC_COMMAND_ID_MAX));

            auto sess = client->device(id_str);
            if (!sess) return MAKERESULT(Module_Libnx, LibnxError_NotFound);

            auto* plugin = sess->plugin<RunCommandPlugin>();
            if (!plugin) return MAKERESULT(Module_Libnx, LibnxError_NotFound);

            plugin->run_remote_command(cmd_str);
            return 0;
        }

        case KdecIpcCmd_ReadBoolSetting: {
            if (r->data.size < sizeof(KdecBoolSettingKey))
                return MAKERESULT(Module_Libnx, LibnxError_BadInput);

            uint8_t key;
            memcpy(&key, r->data.ptr, sizeof(key));

            std::lock_guard<std::mutex> lock(settings_mutex_);
            auto it = bool_settings_.find(key);
            if (it == bool_settings_.end()) return MAKERESULT(Module_Libnx, LibnxError_NotFound);

            *out_size = sizeof(bool);
            *reinterpret_cast<bool*>(out_data) = it->second;
            return 0;
        }

        case KdecIpcCmd_WriteBoolSetting: {
            if (r->data.size < sizeof(KdecWireWriteBoolSetting))
                return MAKERESULT(Module_Libnx, LibnxError_BadInput);

            KdecWireWriteBoolSetting wire{};
            memcpy(&wire, r->data.ptr, sizeof(wire));

            std::lock_guard<std::mutex> lock(settings_mutex_);
            bool_settings_[static_cast<uint8_t>(wire.key)] = wire.value;
            *out_size = 0;
            return 0;
        }

        case KdecIpcCmd_ReadIntSetting: {
            if (r->data.size < sizeof(KdecIntSettingKey))
                return MAKERESULT(Module_Libnx, LibnxError_BadInput);

            uint8_t key;
            memcpy(&key, r->data.ptr, sizeof(key));

            std::lock_guard<std::mutex> lock(settings_mutex_);
            auto it = int_settings_.find(key);
            if (it == int_settings_.end()) return MAKERESULT(Module_Libnx, LibnxError_NotFound);

            *out_size = sizeof(int32_t);
            *reinterpret_cast<int32_t*>(out_data) = it->second;
            return 0;
        }

        case KdecIpcCmd_WriteIntSetting: {
            if (r->data.size < sizeof(KdecWireWriteIntSetting))
                return MAKERESULT(Module_Libnx, LibnxError_BadInput);

            KdecWireWriteIntSetting wire{};
            memcpy(&wire, r->data.ptr, sizeof(wire));

            std::lock_guard<std::mutex> lock(settings_mutex_);
            int_settings_[static_cast<uint8_t>(wire.key)] = wire.value;
            *out_size = 0;
            return 0;
        }

        case KdecIpcCmd_GetAllSettings: {
            const auto& hipc = r->hipc;
            if (hipc.meta.num_recv_buffers < 1) return MAKERESULT(Module_Libnx, LibnxError_BadInput);

            auto* recv_buf  = static_cast<KdecWireSettingEntry*>(hipcGetBufferAddress(hipc.data.recv_buffers));
            size_t recv_size = hipcGetBufferSize(hipc.data.recv_buffers);

            std::lock_guard<std::mutex> lock(settings_mutex_);
            uint32_t count = 0;
            for (const auto& [key, val] : bool_settings_) {
                if ((count + 1) * sizeof(KdecWireSettingEntry) > recv_size) break;
                KdecWireSettingEntry& e = recv_buf[count++];
                e = {};
                e.key = key;
                e.value.as_bool = val;
            }
            for (const auto& [key, val] : int_settings_) {
                if ((count + 1) * sizeof(KdecWireSettingEntry) > recv_size) break;
                KdecWireSettingEntry& e = recv_buf[count++];
                e = {};
                e.key = key;
                e.value.as_int = val;
            }

            *out_size = sizeof(uint32_t);
            *reinterpret_cast<uint32_t*>(out_data) = count;
            return 0;
        }

        default:
            return 1;
    }
}