#pragma once

#include <tesla.hpp>
#include "../utils/media_symbols.hpp"
#include <functional>
#include <string>

// ============================================================================
// MediaIconButton — a circular button with an AlphaSymbol icon.
// Subclasses tsl::elm::Element with proper focus/click behaviour.
// ============================================================================
class MediaIconButton final : public tsl::elm::Element {
public:
    MediaIconButton(const AlphaSymbol* icon, std::function<void()> action);

    // tsl::elm::Element overrides
    tsl::elm::Element* requestFocus(tsl::elm::Element* oldFocus, tsl::FocusDirection dir) override;
    bool               onClick(u64 keys) override;
    void               draw(tsl::gfx::Renderer* renderer) override;
    void               layout(u16, u16, u16, u16) override {}          // bounds set by row
    void               drawFocusBackground(tsl::gfx::Renderer* renderer) override; // decay only
    void               drawHighlight(tsl::gfx::Renderer* renderer) override;       // circle

    void setIcon(const AlphaSymbol* icon)            { m_icon = icon; }
    void setAction(std::function<void()> action)     { m_action = std::move(action); }
    void setDisabled(bool disabled)                  { m_disabled = disabled; }
    bool isDisabled() const                          { return m_disabled; }

private:
    const AlphaSymbol*    m_icon;
    std::function<void()> m_action;
    bool                  m_disabled = false;
};

// ============================================================================
// MediaButtonRow — horizontal container for 5 MediaIconButtons.
// Handles focus navigation (Left/Right) between the buttons internally.
// ============================================================================
class MediaButtonRow final : public tsl::elm::Element {
public:
    // Button indices
    enum : int { IDX_PREV = 0, IDX_REWIND = 1, IDX_PLAY = 2, IDX_FFORWARD = 3, IDX_NEXT = 4 };

    MediaButtonRow();
    ~MediaButtonRow();

    // Configure a single button (call in createUI after construction).
    void setButton(int idx, const AlphaSymbol* icon, std::function<void()> action,
                   bool disabled = false);
    void setButtonDisabled(int idx, bool disabled);
    void setButtonIcon(int idx, const AlphaSymbol* icon);

    // tsl::elm::Element overrides
    tsl::elm::Element* requestFocus(tsl::elm::Element* oldFocus, tsl::FocusDirection dir) override;
    bool               handleInput(u64 keysDown, u64 keysHeld, const HidTouchState&,
                                   HidAnalogStickState leftStick,
                                   HidAnalogStickState rightStick) override;
    void               draw(tsl::gfx::Renderer* renderer) override;
    void               layout(u16 parentX, u16 parentY, u16 parentWidth, u16 parentHeight) override;
    void               drawFocusBackground(tsl::gfx::Renderer*) override {}
    void               drawHighlight(tsl::gfx::Renderer*)       override {}

    static constexpr s32 Height = tsl::style::ListItemDefaultHeight + 20;

private:
    MediaIconButton* m_buttons[5] = {};
    int              m_active     = IDX_PLAY;   // focused button within the row
    int8_t           m_stick_dir  = 0;          // -1 left 0 neutral +1 right (joystick edge detect)
};

// ============================================================================
// MediaSeekBar — TrackBar subclass for seek position.
// Uses V2 style and overrides draw() to show live time labels.
// ============================================================================
class MediaSeekBar final : public tsl::elm::TrackBar {
public:
    explicit MediaSeekBar(std::string device_id);

    // Update displayed position from IPC poll (does NOT trigger the listener).
    void setPositionMs(int64_t pos_ms, int64_t len_ms);

    void draw(tsl::gfx::Renderer* renderer) override;

private:
    std::string m_device_id;
    int64_t     m_pos_ms = 0;
    int64_t     m_len_ms = 0;

    static std::string fmtTime(int64_t ms);
};

// ============================================================================
// MediaTitleBar — scrolling title / artist display (non-interactive).
// ============================================================================
class MediaTitleBar final : public tsl::elm::Element {
public:
    MediaTitleBar();

    void setInfo(const std::string& title, const std::string& artist);

    void               draw(tsl::gfx::Renderer* renderer) override;
    void               layout(u16 px, u16 py, u16 pw, u16) override;
    tsl::elm::Element* requestFocus(tsl::elm::Element*, tsl::FocusDirection) override { return nullptr; }
    void               drawFocusBackground(tsl::gfx::Renderer*) override {}
    void               drawHighlight(tsl::gfx::Renderer*)       override {}

    static constexpr s32 Height = 100;

private:
    std::string m_title;
    std::string m_artist;

    // Scroll state — title
    std::string m_title_scroll;
    u32  m_tw    = 0;
    u32  m_toff  = 0;
    bool m_ttrunc = false;
    u8   m_tctr  = 0;

    // Scroll state — artist
    std::string m_artist_scroll;
    u32  m_aw    = 0;
    u32  m_aoff  = 0;
    bool m_atrunc = false;
    u8   m_actr  = 0;
};

// ============================================================================
// MediaGui
// ============================================================================
class MediaGui : public tsl::Gui {
public:
    MediaGui(std::string device_id, std::string device_name);

    tsl::elm::Element* createUI() override;
    bool               handleInput(u64 keysDown, u64, const HidTouchState&,
                                   HidAnalogStickState, HidAnalogStickState) override;
    void               update() override;

private:
    std::string m_device_id;
    std::string m_device_name;

    MediaTitleBar*  m_title_bar = nullptr;
    MediaSeekBar*   m_seek_bar  = nullptr;
    MediaButtonRow* m_btn_row   = nullptr;

    uint32_t m_tick = 0;

    void pollAndUpdate();
};
