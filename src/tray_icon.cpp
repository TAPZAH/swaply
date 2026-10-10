#include "tray_icon.h"

#include "config.h"
#include "resource.h"
#include "version.h"

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <stdexcept>
#include <string_view>
#include <system_error>

namespace {

[[noreturn]] void throw_last_error(const char* what) {
    throw std::system_error(
        static_cast<int>(GetLastError()),
        std::system_category(),
        what);
}

class UniqueMenu {
public:
    UniqueMenu() : menu_(CreatePopupMenu()) {}

    ~UniqueMenu() {
        if (menu_ != nullptr) {
            DestroyMenu(menu_);
        }
    }

    UniqueMenu(const UniqueMenu&) = delete;
    UniqueMenu& operator=(const UniqueMenu&) = delete;

    [[nodiscard]] explicit operator bool() const noexcept { return menu_ != nullptr; }
    [[nodiscard]] HMENU get() const noexcept { return menu_; }

private:
    HMENU menu_;
};

void copy_tip(wchar_t* dest, std::size_t dest_size, std::wstring_view text) {
    const auto count = std::min(text.size(), dest_size - 1);
    std::copy_n(text.data(), count, dest);
    dest[count] = L'\0';
}

[[nodiscard]] HICON load_app_icon(HINSTANCE instance) {
    HICON icon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_APPICON));
    if (icon == nullptr) {
        icon = LoadIconW(instance, IDI_APPLICATION);
    }
    if (icon == nullptr) {
        icon = LoadIconW(nullptr, IDI_APPLICATION);
    }
    return icon;
}

}  // namespace

TrayIcon::TrayIcon(HINSTANCE instance, HWND window)
    : instance_(instance), window_(window) {
    if (instance_ == nullptr || window_ == nullptr) {
        throw std::invalid_argument("TrayIcon requires a valid instance and window");
    }

    data_.cbSize = sizeof(data_);
    data_.hWnd = window_;
    data_.uID = 1;
    data_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    data_.uCallbackMessage = callback_message;
    data_.hIcon = load_app_icon(instance_);
    copy_tip(data_.szTip, std::size(data_.szTip), L"Swaply " SWAPLY_VERSION_STRW);

    add();
}

TrayIcon::~TrayIcon() {
    remove();
}

void TrayIcon::set_menu_state(
    bool enabled,
    bool auto_switch,
    bool autostart,
    bool keep_after_enter_tab) noexcept {
    enabled_ = enabled;
    auto_switch_ = auto_switch;
    autostart_ = autostart;
    keep_after_enter_tab_ = keep_after_enter_tab;
    copy_tip(
        data_.szTip,
        std::size(data_.szTip),
        enabled_ ? L"Swaply " SWAPLY_VERSION_STRW : L"Swaply " SWAPLY_VERSION_STRW L" (пауза)");
    if (added_) {
        Shell_NotifyIconW(NIM_MODIFY, &data_);
    }
}

bool TrayIcon::handle_message(UINT msg, WPARAM, LPARAM lparam) {
    if (msg == callback_message) {
        switch (LOWORD(lparam)) {
        case WM_LBUTTONDBLCLK:
            PostMessageW(window_, WM_COMMAND, settings_command_id, 0);
            break;
        case WM_RBUTTONUP:
        case WM_CONTEXTMENU:
            show_context_menu();
            break;
        default:
            break;
        }
        return true;
    }

    return false;
}

void TrayIcon::restore_after_explorer_restart() {
    added_ = false;
    if (!Shell_NotifyIconW(NIM_ADD, &data_)) {
        return;
    }
    added_ = true;
}

void TrayIcon::add() {
    if (added_) {
        return;
    }

    if (!Shell_NotifyIconW(NIM_ADD, &data_)) {
        throw_last_error("Shell_NotifyIconW(NIM_ADD) failed");
    }
    added_ = true;
}

void TrayIcon::remove() noexcept {
    if (!added_) {
        return;
    }

    Shell_NotifyIconW(NIM_DELETE, &data_);
    added_ = false;
}

void TrayIcon::show_context_menu() const {
    UniqueMenu menu;
    if (!menu) {
        return;
    }

    const UINT enabled_flags = MF_STRING | (enabled_ ? MF_CHECKED : MF_UNCHECKED);
    const UINT auto_flags = MF_STRING | (auto_switch_ ? MF_CHECKED : MF_UNCHECKED);
    const UINT start_flags = MF_STRING |
        (is_portable_install() ? (MF_GRAYED | MF_DISABLED) : 0) |
        (autostart_ ? MF_CHECKED : MF_UNCHECKED);
    const UINT keep_flags = MF_STRING |
        (keep_after_enter_tab_ ? MF_CHECKED : MF_UNCHECKED);
    if (!AppendMenuW(menu.get(), enabled_flags, enabled_command_id, L"Включено") ||
        !AppendMenuW(menu.get(), auto_flags, auto_switch_command_id, L"Автопереключение") ||
        !AppendMenuW(menu.get(), keep_flags, enter_tab_command_id, L"Хранить слово после Enter/Tab") ||
        !AppendMenuW(menu.get(), start_flags, autostart_command_id, L"Запускать с Windows") ||
        !AppendMenuW(menu.get(), MF_SEPARATOR, 0, nullptr) ||
        !AppendMenuW(menu.get(), MF_STRING, undo_command_id, L"Отменить конвертацию") ||
        !AppendMenuW(menu.get(), MF_STRING, settings_command_id, L"Параметры...") ||
        !AppendMenuW(menu.get(), MF_STRING, update_command_id, L"Проверить обновления...") ||
        !AppendMenuW(menu.get(), MF_STRING, about_command_id, L"О программе...") ||
        !AppendMenuW(menu.get(), MF_SEPARATOR, 0, nullptr) ||
        !AppendMenuW(menu.get(), MF_STRING, exit_command_id, L"Выход")) {
        return;
    }

    POINT cursor{};
    if (!GetCursorPos(&cursor)) {
        return;
    }

    SetForegroundWindow(window_);
    TrackPopupMenu(menu.get(), TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, cursor.x, cursor.y, 0, window_, nullptr);
    PostMessageW(window_, WM_NULL, 0, 0);
}
