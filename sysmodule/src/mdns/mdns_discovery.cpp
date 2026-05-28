#include "mdns_discovery.h"

// Must be defined before mdns.h when ifaddrs is unavailable
#if !__has_include(<ifaddrs.h>)
#  define MDNS_NO_IPV6 1
#  define NEED_IOCTL_IFCONF 1
#endif

#include "mdns.h"
#include "utils/logger.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#if !defined(NEED_IOCTL_IFCONF)
#  include <ifaddrs.h>
#  include <net/if.h>
#endif
#include <array>
#include <atomic>
#include <chrono>
#include <memory>
#include <string_view>
#include "utils/stack_thread.h"
#include <unordered_map>
#include <utility>
#include <vector>

namespace {
constexpr const char* kServiceType = "_kdeconnect._udp.local.";
constexpr auto kIdleSleep = std::chrono::milliseconds(100);
constexpr auto kQueryRepeat = std::chrono::seconds(15);
std::string hostname_or_default() {
    char buffer[256] = {};
    if (gethostname(buffer, sizeof(buffer) - 1) == 0 && buffer[0] != '\0') {
        return std::string(buffer) + ".local.";
    }
    return "MiniKDEConnect.local.";
}
std::vector<in_addr> collect_ipv4_addresses() {
#if !defined(NEED_IOCTL_IFCONF)
    std::vector<in_addr> addresses;
    ifaddrs* ifaddr = nullptr;
    if (getifaddrs(&ifaddr) != 0) {
        return addresses;
    }
    for (ifaddrs* iface = ifaddr; iface != nullptr; iface = iface->ifa_next) {
        if (!iface->ifa_addr || iface->ifa_addr->sa_family != AF_INET) {
            continue;
        }
        const unsigned int flags = static_cast<unsigned int>(iface->ifa_flags);
        if (!(flags & IFF_UP) || !(flags & IFF_MULTICAST) || (flags & IFF_LOOPBACK)) {
            continue;
        }
        const auto* addr = reinterpret_cast<sockaddr_in*>(iface->ifa_addr);
        addresses.push_back(addr->sin_addr);
    }
    freeifaddrs(ifaddr);
    return addresses;
#else
    return std::vector{ in_addr{static_cast<in_addr_t>(gethostid())} };
#endif
}
std::string ipv4_to_string(const in_addr& addr) {
    char buffer[INET_ADDRSTRLEN] = {};
    if (inet_ntop(AF_INET, &addr, buffer, sizeof(buffer)) == nullptr) {
        return {};
    }
    return buffer;
}
std::string sockaddr_to_ipv4_string(const sockaddr* from) {
    if (!from || from->sa_family != AF_INET) {
        return {};
    }
    const auto* addr = reinterpret_cast<const sockaddr_in*>(from);
    return ipv4_to_string(addr->sin_addr);
}
mdns_string_t make_mdns_string(const std::string& value) {
    return mdns_string_t{value.c_str(), value.size()};
}
struct AnnouncedInfo {
    std::string service_type;
    std::string service_instance;
    std::string hostname;
    std::vector<in_addr> addresses_v4;
    uint16_t port = 0;
    std::unordered_map<std::string, std::string> txt_records;
};
struct QueryState {
    std::string instance_id;
    std::string host;
    std::string device_id;
    bool saw_ptr = false;
};
mdns_record_t make_record(const AnnouncedInfo& self, mdns_record_type_t type, const in_addr* addr = nullptr,
                          const std::pair<const std::string, std::string>* txt = nullptr) {
    mdns_record_t record{};
    record.type = type;
    record.rclass = 0;
    record.ttl = 0;
    switch (type) {
    case MDNS_RECORDTYPE_PTR:
        record.name = make_mdns_string(self.service_type);
        record.data.ptr.name = make_mdns_string(self.service_instance);
        break;
    case MDNS_RECORDTYPE_SRV:
        record.name = make_mdns_string(self.service_instance);
        record.data.srv.name = make_mdns_string(self.hostname);
        record.data.srv.port = self.port;
        record.data.srv.priority = 0;
        record.data.srv.weight = 0;
        break;
    case MDNS_RECORDTYPE_A:
        record.name = make_mdns_string(self.hostname);
        if (addr) {
            record.data.a.addr.sin_family = AF_INET;
            record.data.a.addr.sin_addr = *addr;
        }
        break;
    case MDNS_RECORDTYPE_TXT:
        record.name = make_mdns_string(self.service_instance);
        record.data.txt.key = make_mdns_string(txt->first);
        record.data.txt.value = make_mdns_string(txt->second);
        break;
    }
    return record;
}
int service_callback(int sock,
                     const sockaddr* from,
                     size_t addrlen,
                     mdns_entry_type_t entry_type,
                     uint16_t query_id,
                     uint16_t record_type,
                     uint16_t rclass,
                     uint32_t ttl,
                     const void* data,
                     size_t size,
                     size_t name_offset,
                     size_t name_length,
                     size_t record_offset,
                     size_t record_length,
                     void* user_data) {
    (void)ttl;
    (void)name_length;
    (void)record_offset;
    (void)record_length;
    auto* self = static_cast<AnnouncedInfo*>(user_data);
    if (entry_type != MDNS_ENTRYTYPE_QUESTION) {
        return 0;
    }
    char name_buffer[256] = {};
    mdns_string_t name = mdns_string_extract(data, size, &name_offset, name_buffer, sizeof(name_buffer));
    std::string_view queried_name(name.str, name.length);
    auto write_records = [&](const mdns_record_t& answer, const mdns_record_t* additional, size_t additional_count) {
        static std::array<uint32_t, 512> send_buffer{};
        uint16_t unicast = (rclass & MDNS_UNICAST_RESPONSE);
        if (unicast) {
            return mdns_query_answer_unicast(sock, from, addrlen, send_buffer.data(), send_buffer.size() * sizeof(uint32_t), query_id,
                                             static_cast<mdns_record_type_t>(record_type), name.str, name.length,
                                             answer, nullptr, 0, additional, additional_count);
        }
        return mdns_query_answer_multicast(sock, send_buffer.data(), send_buffer.size() * sizeof(uint32_t), answer, nullptr, 0,
                                          additional, additional_count);
    };
    if (queried_name == self->service_type &&
        (record_type == MDNS_RECORDTYPE_PTR || record_type == MDNS_RECORDTYPE_ANY)) {
        mdns_record_t answer = make_record(*self, MDNS_RECORDTYPE_PTR);
        mdns_record_t additional[16];
        size_t additional_count = 0;
        additional[additional_count++] = make_record(*self, MDNS_RECORDTYPE_SRV);
        for (const auto& address : self->addresses_v4)
            additional[additional_count++] = make_record(*self, MDNS_RECORDTYPE_A, &address);
        for (const auto& txt : self->txt_records)
            additional[additional_count++] = make_record(*self, MDNS_RECORDTYPE_TXT, nullptr, &txt);
        return write_records(answer, additional, additional_count);
    }
    if (queried_name == self->service_instance &&
        (record_type == MDNS_RECORDTYPE_SRV || record_type == MDNS_RECORDTYPE_ANY)) {
        mdns_record_t answer = make_record(*self, MDNS_RECORDTYPE_SRV);
        mdns_record_t additional[16];
        size_t additional_count = 0;
        for (const auto& address : self->addresses_v4)
            additional[additional_count++] = make_record(*self, MDNS_RECORDTYPE_A, &address);
        for (const auto& txt : self->txt_records)
            additional[additional_count++] = make_record(*self, MDNS_RECORDTYPE_TXT, nullptr, &txt);
        return write_records(answer, additional, additional_count);
    }
    if (queried_name == self->hostname &&
        (record_type == MDNS_RECORDTYPE_A || record_type == MDNS_RECORDTYPE_ANY) && !self->addresses_v4.empty()) {
        mdns_record_t answer = make_record(*self, MDNS_RECORDTYPE_A, &self->addresses_v4.front());
        mdns_record_t additional[16];
        size_t additional_count = 0;
        for (const auto& txt : self->txt_records)
            additional[additional_count++] = make_record(*self, MDNS_RECORDTYPE_TXT, nullptr, &txt);
        return write_records(answer, additional, additional_count);
    }
    return 0;
}
int discovery_callback(int sock,
                       const sockaddr* from,
                       size_t addrlen,
                       mdns_entry_type_t entry_type,
                       uint16_t query_id,
                       uint16_t record_type,
                       uint16_t rclass,
                       uint32_t ttl,
                       const void* data,
                       size_t size,
                       size_t name_offset,
                       size_t name_length,
                       size_t record_offset,
                       size_t record_length,
                       void* user_data) {
    (void)sock;
    (void)addrlen;
    (void)query_id;
    (void)entry_type;
    (void)rclass;
    (void)ttl;
    (void)name_length;
    (void)record_offset;
    (void)record_length;
    auto* state = static_cast<QueryState*>(user_data);
    if (record_type == MDNS_RECORDTYPE_PTR) {
        char name_buffer[256] = {};
        mdns_string_t name = mdns_string_extract(data, size, &name_offset, name_buffer, sizeof(name_buffer));
        std::string instance(name.str, name.length);
        auto dot = instance.find('.');
        if (dot != std::string::npos)
            instance.resize(dot);
        state->instance_id = std::move(instance);
        state->host = sockaddr_to_ipv4_string(from);
        state->saw_ptr = true;
        return 0;
    }
    if (record_type == MDNS_RECORDTYPE_A) {
        sockaddr_in addr{};
        if (mdns_record_parse_a(data, size, record_offset, record_length, &addr))
            state->host = ipv4_to_string(addr.sin_addr);
    }
    if (record_type == MDNS_RECORDTYPE_TXT) {
        mdns_record_txt_t records[16];
        const size_t parsed = mdns_record_parse_txt(data, size, record_offset, record_length, records,
                                                    sizeof(records) / sizeof(records[0]));
        for (size_t i = 0; i < parsed; ++i) {
            if (std::string_view(records[i].key.str, records[i].key.length) == "id") {
                state->device_id.assign(records[i].value.str, records[i].value.length);
                break;
            }
        }
    }
    return 0;
}
} // namespace
struct MdnsDiscovery::Impl {
    Impl(DeviceInfo  local_device, int tcp_port, PeerFoundCallback on_peer_found)
        : local_device(std::move(local_device)), tcp_port(tcp_port), on_peer_found(std::move(on_peer_found)) {}
    bool start() {
        if (running.exchange(true)) {
            return true;
        }
        service_addr.sin_family = AF_INET;
        service_addr.sin_addr.s_addr = INADDR_ANY;
        service_addr.sin_port = htons(MDNS_PORT);
        service_socket = mdns_socket_open_ipv4(&service_addr);
        if (service_socket < 0) {
            Logger::warn("unable to open service socket.");
            running = false;
            return false;
        }
        discovery_socket = mdns_socket_open_ipv4(nullptr);
        if (discovery_socket < 0) {
            Logger::warn("unable to open discovery socket.");
            mdns_socket_close(service_socket);
            service_socket = -1;
            running = false;
            return false;
        }
        announced = build_announced_info();
        announce(false);
        send_query();
        mdns_thread = StackThread(32 * 1024, "kc-mdns", &Impl::mdns_loop, this);
        return true;
    }
    void stop() {
        if (!running.exchange(false)) {
            return;
        }
        if (mdns_thread.joinable()) {
            mdns_thread.join();
        }
        announce(true);
        if (service_socket >= 0) {
            mdns_socket_close(service_socket);
            service_socket = -1;
        }
        if (discovery_socket >= 0) {
            mdns_socket_close(discovery_socket);
            discovery_socket = -1;
        }
    }
    AnnouncedInfo build_announced_info() {
        AnnouncedInfo info;
        info.service_type = kServiceType;
        info.service_instance = local_device.id + "." + info.service_type;
        info.hostname = hostname_or_default();
        info.addresses_v4 = collect_ipv4_addresses();
        info.port = static_cast<uint16_t>(tcp_port);
        info.txt_records.emplace("id", local_device.id);
        info.txt_records.emplace("name", local_device.name);
        info.txt_records.emplace("type", local_device.type);
        info.txt_records.emplace("protocol", std::to_string(local_device.protocol_version));
        return info;
    }
    void announce(bool goodbye) {
        if (service_socket < 0) {
            return;
        }
        mdns_record_t ptr_record = make_record(announced, MDNS_RECORDTYPE_PTR);
        std::vector<mdns_record_t> additional;
        additional.reserve(2 + announced.addresses_v4.size() + announced.txt_records.size());
        additional.push_back(make_record(announced, MDNS_RECORDTYPE_SRV));
        for (const auto& address : announced.addresses_v4) {
            additional.push_back(make_record(announced, MDNS_RECORDTYPE_A, &address));
        }
        for (const auto& txt : announced.txt_records) {
            additional.push_back(make_record(announced, MDNS_RECORDTYPE_TXT, nullptr, &txt));
        }
        static char buffer[2048];
        if (goodbye) {
            mdns_goodbye_multicast(service_socket, buffer, sizeof(buffer), ptr_record, nullptr, 0,
                                   additional.data(), additional.size());
        } else {
            mdns_announce_multicast(service_socket, buffer, sizeof(buffer), ptr_record, nullptr, 0,
                                   additional.data(), additional.size());
        }
    }
    void send_query() const {
        if (discovery_socket < 0) {
            return;
        }
        static std::array<uint32_t, 512> buffer{};
        mdns_query_send(discovery_socket, MDNS_RECORDTYPE_PTR, announced.service_type.c_str(),
                        announced.service_type.size(), buffer.data(), buffer.size() * sizeof(uint32_t), 0);
    }
    void mdns_loop() {
        std::array<uint32_t, 512> svc_buf{};
        std::array<uint32_t, 512> disc_buf{};
        auto next_query = std::chrono::steady_clock::now();

        while (running.load()) {
            const auto now = std::chrono::steady_clock::now();
            if (now >= next_query) {
                send_query();
                next_query = now + kQueryRepeat;
            }

            fd_set rfds;
            FD_ZERO(&rfds);
            int nfds = 0;
            if (service_socket >= 0)   { FD_SET(service_socket,   &rfds); nfds = std::max(nfds, service_socket   + 1); }
            if (discovery_socket >= 0) { FD_SET(discovery_socket, &rfds); nfds = std::max(nfds, discovery_socket + 1); }
            if (nfds == 0) break;

            timeval tv{0, 100'000}; // 100 ms
            if (select(nfds, &rfds, nullptr, nullptr, &tv) <= 0) continue;

            if (service_socket >= 0 && FD_ISSET(service_socket, &rfds)) {
                mdns_socket_listen(service_socket, svc_buf.data(), svc_buf.size() * sizeof(uint32_t),
                                   service_callback, &announced);
            }
            if (discovery_socket >= 0 && FD_ISSET(discovery_socket, &rfds)) {
                QueryState state;
                mdns_query_recv(discovery_socket, disc_buf.data(), disc_buf.size() * sizeof(uint32_t),
                                discovery_callback, &state, 0);
                if (state.saw_ptr && !state.instance_id.empty() && !state.host.empty() &&
                    !state.device_id.empty() && state.device_id != local_device.id) {
                    if (on_peer_found) on_peer_found(state.device_id, state.host);
                }
            }
        }
    }
    DeviceInfo local_device;
    int tcp_port = 0;
    PeerFoundCallback on_peer_found;
    std::atomic<bool> running{false};
    int service_socket = -1;
    int discovery_socket = -1;
    sockaddr_in service_addr{};
    AnnouncedInfo announced;
    StackThread mdns_thread;
};
MdnsDiscovery::MdnsDiscovery(const DeviceInfo& local_device, int tcp_port, PeerFoundCallback on_peer_found)
    : impl_(std::make_unique<Impl>(local_device, tcp_port, std::move(on_peer_found))) {}
MdnsDiscovery::~MdnsDiscovery() {
    stop();
}
bool MdnsDiscovery::start() const {
    return impl_->start();
}
void MdnsDiscovery::stop() const {
    impl_->stop();
}
