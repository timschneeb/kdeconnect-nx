#include "ipc_service.h"
#include <../../../common/src/kdec/ipc.h>
#include <cstring>

#include "../net/kdeconnect_client.h"
#include "utils/logger.h"
#include "ipc_server.h"

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

            auto* out_devices = static_cast<KdecDeviceInfo*>(hipcGetBufferAddress(hipc.data.recv_buffers));
            size_t max_size = hipcGetBufferSize(hipc.data.recv_buffers);
            size_t max_count = max_size / sizeof(KdecDeviceInfo);

            auto devices_map = client->devices();
            uint32_t count = 0;
            for (const auto& [id, sess] : devices_map) {
                if (count >= max_count) break;
                
                KdecDeviceInfo& info = out_devices[count];
                memset(&info, 0, sizeof(KdecDeviceInfo));
                strncpy(info.id, id.c_str(), KDEC_MAX_DEVICE_ID - 1);
                strncpy(info.name, sess->info.name.c_str(), KDEC_MAX_DEVICE_NAME - 1);
                
                if (sess->paired) info.pair_state = DevicePairState::Paired;
                else if (sess->pair_state == PairState::RequestedByPeer) info.pair_state = DevicePairState::RequestedByPeer;
                else if (sess->pair_state == PairState::Requested) info.pair_state = DevicePairState::RequestedByMe;
                else info.pair_state = DevicePairState::None;

                info.is_connected = !sess->disconnected.load();
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
            const auto& hipc = r->hipc;
            if (hipc.meta.num_send_buffers < 1) return MAKERESULT(Module_Libnx, LibnxError_BadInput);
            const char* device_id = static_cast<const char*>(hipcGetBufferAddress(hipc.data.send_buffers));
            std::string id_str(device_id);
            
            if (cmd_id == KdecIpcCmd_RequestPair) { client->request_pair(id_str); return 0; }
            if (cmd_id == KdecIpcCmd_AcceptPair) { client->accept_pair(id_str); return 0; }
            if (cmd_id == KdecIpcCmd_RejectPair) { client->reject_pair(id_str); return 0; }
            if (cmd_id == KdecIpcCmd_Unpair) { client->unpair(id_str); return 0; }
            return 0;
        }

        default:
            return 1;
    }
}
