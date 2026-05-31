#include "media_gui.h"
#include "gui_common.h"
#include "../utils/media_symbols.hpp"

#include <kdec/ipc_client.h>
#include <string>

#include "elements/media_button_row.h"
#include "elements/media_seek_bar.h"
#include "elements/media_title_bar.h"

MediaGui::MediaGui(std::string device_id, std::string device_name)
    : m_device_id(std::move(device_id)), m_device_name(std::move(device_name)) {}

tsl::elm::Element* MediaGui::createUI() {
    auto* frame = new tsl::elm::OverlayFrame("Media Remote", m_device_name);
    auto* list  = new tsl::elm::List();

    m_title_bar = new MediaTitleBar();
    list->addItem(m_title_bar, MediaTitleBar::Height);

    m_seek_bar = new MediaSeekBar(m_device_id);
    list->addItem(m_seek_bar, tsl::style::TrackBarDefaultHeight);

    int32_t seek_step = 10000;
    kdecIpcReadIntSetting(KdecIntSettingKey::MprisSeekStepSize, seek_step);

    m_btn_row = new MediaButtonRow();
    m_btn_row->setButton(MediaButtonRow::IDX_REWIND,   &media_sym::backward::symbol,
        [id = m_device_id, step = seek_step]{ kdecIpcSendMediaAction(id, KdecMediaAction::Seek, -step); });
    m_btn_row->setButton(MediaButtonRow::IDX_PREV,     &media_sym::prev::symbol,
        [id = m_device_id]{ kdecIpcSendMediaAction(id, KdecMediaAction::Previous); });
    m_btn_row->setButton(MediaButtonRow::IDX_PLAY,     &media_sym::play::symbol,
        [id = m_device_id]{ kdecIpcSendMediaAction(id, KdecMediaAction::PlayPause); });
    m_btn_row->setButton(MediaButtonRow::IDX_NEXT,     &media_sym::next::symbol,
        [id = m_device_id]{ kdecIpcSendMediaAction(id, KdecMediaAction::Next); });
    m_btn_row->setButton(MediaButtonRow::IDX_FFORWARD, &media_sym::forward::symbol,
        [id = m_device_id, step = seek_step]{ kdecIpcSendMediaAction(id, KdecMediaAction::Seek, step); });
    list->addItem(m_btn_row, MediaButtonRow::Height);

    frame->setContent(list);
    pollAndUpdate();
    return frame;
}

bool MediaGui::handleInput(const u64 keysDown, u64, const HidTouchState&,
                           HidAnalogStickState, HidAnalogStickState) {
    if (keysDown & HidNpadButton_B) { tsl::goBack(); return true; }
    return false;
}

void MediaGui::update() {
    if (++m_tick < 30) return;
    m_tick = 0;
    pollAndUpdate();
}

void MediaGui::pollAndUpdate() const {
    KdecMediaInfo info{};
    if (R_FAILED(kdecIpcGetMediaInfo(m_device_id, info))) {
        if (m_title_bar) m_title_bar->setInfo("No media playing", "");
        return;
    }

    const std::string title  = info.title[0]  ? info.title  : "Unknown";
    const std::string artist = info.artist[0] ? info.artist : info.player;

    if (m_title_bar) m_title_bar->setInfo(title, artist);
    if (m_seek_bar) {
        m_seek_bar->setSeekable(info.can_seek);
        m_seek_bar->setPositionMs(info.can_seek ? info.position : 0,
                                  info.can_seek ? info.length   : 0);
    }

    if (m_btn_row) {
        m_btn_row->setButtonIcon(MediaButtonRow::IDX_PLAY,
            info.is_playing ? &media_sym::pause::symbol : &media_sym::play::symbol);
        m_btn_row->setButtonDisabled(MediaButtonRow::IDX_PREV,     !info.can_go_previous);
        m_btn_row->setButtonDisabled(MediaButtonRow::IDX_NEXT,     !info.can_go_next);
        m_btn_row->setButtonDisabled(MediaButtonRow::IDX_REWIND,   !info.can_seek);
        m_btn_row->setButtonDisabled(MediaButtonRow::IDX_FFORWARD, !info.can_seek);
    }
}
