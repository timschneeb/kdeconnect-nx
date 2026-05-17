#include "ipc_service.h"
#include <../../../common/src/kdec/ipc.h>
#include <../../../common/src/kdec/ipc_buffer.h>
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

            auto* recv_buffer = static_cast<uint8_t*>(hipcGetBufferAddress(hipc.data.recv_buffers));
            size_t recv_size  = hipcGetBufferSize(hipc.data.recv_buffers);

            auto devices_map = client->devices();
            IpcWriter wr;
            uint32_t count = 0;
            for (const auto& [id, sess] : devices_map) {
                DevicePairState pair_state;
                if (sess->paired) pair_state = DevicePairState::Paired;
                else if (sess->pair_state == PairState::RequestedByPeer) pair_state = DevicePairState::RequestedByPeer;
                else if (sess->pair_state == PairState::Requested) pair_state = DevicePairState::RequestedByMe;
                else pair_state = DevicePairState::None;

                bool is_connected = !sess->disconnected.load();

                // Check if device supports find-my-phone
                bool supports_fmp = false;
                for (const auto& cap : sess->info.incoming_capabilities) {
                    if (cap == PacketTypes::FindMyPhoneRequest) {
                        supports_fmp = true;
                        break;
                    }
                }

                int8_t battery_level = -1; // unavailable for now

                wr.write_string(id);
                wr.write_string(sess->info.name);
                wr.write(static_cast<uint8_t>(pair_state));
                wr.write(is_connected);
                wr.write(supports_fmp);
                wr.write(battery_level);
                count++;
            }

            if (wr.size() <= recv_size) {
                memcpy(recv_buffer, wr.data().data(), wr.size());
            }

            *out_size = sizeof(uint32_t);
            *reinterpret_cast<uint32_t*>(out_data) = count;
            return 0;
        }

        case KdecIpcCmd_RequestPair:
        case KdecIpcCmd_AcceptPair:
        case KdecIpcCmd_RejectPair:
        case KdecIpcCmd_Unpair: {
            const auto& hipc = r->hipc;
            if (hipc.meta.num_send_buffers < 1) return MAKERESULT(Module_Libnx, LibnxError_BadInput);
            const char* device_id = static_cast<const char*>(hipcGetBufferAddress(hipc.data.send_buffers));
            std::string id_str(device_id);

            if (cmd_id == KdecIpcCmd_RequestPair) { client->request_pair(id_str); return 0; }
            if (cmd_id == KdecIpcCmd_AcceptPair)  { client->accept_pair(id_str);  return 0; }
            if (cmd_id == KdecIpcCmd_RejectPair)  { client->reject_pair(id_str);  return 0; }
            if (cmd_id == KdecIpcCmd_Unpair)      { client->unpair(id_str);       return 0; }
            return 0;
        }

        case KdecIpcCmd_Ping: {
            const auto& hipc = r->hipc;
            if (hipc.meta.num_send_buffers < 1) return MAKERESULT(Module_Libnx, LibnxError_BadInput);
            const char* device_id = static_cast<const char*>(hipcGetBufferAddress(hipc.data.send_buffers));
            std::string id_str(device_id);

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

            auto* recv_buffer = static_cast<uint8_t*>(hipcGetBufferAddress(hipc.data.recv_buffers));
            size_t recv_size  = hipcGetBufferSize(hipc.data.recv_buffers);

            auto devices_map = client->devices();
            for (const auto& [id, sess] : devices_map) {
                auto* mpris = sess->plugin<MprisPlugin>();
                if (!mpris) continue;
                std::string player = mpris->current_player();
                if (player.empty()) continue;

                MprisPlugin::PlayerState state = mpris->player_state();

                IpcWriter wr;
                wr.write_string(id);
                wr.write_string(player);
                wr.write_string(state.title);
                wr.write_string(state.artist);
                wr.write_string(state.album);
                wr.write(state.is_playing);
                wr.write(state.can_play);
                wr.write(state.can_pause);
                wr.write(state.can_go_next);
                wr.write(state.can_go_previous);
                wr.write(state.can_seek);
                wr.write(static_cast<int32_t>(state.volume));
                wr.write(state.position);
                wr.write(state.length);

                uint32_t bytes_written = 0;
                if (wr.size() <= recv_size) {
                    memcpy(recv_buffer, wr.data().data(), wr.size());
                    bytes_written = static_cast<uint32_t>(wr.size());
                }

                *out_size = sizeof(uint32_t);
                *reinterpret_cast<uint32_t*>(out_data) = bytes_written;
                return 0;
            }

            // No active player found — return zero bytes
            *out_size = sizeof(uint32_t);
            *reinterpret_cast<uint32_t*>(out_data) = 0;
            return 0;
        }

        case KdecIpcCmd_SendMediaAction: {
            if (r->data.size < sizeof(KdecWireSendMediaAction))
                return MAKERESULT(Module_Libnx, LibnxError_BadInput);

            KdecWireSendMediaAction wire;
            memcpy(&wire, r->data.ptr, sizeof(wire));

            auto action = static_cast<KdecMediaAction>(wire.action);

            auto devices_map = client->devices();
            for (const auto& [id, sess] : devices_map) {
                auto* mpris = sess->plugin<MprisPlugin>();
                if (!mpris) continue;
                std::string player = mpris->current_player();
                if (player.empty()) continue;

                switch (action) {
                    case KdecMediaAction::Play:        mpris->send_action(player, "Play");     break;
                    case KdecMediaAction::Pause:       mpris->send_action(player, "Pause");    break;
                    case KdecMediaAction::PlayPause:   mpris->send_action(player, "PlayPause"); break;
                    case KdecMediaAction::Stop:        mpris->send_action(player, "Stop");     break;
                    case KdecMediaAction::Next:        mpris->send_action(player, "Next");     break;
                    case KdecMediaAction::Previous:    mpris->send_action(player, "Previous"); break;
                    case KdecMediaAction::SetVolume:   mpris->set_volume(player, static_cast<int>(wire.value)); break;
                    case KdecMediaAction::Seek:        mpris->seek(player, wire.value);        break;
                    case KdecMediaAction::SetPosition: mpris->set_position(player, wire.value); break;
                }
                return 0;
            }
            return 0;
        }

        case KdecIpcCmd_GetCommandList: {
            const auto& hipc = r->hipc;
            if (hipc.meta.num_send_buffers < 1 || hipc.meta.num_recv_buffers < 1)
                return MAKERESULT(Module_Libnx, LibnxError_BadInput);

            const char* device_id = static_cast<const char*>(hipcGetBufferAddress(hipc.data.send_buffers));
            std::string id_str(device_id);

            auto* recv_buffer = static_cast<uint8_t*>(hipcGetBufferAddress(hipc.data.recv_buffers));
            size_t recv_size  = hipcGetBufferSize(hipc.data.recv_buffers);

            auto sess = client->device(id_str);
            if (!sess) return MAKERESULT(Module_Libnx, LibnxError_NotFound);

            auto* plugin = sess->plugin<RunCommandPlugin>();
            if (!plugin) return MAKERESULT(Module_Libnx, LibnxError_NotFound);

            auto commands = plugin->remote_command_list();

            IpcWriter wr;
            for (const auto& [id, name] : commands) {
                wr.write_string(id);
                wr.write_string(name);
            }

            if (wr.size() <= recv_size) {
                memcpy(recv_buffer, wr.data().data(), wr.size());
            }

            *out_size = sizeof(uint32_t);
            *reinterpret_cast<uint32_t*>(out_data) = static_cast<uint32_t>(commands.size());
            return 0;
        }

        case KdecIpcCmd_RunCommand: {
            const auto& hipc = r->hipc;
            if (hipc.meta.num_send_buffers < 2) return MAKERESULT(Module_Libnx, LibnxError_BadInput);

            const char* device_id  = static_cast<const char*>(hipcGetBufferAddress(hipc.data.send_buffers));
            const char* command_id = static_cast<const char*>(hipcGetBufferAddress(hipc.data.send_buffers + 1));
            std::string id_str(device_id);
            std::string cmd_str(command_id);

            auto sess = client->device(id_str);
            if (!sess) return MAKERESULT(Module_Libnx, LibnxError_NotFound);

            auto* plugin = sess->plugin<RunCommandPlugin>();
            if (!plugin) return MAKERESULT(Module_Libnx, LibnxError_NotFound);

            plugin->run_remote_command(cmd_str);
            return 0;
        }

        case KdecIpcCmd_ReadSetting: {
            const auto& hipc = r->hipc;
            if (hipc.meta.num_send_buffers < 1 || hipc.meta.num_recv_buffers < 1)
                return MAKERESULT(Module_Libnx, LibnxError_BadInput);
            if (r->data.size < sizeof(KdecWireSettingType))
                return MAKERESULT(Module_Libnx, LibnxError_BadInput);

            KdecWireSettingType wire_type;
            memcpy(&wire_type, r->data.ptr, sizeof(wire_type));

            const char* key_str = static_cast<const char*>(hipcGetBufferAddress(hipc.data.send_buffers));
            std::string key(key_str);

            auto* recv_buffer = static_cast<uint8_t*>(hipcGetBufferAddress(hipc.data.recv_buffers));
            size_t recv_size  = hipcGetBufferSize(hipc.data.recv_buffers);

            std::lock_guard<std::mutex> lock(settings_mutex_);
            auto it = settings_.find(key);
            if (it == settings_.end()) return MAKERESULT(Module_Libnx, LibnxError_NotFound);

            const KdecSettingEntry& entry = it->second;
            IpcWriter wr;
            switch (entry.type) {
                case KdecSettingType::Bool:   wr.write(std::get<bool>(entry.value));        break;
                case KdecSettingType::Int:    wr.write(std::get<int32_t>(entry.value));     break;
                case KdecSettingType::String: wr.write_string(std::get<std::string>(entry.value)); break;
            }

            if (wr.size() <= recv_size) {
                memcpy(recv_buffer, wr.data().data(), wr.size());
            }

            *out_size = 0;
            return 0;
        }

        case KdecIpcCmd_WriteSetting: {
            const auto& hipc = r->hipc;
            if (hipc.meta.num_send_buffers < 2) return MAKERESULT(Module_Libnx, LibnxError_BadInput);
            if (r->data.size < sizeof(KdecWireSettingType))
                return MAKERESULT(Module_Libnx, LibnxError_BadInput);

            KdecWireSettingType wire_type;
            memcpy(&wire_type, r->data.ptr, sizeof(wire_type));

            const char* key_str = static_cast<const char*>(hipcGetBufferAddress(hipc.data.send_buffers));
            std::string key(key_str);

            const uint8_t* val_ptr  = static_cast<const uint8_t*>(hipcGetBufferAddress(hipc.data.send_buffers + 1));
            size_t          val_size = hipcGetBufferSize(hipc.data.send_buffers + 1);

            KdecSettingType type = static_cast<KdecSettingType>(wire_type.type);
            KdecSettingEntry entry;
            entry.key  = key;
            entry.type = type;

            IpcReader rd(val_ptr, val_size);
            switch (type) {
                case KdecSettingType::Bool:   entry.value = rd.read<bool>();    break;
                case KdecSettingType::Int:    entry.value = rd.read<int32_t>(); break;
                case KdecSettingType::String: entry.value = rd.read_string();   break;
            }
            if (!rd.ok()) return MAKERESULT(Module_Libnx, LibnxError_BadInput);

            {
                std::lock_guard<std::mutex> lock(settings_mutex_);
                settings_[key] = std::move(entry);
            }

            *out_size = 0;
            return 0;
        }

        case KdecIpcCmd_GetAllSettings: {
            const auto& hipc = r->hipc;
            if (hipc.meta.num_recv_buffers < 1) return MAKERESULT(Module_Libnx, LibnxError_BadInput);

            auto* recv_buffer = static_cast<uint8_t*>(hipcGetBufferAddress(hipc.data.recv_buffers));
            size_t recv_size  = hipcGetBufferSize(hipc.data.recv_buffers);

            std::lock_guard<std::mutex> lock(settings_mutex_);

            IpcWriter wr;
            for (const auto& [key, entry] : settings_) {
                wr.write_string(entry.key);
                wr.write(static_cast<uint8_t>(entry.type));
                switch (entry.type) {
                    case KdecSettingType::Bool:   wr.write(std::get<bool>(entry.value));        break;
                    case KdecSettingType::Int:    wr.write(std::get<int32_t>(entry.value));     break;
                    case KdecSettingType::String: wr.write_string(std::get<std::string>(entry.value)); break;
                }
            }

            uint32_t count = static_cast<uint32_t>(settings_.size());
            if (wr.size() <= recv_size) {
                memcpy(recv_buffer, wr.data().data(), wr.size());
            }

            *out_size = sizeof(uint32_t);
            *reinterpret_cast<uint32_t*>(out_data) = count;
            return 0;
        }

        default:
            return 1;
    }
}
