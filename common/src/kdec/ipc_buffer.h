#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

class IpcWriter {
public:
    void write_string(const std::string& s) {
        auto len = static_cast<uint16_t>(s.size());
        write(len);
        buf_.insert(buf_.end(), s.begin(), s.end());
    }
    template<typename T> void write(const T& v) {
        const auto* p = reinterpret_cast<const uint8_t*>(&v);
        buf_.insert(buf_.end(), p, p + sizeof(T));
    }
    const std::vector<uint8_t>& data() const { return buf_; }
    size_t size() const { return buf_.size(); }
private:
    std::vector<uint8_t> buf_;
};

class IpcReader {
public:
    IpcReader(const uint8_t* data, size_t size) : ptr_(data), end_(data + size) {}
    std::string read_string() {
        auto len = read<uint16_t>();
        if (!ok_ || ptr_ + len > end_) { ok_ = false; return {}; }
        std::string s(reinterpret_cast<const char*>(ptr_), len);
        ptr_ += len;
        return s;
    }
    template<typename T> T read() {
        if (ptr_ + sizeof(T) > end_) { ok_ = false; return {}; }
        T v; std::memcpy(&v, ptr_, sizeof(T)); ptr_ += sizeof(T); return v;
    }
    bool ok() const { return ok_; }
    bool has_data() const { return ptr_ < end_; }
private:
    const uint8_t* ptr_;
    const uint8_t* end_;
    bool ok_ = true;
};

// Inline payload for KdecIpcCmd_SendMediaAction
struct KdecWireSendMediaAction {
    uint8_t action;
    uint8_t _pad[7];
    int64_t value;
};
static_assert(sizeof(KdecWireSendMediaAction) == 16);

// Inline payload for KdecIpcCmd_ReadSetting and KdecIpcCmd_WriteSetting
struct KdecWireSettingType {
    uint32_t type; // KdecSettingType cast to uint32_t
};
