#pragma once

#include <Windows.h>
#include <shellapi.h>

class TrayIcon {
public:
    static constexpr UINT callback_message = WM_APP + 1;
    static constexpr UINT exit_command_id = 1;
    static constexpr UINT enabled_command_id = 2;
    static constexpr UINT auto_switch_command_id = 3;
    static constexpr UINT autostart_command_id = 4;
    static constexpr UINT settings_command_id = 5;
    static constexpr UINT undo_command_id = 6;
    static constexpr UINT about_command_id = 7;
    static constexpr UINT update_command_id = 8;
    static constexpr UINT enter_tab_command_id = 9;

    TrayIcon(HINSTANCE instance, HWND window);
    ~TrayIcon();

    TrayIcon(const TrayIcon&) = delete;
    TrayIcon& operator=(const TrayIcon&) = delete;
    TrayIcon(TrayIcon&&) = delete;
    TrayIcon& operator=(TrayIcon&&) = delete;

    void set_menu_state(
        bool enabled,
        bool auto_switch,
        bool autostart,
        bool keep_after_enter_tab) noexcept;

    // Returns true when the message was handled.
    [[nodiscard]] bool handle_message(UINT msg, WPARAM wparam, LPARAM lparam);

    void restore_after_explorer_restart();

private:
    void add();
    void remove() noexcept;
    void show_context_menu() const;

    HINSTANCE instance_;
    HWND window_;
    NOTIFYICONDATAW data_{};
    bool added_ = false;
    bool enabled_ = true;
    bool auto_switch_ = true;
    bool autostart_ = false;
    bool keep_after_enter_tab_ = false;
};
