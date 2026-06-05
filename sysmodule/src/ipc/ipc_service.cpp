#include "ipc_service.h"
#include <kdec/ipc.h>
#include <cstring>
#include <malloc.h>

#include "utils/settings_store.h"

#include "config.h"
#include "../net/kdeconnect_client.h"
#include "utils/logger.h"
#include "ipc_server.h"
#include "../plugins/find_my_phone_plugin.h"
#include "../plugins/ping_plugin.h"
#include "../plugins/mpris_plugin.h"
#include "../plugins/run_command_plugin.h"
#include "../plugins/system_volume_plugin.h"
#include "../net/network_packet.h"
#include "plugins/battery_plugin.h"
#include "../plugins/share_plugin.h"
#include "../plugins/notification_plugin.h"

#define MAX_SESSIONS 2

extern "C" {
extern void* fake_heap_start;
extern void* fake_heap_end;

size_t internal_heap_size() {
    return static_cast<u8*>(fake_heap_end) - static_cast<u8*>(fake_heap_start);
}
}

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
        return;
    }

    SettingsStore::load();
}

void IpcService::stop() {
    if (!running_) return;
    running_ = false;

    ipcServerExit(&srv_);
    // ipcServerExit closes the handles, but that alone may not wake up a thread
    // blocked in svcWaitSynchronization. svcCancelSynchronization forces it out.
    if (thread_.handle)
        svcCancelSynchronization(thread_.handle);

    threadWaitForExit(&thread_);
    threadClose(&thread_);
}

void IpcService::thread_func(void* arg) {
    auto* self = static_cast<IpcService*>(arg);

    Logger::info("IPC service started");
    while (self->running_) {
        Result rc = ipcServerProcess(&self->srv_, handle_command_static, self);
        if (R_FAILED(rc) && rc != KERNELRESULT(TimedOut) && self->running_) {
            Logger::error("ipcServerProcess failed: 0x%x", rc);
        }
    }
}

Result IpcService::handle_command_static(void* userdata, const IpcServerRequest* r, u8* out_data, size_t* out_size) {
    auto* self = static_cast<IpcService*>(userdata);
    return self->handle_command(r->data.cmdId, r, out_data, out_size);
}

Result IpcService::handle_command(u32 cmd_id, const IpcServerRequest* r, u8* out_data, size_t* out_size) const {
    auto client = app_->client();
    if (!client) return MAKERESULT(Module_Libnx, LibnxError_NotInitialized);

    switch (cmd_id) {
        case KdecIpcCmd_GetApiVersion:
            *out_size = sizeof(uint32_t);
            *reinterpret_cast<uint32_t*>(out_data) = KDEC_IPC_API_VERSION;
            return 0;

        case KdecIpcCmd_GetDeviceCount: {
            *out_size = sizeof(uint32_t);
            *reinterpret_cast<uint32_t*>(out_data) =
                client->devices().size() + client->offline_paired_devices().size();
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

                if (sess->info.type == "phone") {
                    info.type = DeviceType::Phone;
                }
                else if (sess->info.type == "laptop") {
                    info.type = DeviceType::Laptop;
                }
                else if (sess->info.type == "tablet") {
                    info.type = DeviceType::Tablet;
                }
                else if (sess->info.type == "tv") {
                    info.type = DeviceType::TV;
                }
                else {
                    info.type = DeviceType::Desktop;
                }

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
                    }
                    else if (cap == PacketTypes::RunCommandRequest) {
                        info.supports_commands = true;
                    }
                    else if (cap == PacketTypes::SystemVolumeRequest) {
                        info.supports_volume_sinks = true;
                    }
                    else if (cap == PacketTypes::MprisRequest) {
                        info.supports_mpris_remote = true;
                    }
                    else if (cap == PacketTypes::ShareRequest) {
                        info.supports_share = true;
                    }
                }

                if (auto* bat = sess->plugin<BatteryPlugin>())
                    bat->read_remote_state(info.battery_level, info.is_charging);
                else
                    info.battery_level = -1;
                count++;
            }

            // Append paired devices that have no active session (offline/disconnected).
            for (const auto& pdev : client->offline_paired_devices()) {
                if ((count + 1) * sizeof(KdecDeviceInfo) > recv_size) break;
                KdecDeviceInfo& info = recv_buf[count];
                memset(&info, 0, sizeof(info));
                strncpy(info.id,   pdev.info.id.c_str(),   KDEC_DEVICE_ID_MAX   - 1);
                strncpy(info.name, pdev.info.name.c_str(), KDEC_DEVICE_NAME_MAX - 1);
                info.pair_state   = DevicePairState::Paired;
                info.is_connected = false;
                info.battery_level = -1;
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

        case KdecIpcCmd_GetVolumeSinks: {
            const auto& hipc = r->hipc;
            if (hipc.meta.num_recv_buffers < 1) return MAKERESULT(Module_Libnx, LibnxError_BadInput);
            if (r->data.size < sizeof(KdecWireDeviceId)) return MAKERESULT(Module_Libnx, LibnxError_BadInput);

            KdecWireDeviceId wire{};
            memcpy(&wire, r->data.ptr, sizeof(wire));
            std::string id_str(wire.device_id, strnlen(wire.device_id, KDEC_DEVICE_ID_MAX));

            auto* recv_buf  = static_cast<KdecVolumeSinkInfo*>(hipcGetBufferAddress(hipc.data.recv_buffers));
            size_t recv_size = hipcGetBufferSize(hipc.data.recv_buffers);

            auto sess = client->device(id_str);
            if (!sess) return MAKERESULT(Module_Libnx, LibnxError_NotFound);

            auto* plugin = sess->plugin<SystemVolumePlugin>();
            if (!plugin) return MAKERESULT(Module_Libnx, LibnxError_NotFound);

            auto sinks = plugin->get_remote_sink_list();
            uint32_t count = 0;
            for (const auto& s : sinks) {
                if ((count + 1) * sizeof(KdecVolumeSinkInfo) > recv_size) break;
                KdecVolumeSinkInfo& entry = recv_buf[count];
                memset(&entry, 0, sizeof(entry));
                strncpy(entry.name,        s.name.c_str(),        KDEC_SINK_NAME_MAX - 1);
                strncpy(entry.description, s.description.c_str(), KDEC_SINK_DESC_MAX - 1);
                entry.volume            = s.volume;
                entry.is_muted          = s.is_muted;
                entry.is_default_output = s.is_default_output;
                count++;
            }

            *out_size = sizeof(uint32_t);
            *reinterpret_cast<uint32_t*>(out_data) = count;
            return 0;
        }

        case KdecIpcCmd_SetVolumeSink: {
            if (r->data.size < sizeof(KdecWireSetVolumeSink))
                return MAKERESULT(Module_Libnx, LibnxError_BadInput);

            KdecWireSetVolumeSink wire{};
            memcpy(&wire, r->data.ptr, sizeof(wire));
            std::string id_str(wire.device_id, strnlen(wire.device_id, KDEC_DEVICE_ID_MAX));
            std::string sink_str(wire.sink_name, strnlen(wire.sink_name, KDEC_SINK_NAME_MAX));

            auto sess = client->device(id_str);
            if (!sess) return MAKERESULT(Module_Libnx, LibnxError_NotFound);

            auto* plugin = sess->plugin<SystemVolumePlugin>();
            if (!plugin) return MAKERESULT(Module_Libnx, LibnxError_NotFound);

            plugin->set_remote_sink(sink_str, wire.volume, wire.muted, wire.is_default_output);
            return 0;
        }

        case KdecIpcCmd_Ring: {
            if (r->data.size < sizeof(KdecWireDeviceId))
                return MAKERESULT(Module_Libnx, LibnxError_BadInput);

            KdecWireDeviceId wire{};
            memcpy(&wire, r->data.ptr, sizeof(wire));
            std::string id_str(wire.device_id, strnlen(wire.device_id, KDEC_DEVICE_ID_MAX));

            auto sess = client->device(id_str);
            if (!sess) return MAKERESULT(Module_Libnx, LibnxError_NotFound);

            auto* fmp = sess->plugin<FindMyPhonePlugin>();
            if (!fmp) return MAKERESULT(Module_Libnx, LibnxError_NotFound);

            fmp->find();
            return 0;
        }

        case KdecIpcCmd_GetMediaInfo: {
            const auto& hipc = r->hipc;
            if (hipc.meta.num_recv_buffers < 1) return MAKERESULT(Module_Libnx, LibnxError_BadInput);
            if (r->data.size < sizeof(KdecWireDeviceId)) return MAKERESULT(Module_Libnx, LibnxError_BadInput);

            KdecWireDeviceId id_wire{};
            memcpy(&id_wire, r->data.ptr, sizeof(id_wire));
            std::string id_str(id_wire.device_id, strnlen(id_wire.device_id, KDEC_DEVICE_ID_MAX));

            auto* recv_buf  = static_cast<KdecMediaInfo*>(hipcGetBufferAddress(hipc.data.recv_buffers));
            size_t recv_size = hipcGetBufferSize(hipc.data.recv_buffers);

            uint32_t found = 0;
            if (recv_size >= sizeof(KdecMediaInfo)) {
                if (auto sess = client->device(id_str)) {
                    if (auto* mpris = sess->plugin<MprisPlugin>()) {
                        std::string player = mpris->current_player();
                        if (!player.empty()) {
                            MprisPlugin::PlayerState state = mpris->player_state();

                            KdecMediaInfo& info = *recv_buf;
                            memset(&info, 0, sizeof(info));
                            strncpy(info.device_id, id_str.c_str(),       KDEC_DEVICE_ID_MAX - 1);
                            strncpy(info.player,    player.c_str(),        KDEC_PLAYER_MAX    - 1);
                            strncpy(info.title,     state.title.c_str(),   KDEC_TITLE_MAX     - 1);
                            strncpy(info.artist,    state.artist.c_str(),  KDEC_ARTIST_MAX    - 1);
                            strncpy(info.album,     state.album.c_str(),   KDEC_ALBUM_MAX     - 1);
                            info.position        = state.position;
                            info.length          = state.length;
                            info.volume          = state.volume;
                            info.is_playing      = state.is_playing;
                            info.can_play        = state.can_play;
                            info.can_pause       = state.can_pause;
                            info.can_go_next     = state.can_go_next;
                            info.can_go_previous = state.can_go_previous;
                            // Mirror Android's isSeekAllowed: only expose seekbar when length is known.
                            // length == 0 means never received; length < 0 means explicit "unknown" (live).
                            // Negative pos/length sentinels are filtered in on_packet_received so no
                            // position guard is needed here: a partial seek update won't flip this false.
                            info.can_seek        = state.can_seek && state.length > 0;
                            strncpy(info.album_art_hash, state.album_art_hash.c_str(), sizeof(info.album_art_hash) - 1);
                            found = 1;
                        }
                    }
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
            std::string id_str(wire.device_id, strnlen(wire.device_id, KDEC_DEVICE_ID_MAX));

            auto sess = client->device(id_str);
            if (!sess) return MAKERESULT(Module_Libnx, LibnxError_NotFound);

            auto* mpris = sess->plugin<MprisPlugin>();
            if (!mpris) return MAKERESULT(Module_Libnx, LibnxError_NotFound);

            std::string player = mpris->current_player();
            if (player.empty()) return 0;

            switch (static_cast<KdecMediaAction>(wire.action)) {
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
            if (key >= static_cast<uint8_t>(KdecBoolSettingKey::KDEC_BOOL_SETTING_COUNT))
                return MAKERESULT(Module_Libnx, LibnxError_NotFound);

            *out_size = sizeof(bool);
            *reinterpret_cast<bool*>(out_data) = SettingsStore::get(static_cast<KdecBoolSettingKey>(key));
            return 0;
        }

        case KdecIpcCmd_WriteBoolSetting: {
            if (r->data.size < sizeof(KdecWireWriteBoolSetting))
                return MAKERESULT(Module_Libnx, LibnxError_BadInput);

            KdecWireWriteBoolSetting wire{};
            memcpy(&wire, r->data.ptr, sizeof(wire));
            SettingsStore::set(wire.key, wire.value);

            if (wire.key == KdecBoolSettingKey::RunCommandPowerCommandsEnabled) {
                // If power commands were changed, resend the command list.
                for (auto [id, session] : client->devices()) {
                    RunCommandPlugin* plg;
                    if (plg = session->plugin<RunCommandPlugin>(); plg) {
                        plg->send_local_command_list();
                    }
                }
            }

            *out_size = 0;
            return 0;
        }

        case KdecIpcCmd_ReadIntSetting: {
            if (r->data.size < sizeof(KdecIntSettingKey))
                return MAKERESULT(Module_Libnx, LibnxError_BadInput);

            uint8_t key;
            memcpy(&key, r->data.ptr, sizeof(key));
            if (key >= static_cast<uint8_t>(KdecIntSettingKey::KDEC_INT_SETTING_COUNT))
                return MAKERESULT(Module_Libnx, LibnxError_NotFound);

            *out_size = sizeof(int32_t);
            *reinterpret_cast<int32_t*>(out_data) = SettingsStore::get(static_cast<KdecIntSettingKey>(key));
            return 0;
        }

        case KdecIpcCmd_WriteIntSetting: {
            if (r->data.size < sizeof(KdecWireWriteIntSetting))
                return MAKERESULT(Module_Libnx, LibnxError_BadInput);

            KdecWireWriteIntSetting wire{};
            memcpy(&wire, r->data.ptr, sizeof(wire));
            SettingsStore::set(wire.key, wire.value);
            *out_size = 0;
            return 0;
        }

        case KdecIpcCmd_GetAllSettings: {
            const auto& hipc = r->hipc;
            if (hipc.meta.num_recv_buffers < 1) return MAKERESULT(Module_Libnx, LibnxError_BadInput);

            auto* recv_buf = static_cast<KdecWireSettingEntry*>(hipcGetBufferAddress(hipc.data.recv_buffers));
            const size_t recv_size = hipcGetBufferSize(hipc.data.recv_buffers);

            const auto entries = SettingsStore::get_all();
            uint32_t count = 0;
            for (const auto& e : entries) {
                if ((count + 1) * sizeof(KdecWireSettingEntry) > recv_size) break;
                recv_buf[count++] = e;
            }

            *out_size = sizeof(uint32_t);
            *reinterpret_cast<uint32_t*>(out_data) = count;
            return 0;
        }

        case KdecIpcCmd_SendScreenshot: {
            if (r->data.size < sizeof(KdecWireDeviceId))
                return MAKERESULT(Module_Libnx, LibnxError_BadInput);

            KdecWireDeviceId wire{};
            memcpy(&wire, r->data.ptr, sizeof(wire));
            std::string id_str(wire.device_id, strnlen(wire.device_id, KDEC_DEVICE_ID_MAX));

            auto sess = client->device(id_str);
            if (!sess) return MAKERESULT(Module_Libnx, LibnxError_NotFound);

            auto* share = sess->plugin<SharePlugin>();
            if (!share) return MAKERESULT(Module_Libnx, LibnxError_NotFound);

            return share->send_screenshot() ? 0 : MAKERESULT(Module_Libnx, LibnxError_IoError);
        }

        case KdecIpcCmd_GetMemoryInfo: {
            KdecMemoryInfo s = {};

            u64 used = 0;
            svcGetInfo(&used, InfoType_UsedMemorySize, CUR_PROCESS_HANDLE, 0);
            s.proc_used_kb = used / 1024ULL;

            s.socket_tmem_kb = bsdGetTransferMemSizeForConfig(&socketInitConfig) / 1024;

            struct mallinfo mi = mallinfo();
            s.heap_used_kb  = mi.uordblks / 1024;
            s.heap_total_kb = (mi.uordblks + mi.fordblks) / 1024;
            s.heap_max_kb = internal_heap_size() / 1024;

            *out_size = sizeof(KdecMemoryInfo);
            *reinterpret_cast<KdecMemoryInfo*>(out_data) = s;
            return 0;
        }

        case KdecIpcCmd_SendTestNotification:
            static std::atomic_uint32_t test_notification_id = 0;
            NotificationPlugin::post_app_notification(
                "test" + std::to_string(test_notification_id++), "KDE Connect", "Test notification",
                "This is a long test notification. Lorem ipsum dolor sit amet, consectetur adipiscing elit, "
                "sed do eiusmod tempor incididunt ut labore et dolore magna aliqua. Ut enim ad minim veniam, quis "
                "nostrud exercitation ullamco laboris nisi ut aliquip ex ea commodo consequat.", "");
            return 0;

        case KdecIpcCmd_SendBroadcast:
            client->send_broadcast();
            return 0;

        default:
            return 1;
    }
}
