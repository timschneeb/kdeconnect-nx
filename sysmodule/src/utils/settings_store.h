#pragma once

#include <cstdint>
#include <vector>
#include <kdec/ipc.h>

// Thread-safe settings store. Call load() once at startup; all other functions
// are safe to call from any thread. Writes are persisted immediately via save().
namespace SettingsStore {

void load();
void save();

bool get(KdecBoolSettingKey key);
void set(KdecBoolSettingKey key, bool value);

int32_t get(KdecIntSettingKey key);
void set(KdecIntSettingKey key, int32_t value);

std::vector<KdecWireSettingEntry> get_all();

} // namespace SettingsStore
