#include "input_sim.h"

#include <Windows.h>

#include <cstring>
#include <optional>
#include <string>
#include <vector>

namespace {

constexpr wchar_t kEnglishLayoutId[] = L"00000409";
constexpr wchar_t kRussianLayoutId[] = L"00000419";

void append_key(std::vector<INPUT>& inputs, WORD vk) {
    INPUT down{};
    down.type = INPUT_KEYBOARD;
    down.ki.wVk = vk;
    inputs.push_back(down);

    INPUT up = down;
    up.ki.dwFlags = KEYEVENTF_KEYUP;
    inputs.push_back(up);
}

void append_unicode(std::vector<INPUT>& inputs, wchar_t ch) {
    INPUT down{};
    down.type = INPUT_KEYBOARD;
    down.ki.wVk = 0;
    down.ki.wScan = static_cast<WORD>(ch);
    down.ki.dwFlags = KEYEVENTF_UNICODE;
    inputs.push_back(down);

    INPUT up = down;
    up.ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
    inputs.push_back(up);
}

[[nodiscard]] HKL load_layout(Translator::Layout layout) {
    const wchar_t* id = (layout == Translator::Layout::Ru) ? kRussianLayoutId : kEnglishLayoutId;
    return LoadKeyboardLayoutW(id, KLF_NOTELLSHELL);
}

bool send_chord(WORD modifier, WORD key) {
    INPUT inputs[4]{};
    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wVk = modifier;
    inputs[1].type = INPUT_KEYBOARD;
    inputs[1].ki.wVk = key;
    inputs[2] = inputs[1];
    inputs[2].ki.dwFlags = KEYEVENTF_KEYUP;
    inputs[3] = inputs[0];
    inputs[3].ki.dwFlags = KEYEVENTF_KEYUP;
    return SendInput(4, inputs, sizeof(INPUT)) == 4;
}

class UniqueClipboard {
public:
    UniqueClipboard() {
        for (int attempt = 0; attempt < 8; ++attempt) {
            if (OpenClipboard(nullptr) != FALSE) {
                open_ = true;
                return;
            }
            Sleep(10);
        }
    }

    ~UniqueClipboard() {
        if (open_) {
            CloseClipboard();
        }
    }

    UniqueClipboard(const UniqueClipboard&) = delete;
    UniqueClipboard& operator=(const UniqueClipboard&) = delete;

    [[nodiscard]] explicit operator bool() const noexcept { return open_; }

private:
    bool open_ = false;
};

[[nodiscard]] std::optional<std::wstring> clipboard_text() {
    UniqueClipboard clipboard;
    if (!clipboard) {
        return std::nullopt;
    }

    HANDLE data = GetClipboardData(CF_UNICODETEXT);
    if (data == nullptr) {
        return std::nullopt;
    }

    const auto* text = static_cast<const wchar_t*>(GlobalLock(data));
    if (text == nullptr) {
        return std::nullopt;
    }

    std::wstring copy = text;
    GlobalUnlock(data);
    return copy;
}

bool set_clipboard_text(std::wstring_view text) {
    UniqueClipboard clipboard;
    if (!clipboard) {
        return false;
    }

    if (!EmptyClipboard()) {
        return false;
    }

    const std::size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (memory == nullptr) {
        return false;
    }

    auto* dest = static_cast<wchar_t*>(GlobalLock(memory));
    if (dest == nullptr) {
        GlobalFree(memory);
        return false;
    }

    std::memcpy(dest, text.data(), text.size() * sizeof(wchar_t));
    dest[text.size()] = L'\0';
    GlobalUnlock(memory);

    if (SetClipboardData(CF_UNICODETEXT, memory) == nullptr) {
        GlobalFree(memory);
        return false;
    }
    return true;
}

}  // namespace

bool InputSimulator::replace_text(std::size_t delete_count, std::wstring_view text) {
    return replace_text(delete_count, text, 0);
}

bool InputSimulator::replace_text(std::size_t delete_count, std::wstring_view text, UINT trailing_vk) {
    std::vector<INPUT> inputs;
    inputs.reserve((delete_count + text.size() + (trailing_vk != 0 ? 1 : 0)) * 2);

    for (std::size_t i = 0; i < delete_count; ++i) {
        append_key(inputs, VK_BACK);
    }
    for (const wchar_t ch : text) {
        append_unicode(inputs, ch);
    }
    if (trailing_vk != 0) {
        append_key(inputs, static_cast<WORD>(trailing_vk));
    }

    if (inputs.empty()) {
        return true;
    }

    const UINT sent = SendInput(static_cast<UINT>(inputs.size()), inputs.data(), sizeof(INPUT));
    return sent == inputs.size();
}

bool InputSimulator::convert_selection() {
    const auto backup = clipboard_text();

    if (!send_chord(VK_CONTROL, 'C')) {
        return false;
    }
    Sleep(40);

    const auto selected = clipboard_text();
    if (!selected.has_value() || selected->empty() || selected->size() > 8192) {
        if (backup.has_value()) {
            set_clipboard_text(*backup);
        }
        return false;
    }

    const std::wstring converted = Translator::convert(*selected);
    if (converted == *selected) {
        if (backup.has_value()) {
            set_clipboard_text(*backup);
        }
        return false;
    }

    if (!set_clipboard_text(converted)) {
        return false;
    }
    if (!send_chord(VK_CONTROL, 'V')) {
        return false;
    }
    Sleep(40);

    if (backup.has_value()) {
        set_clipboard_text(*backup);
    }

    activate_layout(Translator::opposite(Translator::infer_layout(*selected)));
    return true;
}

bool InputSimulator::activate_layout(Translator::Layout layout) {
    if (layout == Translator::Layout::Other) {
        return false;
    }

    const HKL hkl = load_layout(layout);
    if (hkl == nullptr) {
        return false;
    }

    HWND foreground = GetForegroundWindow();
    if (foreground == nullptr) {
        return ActivateKeyboardLayout(hkl, 0) != nullptr;
    }

    return PostMessageW(
               foreground,
               WM_INPUTLANGCHANGEREQUEST,
               0,
               reinterpret_cast<LPARAM>(hkl)) != FALSE;
}
