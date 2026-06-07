#include "settings_gui.h"
#include "gui_common.h"
#include "git_version.h"
#include "elements/font_size_bar.h"
#include "notification_style_gui.h"
#include "../utils/symbols.h"

#include <kdec/ipc_client.h>

#include "main_gui.h"

static constexpr int32_t kDurationSteps[]  = {1000, 2000, 3000, 4000, 5000, 6000, 8000, 10000, 15000};
static constexpr int32_t kSeekSteps[]      = {5000, 10000, 15000, 20000, 30000, 45000, 60000};
static constexpr int32_t kFontSizeSteps[]  = {14, 16, 18, 20, 22, 24, 26};

static int findStepIndex(const int32_t* steps, const size_t count, const int32_t value) {
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
    toggle_remote->setStateChangedListener([](const bool v) {
        kdecIpcWriteBoolSetting(KdecBoolSettingKey::NotificationShowRemoteMessages, v);
    });
    list->addItem(toggle_remote);

    bool show_on_connect = false;
    kdecIpcReadBoolSetting(KdecBoolSettingKey::NotificationShowOnConnect, show_on_connect);
    auto* toggle_connect = new tsl::elm::ToggleListItem("Notify on connect", show_on_connect);
    toggle_connect->setStateChangedListener([](const bool v) {
        kdecIpcWriteBoolSetting(KdecBoolSettingKey::NotificationShowOnConnect, v);
    });
    list->addItem(toggle_connect);

    bool notify_battery_low = true;
    kdecIpcReadBoolSetting(KdecBoolSettingKey::NotificationShowOnBatteryLow, notify_battery_low);
    auto* toggle_battery_low = new tsl::elm::ToggleListItem("Notify on low battery", notify_battery_low);
    toggle_battery_low->setStateChangedListener([](const bool v) {
        kdecIpcWriteBoolSetting(KdecBoolSettingKey::NotificationShowOnBatteryLow, v);
    });
    list->addItem(toggle_battery_low);

    bool show_icon = true;
    kdecIpcReadBoolSetting(KdecBoolSettingKey::NotificationShowIcon, show_icon);
    auto* toggle_icon = new tsl::elm::ToggleListItem("Show app icons", show_icon);
    toggle_icon->setStateChangedListener([](const bool v) {
        kdecIpcWriteBoolSetting(KdecBoolSettingKey::NotificationShowIcon, v);
    });
    list->addItem(toggle_icon);

    bool show_time = true;
    kdecIpcReadBoolSetting(KdecBoolSettingKey::NotificationShowTime, show_time);
    auto* toggle_time = new tsl::elm::ToggleListItem("Show time", show_time);
    toggle_time->setStateChangedListener([](const bool v) {
        kdecIpcWriteBoolSetting(KdecBoolSettingKey::NotificationShowTime, v);
    });
    list->addItem(toggle_time);

    int32_t style_raw = 0;
    kdecIpcReadIntSetting(KdecIntSettingKey::NotificationStyle, style_raw);
    auto* style_item = new tsl::elm::ListItem("Notification style", "Type " + std::to_string(style_raw + 1));
    style_item->setClickListener([](const u64 keys) -> bool {
        if (keys & HidNpadButton_A) {
            tsl::changeTo<NotificationStyleGui>();
            return true;
        }
        return false;
    });
    list->addItem(style_item);

    // Create the font size bar first so the toggle's listener can capture it
    int32_t font_size = 22;
    kdecIpcReadIntSetting(KdecIntSettingKey::NotificationFontSize, font_size);
    constexpr size_t kFontCount = std::size(kFontSizeSteps);
    auto* font_bar = new FontSizeBar(
        "",
        {"14", "16", "18", "20", "22", "24", "26"},
        true, "Font size"
    );
    font_bar->setProgress(static_cast<u16>(findStepIndex(kFontSizeSteps, kFontCount, font_size)));
    font_bar->setValueChangedListener([](const u16 idx) {
        if (idx < kFontCount)
            kdecIpcWriteIntSetting(KdecIntSettingKey::NotificationFontSize, kFontSizeSteps[idx]);
    });

    bool dynamic_font = false;
    kdecIpcReadBoolSetting(KdecBoolSettingKey::NotificationDynamicFontSize, dynamic_font);
    font_bar->setEnabled(!dynamic_font);
    auto* toggle_dynfont = new tsl::elm::ToggleListItem("Dynamic font size", dynamic_font);
    toggle_dynfont->setStateChangedListener([font_bar](const bool v) {
        kdecIpcWriteBoolSetting(KdecBoolSettingKey::NotificationDynamicFontSize, v);
        font_bar->setEnabled(!v);
    });
    list->addItem(toggle_dynfont);
    list->addItem(font_bar, tsl::style::TrackBarDefaultHeight);

    int32_t duration = 4000;
    kdecIpcReadIntSetting(KdecIntSettingKey::NotificationDuration, duration);
    constexpr size_t kDurCount = std::size(kDurationSteps);
    auto* dur_bar = new tsl::elm::NamedStepTrackBar(
        "",
        {"1s", "2s", "3s", "4s", "5s", "6s", "8s", "10s", "15s"},
        true, "Notification duration"
    );
    dur_bar->setProgress(static_cast<u16>(findStepIndex(kDurationSteps, kDurCount, duration)));
    dur_bar->setValueChangedListener([](const u16 idx) {
        if (idx < kDurCount)
            kdecIpcWriteIntSetting(KdecIntSettingKey::NotificationDuration, kDurationSteps[idx]);
    });
    list->addItem(dur_bar, tsl::style::TrackBarDefaultHeight);

    auto* test_notif = new tsl::elm::ListItem("Send test notification");
    test_notif->setValue(sym::mail, true);
    test_notif->setClickListener([](const u64 keys) {
        if (keys & HidNpadButton_A) {
            kdecIpcSendTestNotification();
            return true;
        }
        return false;
    });
    list->addItem(test_notif);

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
    seek_bar->setValueChangedListener([](const u16 idx) {
        if (idx < kSeekCount)
            kdecIpcWriteIntSetting(KdecIntSettingKey::MprisSeekStepSize, kSeekSteps[idx]);
    });
    list->addItem(seek_bar, tsl::style::TrackBarDefaultHeight);

    bool show_art = true;
    kdecIpcReadBoolSetting(KdecBoolSettingKey::MprisShowAlbumArt, show_art);
    auto* toggle_show_art = new tsl::elm::ToggleListItem("Show thumbnail (if available)", show_art);
    toggle_show_art->setStateChangedListener([](const bool v) {
        kdecIpcWriteBoolSetting(KdecBoolSettingKey::MprisShowAlbumArt, v);
    });
    list->addItem(toggle_show_art);

    bool art_top = false;
    kdecIpcReadBoolSetting(KdecBoolSettingKey::MprisAlbumArtTopLayout, art_top);
    auto* toggle_art_top = new tsl::elm::ToggleListItem("Thumbnail above track info", art_top);
    toggle_art_top->setStateChangedListener([](const bool v) {
        kdecIpcWriteBoolSetting(KdecBoolSettingKey::MprisAlbumArtTopLayout, v);
    });
    list->addItem(toggle_art_top);

    list->addItem(new tsl::elm::CategoryHeader("Commands"));

    bool power_enabled = true;
    kdecIpcReadBoolSetting(KdecBoolSettingKey::RunCommandPowerCommandsEnabled, power_enabled);
    auto* toggle_power = new tsl::elm::ToggleListItem("Allow power commands", power_enabled);
    toggle_power->setStateChangedListener([](const bool v) {
        kdecIpcWriteBoolSetting(KdecBoolSettingKey::RunCommandPowerCommandsEnabled, v);
    });
    list->addItem(toggle_power);

    list->addItem(new tsl::elm::CategoryHeader("Debugging"));

    KdecMemoryInfo mem_info{};
    kdecIpcGetMemoryInfo(mem_info);
    char heap_buf[64];
    std::snprintf(heap_buf, sizeof(heap_buf), "%zu KB / %zu KB",
                  mem_info.heap_used_kb, mem_info.heap_max_kb);
    m_heap_item = new tsl::elm::ListItem("Heap usage");
    m_heap_item->setValue(heap_buf);
    list->addItem(m_heap_item);

    m_heap_arena = new tsl::elm::ListItem("Heap arena size");
    std::snprintf(heap_buf, sizeof(heap_buf), "%zu KB", mem_info.heap_total_kb);
    m_heap_arena->setValue(heap_buf);
    list->addItem(m_heap_arena);

    auto* used_tmem_item = new tsl::elm::ListItem("Socket buffers (Heap)");
    std::snprintf(heap_buf, sizeof(heap_buf), "%zu KB", mem_info.socket_tmem_kb);
    used_tmem_item->setValue(heap_buf);
    list->addItem(used_tmem_item);

    auto* used_mem_item = new tsl::elm::ListItem("Total used memory");
    std::snprintf(heap_buf, sizeof(heap_buf), "%zu KB", mem_info.proc_used_kb);
    used_mem_item->setValue(heap_buf);
    list->addItem(used_mem_item);

    KdecVersionInfo ver_info{};
    kdecIpcGetVersionInfo(ver_info);

    auto is_version_different = std::strcmp(ver_info.version, MINIKDECONNECT_VERSION) != 0 ||
        std::strcmp(ver_info.git_commit, GIT_COMMIT_HASH) != 0;

    {
        char ver_buf[64];
        if (ver_info.is_debug)
            std::snprintf(ver_buf, sizeof(ver_buf), "v%s-dbg-%s", ver_info.version, ver_info.git_commit);
        else
            std::snprintf(ver_buf, sizeof(ver_buf), "v%s-%s", ver_info.version, ver_info.git_commit);
        auto* sysmodule_ver = new tsl::elm::ListItem(is_version_different ? "Sysmodule version" : "Version");
        sysmodule_ver->setValue(ver_buf);
        list->addItem(sysmodule_ver);
    }

    if (is_version_different) {
        char ovl_buf[64];
        std::snprintf(ovl_buf, sizeof(ovl_buf), "v%s-%s", MINIKDECONNECT_VERSION, GIT_COMMIT_HASH);
        auto* overlay_ver = new tsl::elm::ListItem("Overlay version");
        overlay_ver->setValue(ovl_buf);
        list->addItem(overlay_ver);
    }

    auto* kill_sysmodule = new tsl::elm::ListItem("Kill sysmodule");
    kill_sysmodule->setValue(sym::cross);
    kill_sysmodule->setValueColor(tsl::Color(255, 0, 0, 255));
    kill_sysmodule->setClickListener([](const u64 keys) {
        if (keys & HidNpadButton_A) {
            if (R_SUCCEEDED(pmshellInitialize())) {
                pmshellTerminateProgram(SYSMODULE_TITLE_ID);
                pmshellExit();
                svcSleepThread(500'000'000LL);
                tsl::swapTo<MainGui>(SwapDepth{2});
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
    std::snprintf(heap_buf, sizeof(heap_buf), "%zu KB", mem_info.heap_total_kb);
    m_heap_arena->setValue(heap_buf);
}

bool SettingsGui::handleInput(const u64 keysDown, u64, const HidTouchState&,
                              HidAnalogStickState, HidAnalogStickState) {
    if (keysDown & HidNpadButton_B) { tsl::goBack(); return true; }
    return false;
}
