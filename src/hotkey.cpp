#include "hotkey.h"

#include <algorithm>
#include <cctype>

namespace {

[[nodiscard]] std::string to_lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return text;
}

[[nodiscard]] UINT vk_from_name(std::string_view name) {
    const std::string key = to_lower(std::string(name));
    if (key == "pause" || key == "break") {
        return VK_PAUSE;
    }
    if (key == "scroll" || key == "scrolllock") {
        return VK_SCROLL;
    }
    if (key == "insert" || key == "ins") {
        return VK_INSERT;
    }
    if (key == "delete" || key == "del") {
        return VK_DELETE;
    }
    if (key == "home") {
        return VK_HOME;
    }
    if (key == "end") {
        return VK_END;
    }
    if (key == "space") {
        return VK_SPACE;
    }
    if (key.size() >= 2 && (key[0] == 'f' || key[0] == 'F')) {
        int n = 0;
        for (std::size_t i = 1; i < key.size(); ++i) {
            if (!std::isdigit(static_cast<unsigned char>(key[i]))) {
                n = 0;
                break;
            }
            n = n * 10 + (key[i] - '0');
        }
        if (n >= 1 && n <= 24) {
            return static_cast<UINT>(VK_F1 + n - 1);
        }
    }
    if (key.size() == 1) {
        const char ch = static_cast<char>(std::toupper(static_cast<unsigned char>(key[0])));
        if (ch >= 'A' && ch <= 'Z') {
            return static_cast<UINT>(ch);
        }
        if (ch >= '0' && ch <= '9') {
            return static_cast<UINT>(ch);
        }
    }
    return 0;
}

[[nodiscard]] std::wstring vk_to_name(UINT vk) {
    if (vk == VK_PAUSE || vk == VK_CANCEL) {
        return L"Pause";
    }
    if (vk == VK_SCROLL) {
        return L"Scroll Lock";
    }
    if (vk == VK_INSERT) {
        return L"Insert";
    }
    if (vk == VK_DELETE) {
        return L"Delete";
    }
    if (vk == VK_HOME) {
        return L"Home";
    }
    if (vk == VK_END) {
        return L"End";
    }
    if (vk == VK_SPACE) {
        return L"Space";
    }
    if (vk == VK_BACK) {
        return L"Backspace";
    }
    if (vk == VK_TAB) {
        return L"Tab";
    }
    if (vk == VK_CAPITAL) {
        return L"Caps Lock";
    }
    if (vk == VK_PRIOR) {
        return L"Page Up";
    }
    if (vk == VK_NEXT) {
        return L"Page Down";
    }
    if (vk == VK_SNAPSHOT) {
        return L"Print Screen";
    }
    if (vk >= VK_F1 && vk <= VK_F24) {
        return L"F" + std::to_wstring(vk - VK_F1 + 1);
    }
    if (vk >= 'A' && vk <= 'Z') {
        return std::wstring(1, static_cast<wchar_t>(vk));
    }
    if (vk >= '0' && vk <= '9') {
        return std::wstring(1, static_cast<wchar_t>(vk));
    }

    const UINT scan = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
    wchar_t name[64]{};
    if (GetKeyNameTextW(static_cast<LONG>(scan << 16), name, 64) > 0) {
        return name;
    }
    return L"Key" + std::to_wstring(vk);
}

}  // namespace

bool Hotkey::matches(UINT event_vk, bool event_ctrl, bool event_alt, bool event_shift, bool event_win) const noexcept {
    if (vk == 0) {
        return false;
    }
    const bool vk_ok = event_vk == vk || (vk == VK_PAUSE && event_vk == VK_CANCEL);
    return vk_ok && event_ctrl == ctrl && event_alt == alt && event_shift == shift && event_win == win;
}

UINT Hotkey::pack() const noexcept {
    UINT packed = vk & 0xFFFFu;
    if (ctrl) {
        packed |= 1u << 16;
    }
    if (alt) {
        packed |= 1u << 17;
    }
    if (shift) {
        packed |= 1u << 18;
    }
    if (win) {
        packed |= 1u << 19;
    }
    return packed;
}

Hotkey Hotkey::unpack(UINT packed) noexcept {
    Hotkey key{};
    key.vk = packed & 0xFFFFu;
    key.ctrl = (packed & (1u << 16)) != 0;
    key.alt = (packed & (1u << 17)) != 0;
    key.shift = (packed & (1u << 18)) != 0;
    key.win = (packed & (1u << 19)) != 0;
    return key;
}

std::wstring Hotkey::to_wstring() const {
    if (vk == 0) {
        return L"нет";
    }

    std::wstring text;
    if (ctrl) {
        text += L"Ctrl+";
    }
    if (alt) {
        text += L"Alt+";
    }
    if (shift) {
        text += L"Shift+";
    }
    if (win) {
        text += L"Win+";
    }
    text += vk_to_name(vk);
    return text;
}

std::string Hotkey::to_string() const {
    const std::wstring wide = to_wstring();
    std::string text;
    text.reserve(wide.size());
    for (const wchar_t ch : wide) {
        if (ch < 128) {
            text.push_back(static_cast<char>(ch));
        }
    }
    return text;
}

std::wstring Hotkey::key_name(UINT vk) {
    return vk_to_name(vk);
}

Hotkey Hotkey::parse(std::string_view text) {
    Hotkey key{};
    std::string current;
    for (std::size_t i = 0; i <= text.size(); ++i) {
        const char ch = (i < text.size()) ? text[i] : '+';
        if (ch == '+' || ch == ' ' || i == text.size()) {
            if (!current.empty()) {
                const std::string token = to_lower(current);
                if (token == "ctrl" || token == "control") {
                    key.ctrl = true;
                } else if (token == "alt") {
                    key.alt = true;
                } else if (token == "shift") {
                    key.shift = true;
                } else if (token == "win" || token == "super") {
                    key.win = true;
                } else {
                    key.vk = vk_from_name(token);
                }
                current.clear();
            }
        } else {
            current.push_back(ch);
        }
    }
    return key;
}
