#include "album_art.h"
#include "logger.h"

#include <cstdio>
#include <dirent.h>
#include <sys/stat.h>

namespace AlbumArt {

std::string url_hash(const std::string& url) {
    // FNV-1a 64-bit
    uint64_t h = 14695981039346656037ULL;
    for (const unsigned char c : url) {
        h ^= c;
        h *= 1099511628211ULL;
    }
    char buf[17];
    snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(h));
    return buf;
}

std::string img_path(const std::string& hash) {
    return std::string(kDir) + "/" + hash;
}

void clear_dir() {
#ifdef __SWITCH__
    mkdir(kDir, 0755);
    DIR* d = opendir(kDir);
    if (!d) return;
    dirent* entry;
    while ((entry = readdir(d)) != nullptr) {
        const std::string name = entry->d_name;
        if (name == "." || name == "..") continue;
        remove((std::string(kDir) + "/" + name).c_str());
    }
    closedir(d);
#endif
}

bool download_raw(DeviceProvider* provider,
                  const std::shared_ptr<DeviceProvider::DeviceSession>& session,
                  NetworkPacket& np,
                  const std::string& hash) {
    const std::string path = img_path(hash);
    Logger::info("AlbumArt: downloading hash=%s size=%lld port=%d",
                 hash.c_str(), static_cast<long long>(np.payload_size), np.payload_port);

    if (!provider->download_payload(session, np, path)) {
        Logger::warn("AlbumArt: payload download failed (%s)", hash.c_str());
        return false;
    }

    // Validate magic bytes: must be JPEG, PNG, or WebP.
    unsigned char magic[12] = {};
#ifdef __SWITCH__
    {
        FsFileSystem* fs = fsdevGetDeviceFileSystem("sdmc");
        char path_buf[256];
        snprintf(path_buf, sizeof(path_buf), "%s", path.c_str());
        FsFile f;
        if (fs && R_SUCCEEDED(fsFsOpenFile(fs, path_buf, FsOpenMode_Read, &f))) {
            uint8_t stack_buf[12];
            u64 nr = 0;
            fsFileRead(&f, 0, stack_buf, sizeof(stack_buf), FsReadOption_None, &nr);
            memcpy(magic, stack_buf, nr);
            fsFileClose(&f);
        }
    }
#else
    if (FILE* f = fopen(path.c_str(), "rb")) {
        fread(magic, 1, sizeof(magic), f);
        fclose(f);
    }
#endif

    const bool is_jpeg = magic[0] == 0xFF && magic[1] == 0xD8 && magic[2] == 0xFF;
    const bool is_png  = magic[0] == 0x89 && magic[1] == 'P'  && magic[2] == 'N';
    const bool is_webp = memcmp(magic, "RIFF", 4) == 0 && memcmp(magic + 8, "WEBP", 4) == 0;

    if (!is_jpeg && !is_png && !is_webp) {
        Logger::warn("AlbumArt: payload is not a valid image (magic %02x %02x %02x), discarding %s",
                     magic[0], magic[1], magic[2], hash.c_str());
        remove(path.c_str());
        return false;
    }

    Logger::info("AlbumArt: saved %s image as %s",
                 is_jpeg ? "JPEG" : is_png ? "PNG" : "WebP", hash.c_str());
    return true;
}

} // namespace AlbumArt
