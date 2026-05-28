#include "settings_gui.h"
#include "gui_common.h"
#include "../utils/symbols.h"

#include <kdec/ipc_client.h>

static constexpr int32_t kDurationSteps[] = {1000, 2000, 3000, 4000, 5000, 6000, 8000, 10000, 15000};
static constexpr int32_t kSeekSteps[]     = {5000, 10000, 15000, 20000, 30000, 45000, 60000};

static int findStepIndex(const int32_t* steps, size_t count, int32_t value) {
    for (size_t i = 0; i < count; ++i)
        if (steps[i] == value) return static_cast<int>(i);
    return 0;
}

tsl::elm::Element* SettingsGui::createUI() {
    auto* frame = new tsl::elm::OverlayFrame("Settings", "KDE Connect NX");
    auto* list  = new tsl::elm::List();

    list->addItem(new tsl::elm::CategoryHeader("Notifications"));

    bool show_remote = true;
    kdecIpcReadBoolSetting(KdecBoolSettingKey::NotificationShowRemoteMessages, show_remote);
    auto* toggle_remote = new tsl::elm::ToggleListItem("Show notifications", show_remote);
    toggle_remote->setStateChangedListener([](bool v) {
        kdecIpcWriteBoolSetting(KdecBoolSettingKey::NotificationShowRemoteMessages, v);
    });
    list->addItem(toggle_remote);

    bool show_on_connect = false;
    kdecIpcReadBoolSetting(KdecBoolSettingKey::NotificationShowOnConnect, show_on_connect);
    auto* toggle_connect = new tsl::elm::ToggleListItem("Notify on connect", show_on_connect);
    toggle_connect->setStateChangedListener([](bool v) {
        kdecIpcWriteBoolSetting(KdecBoolSettingKey::NotificationShowOnConnect, v);
    });
    list->addItem(toggle_connect);

    bool show_icon = true;
    kdecIpcReadBoolSetting(KdecBoolSettingKey::NotificationShowIcon, show_icon);
    auto* toggle_icon = new tsl::elm::ToggleListItem("Show app icons", show_icon);
    toggle_icon->setStateChangedListener([](bool v) {
        kdecIpcWriteBoolSetting(KdecBoolSettingKey::NotificationShowIcon, v);
    });
    list->addItem(toggle_icon);

    int32_t duration = 4000;
    kdecIpcReadIntSetting(KdecIntSettingKey::NotificationDuration, duration);
    constexpr size_t kDurCount = std::size(kDurationSteps);
    auto* dur_bar = new tsl::elm::NamedStepTrackBar(
        "",
        {"1s", "2s", "3s", "4s", "5s", "6s", "8s", "10s", "15s"},
        true, "Notification duration"
    );
    dur_bar->setProgress(static_cast<u16>(findStepIndex(kDurationSteps, kDurCount, duration)));
    dur_bar->setValueChangedListener([](u16 idx) {
        if (idx < kDurCount)
            kdecIpcWriteIntSetting(KdecIntSettingKey::NotificationDuration, kDurationSteps[idx]);
    });
    list->addItem(dur_bar, tsl::style::TrackBarDefaultHeight);

    list->addItem(new tsl::elm::CategoryHeader("Media"));

    int32_t seek_step = 10000;
    kdecIpcReadIntSetting(KdecIntSettingKey::MprisSeekStepSize, seek_step);
    constexpr size_t kSeekCount = std::size(kSeekSteps);
    auto* seek_bar = new tsl::elm::NamedStepTrackBar(
        "",
        {"5s", "10s", "15s", "20s", "30s", "45s", "60s"},
        true, "Rewind/Fast-forward step size"
    );
    seek_bar->setProgress(static_cast<u16>(findStepIndex(kSeekSteps, kSeekCount, seek_step)));
    seek_bar->setValueChangedListener([](u16 idx) {
        if (idx < kSeekCount)
            kdecIpcWriteIntSetting(KdecIntSettingKey::MprisSeekStepSize, kSeekSteps[idx]);
    });
    list->addItem(seek_bar, tsl::style::TrackBarDefaultHeight);

    list->addItem(new tsl::elm::CategoryHeader("Debugging"));

    KdecMemoryInfo mem_info{};
    kdecIpcGetMemoryInfo(mem_info);
    char heap_buf[64];
    std::snprintf(heap_buf, sizeof(heap_buf), "%zu KB / %zu KB",
                  mem_info.heap_used_kb, mem_info.heap_max_kb);
    m_heap_item = new tsl::elm::ListItem("Heap usage");
    m_heap_item->setValue(heap_buf);
    list->addItem(m_heap_item);

    auto* peak_heap = new tsl::elm::ListItem("Peak heap usage");
    std::snprintf(heap_buf, sizeof(heap_buf), "%zu KB", mem_info.heap_total_kb);
    peak_heap->setValue(heap_buf);
    list->addItem(peak_heap);

    auto* used_tmem_item = new tsl::elm::ListItem("Socket buffers (Heap)");
    std::snprintf(heap_buf, sizeof(heap_buf), "%zu KB", mem_info.socket_tmem_kb);
    used_tmem_item->setValue(heap_buf);
    list->addItem(used_tmem_item);

    auto* used_mem_item = new tsl::elm::ListItem("Total used memory");
    std::snprintf(heap_buf, sizeof(heap_buf), "%zu KB", mem_info.proc_used_kb);
    used_mem_item->setValue(heap_buf);
    list->addItem(used_mem_item);

    auto* kill_sysmodule = new tsl::elm::ListItem("Kill sysmodule");
    kill_sysmodule->setValue(sym::cross);
    kill_sysmodule->setValueColor(tsl::Color(255, 0, 0, 255));
    kill_sysmodule->setClickListener([](u64 keys) {
        if (keys & HidNpadButton_A) {
            if (R_SUCCEEDED(pmshellInitialize())) {
                pmshellTerminateProgram(SYSMODULE_TITLE_ID);
                pmshellExit();
                svcSleepThread(500'000'000LL);
                tsl::goBack();
            }
            return true;
        }
        return false;
    });
    list->addItem(kill_sysmodule);

    frame->setContent(list);
    return frame;
}

void SettingsGui::update() {
    if (!m_heap_item) return;
    if (++m_update_counter < 120) return; // ~2 s at 60 fps
    m_update_counter = 0;

    KdecMemoryInfo mem_info{};
    kdecIpcGetMemoryInfo(mem_info);
    char heap_buf[64];
    std::snprintf(heap_buf, sizeof(heap_buf), "%zu KB / %zu KB",
                  mem_info.heap_used_kb, mem_info.heap_max_kb);
    m_heap_item->setValue(heap_buf);
}

bool SettingsGui::handleInput(u64 keysDown, u64, const HidTouchState&,
                              HidAnalogStickState, HidAnalogStickState) {
    if (keysDown & HidNpadButton_B) { tsl::goBack(); return true; }
    return false;
}
