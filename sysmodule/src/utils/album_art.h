#pragma once
#include <memory>
#include <string>
#include "../plugins/plugin.h"
#include "../net/network_packet.h"

namespace AlbumArt {

static constexpr int  kOutputDim = 80;  // display size used by the overlay
static constexpr auto kDir       = "/config/kdeconnect/album_art";

// 16-char lowercase hex FNV-1a hash of a URL.
std::string url_hash(const std::string& url);

// Full path to the raw downloaded image file for a given hash (no extension).
std::string img_path(const std::string& hash);

// Delete all files in kDir. Safe to call multiple times; creates the dir if missing.
void clear_dir();

// Download the payload in np to img_path(hash) without decoding.
// The overlay is responsible for decoding. Returns true on success.
bool download_raw(DeviceProvider* provider,
                  const std::shared_ptr<DeviceProvider::DeviceSession>& session,
                  NetworkPacket& np,
                  const std::string& hash);

} // namespace AlbumArt
