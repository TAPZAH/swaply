#include "input_sim.h"

#include "focus_guard.h"

#include <Windows.h>

#include <algorithm>
#include <cstring>
#include <iterator>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr wchar_t kEnglishLayoutId[] = L"00000409";
constexpr wchar_t kRussianLayoutId[] = L"00000419";
constexpr std::size_t kMaxClipboardCharacters = 1024 * 1024;
constexpr DWORD kClipboardTimeoutMs = 750;
constexpr DWORD kReplaceFlushTimeoutMs = 50;

HWND g_owner_window = nullptr;

void append_key(std::vector<INPUT>& inputs, WORD vk, DWORD extra_flags = 0) {
    INPUT down{};
    down.type = INPUT_KEYBOARD;
    down.ki.wVk = vk;
    down.ki.wScan = static_cast<WORD>(MapVirtualKeyW(vk, MAPVK_VK_TO_VSC));
    down.ki.dwFlags = extra_flags;
    inputs.push_back(down);

    INPUT up = down;
    up.ki.dwFlags = extra_flags | KEYEVENTF_KEYUP;
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

[[nodiscard]] bool send_inputs(std::vector<INPUT>& inputs) {
    if (inputs.empty()) {
        return true;
    }
    return SendInput(static_cast<UINT>(inputs.size()), inputs.data(), sizeof(INPUT)) == inputs.size();
}

[[nodiscard]] HKL load_layout(Translator::Layout layout) {
    const LANGID language = (layout == Translator::Layout::Ru)
                                ? MAKELANGID(LANG_RUSSIAN, SUBLANG_DEFAULT)
                                : MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US);

    HKL installed[64]{};
    const int count = GetKeyboardLayoutList(static_cast<int>(std::size(installed)), installed);
    for (int i = 0; i < count; ++i) {
        if (PRIMARYLANGID(LOWORD(reinterpret_cast<ULONG_PTR>(installed[i]))) == PRIMARYLANGID(language)) {
            return installed[i];
        }
    }

    const wchar_t* id = (layout == Translator::Layout::Ru) ? kRussianLayoutId : kEnglishLayoutId;
    return LoadKeyboardLayoutW(id, KLF_NOTELLSHELL);
}

void release_held_modifiers() {
    constexpr WORD kModifiers[] = {
        VK_LCONTROL,
        VK_RCONTROL,
        VK_CONTROL,
        VK_LMENU,
        VK_RMENU,
        VK_MENU,
        VK_LSHIFT,
        VK_RSHIFT,
        VK_SHIFT,
        VK_LWIN,
        VK_RWIN,
    };

    std::vector<INPUT> ups;
    ups.reserve(std::size(kModifiers));
    for (const WORD vk : kModifiers) {
        if (GetAsyncKeyState(vk) >= 0) {
            continue;
        }
        INPUT up{};
        up.type = INPUT_KEYBOARD;
        up.ki.wVk = vk;
        up.ki.wScan = static_cast<WORD>(MapVirtualKeyW(vk, MAPVK_VK_TO_VSC));
        up.ki.dwFlags = KEYEVENTF_KEYUP;
        ups.push_back(up);
    }
    static_cast<void>(send_inputs(ups));
}

bool send_chord(WORD modifier, WORD key) {
    release_held_modifiers();

    std::vector<INPUT> inputs;
    inputs.reserve(4);
    INPUT down{};
    down.type = INPUT_KEYBOARD;
    down.ki.wVk = modifier;
    down.ki.wScan = static_cast<WORD>(MapVirtualKeyW(modifier, MAPVK_VK_TO_VSC));
    inputs.push_back(down);

    INPUT key_down{};
    key_down.type = INPUT_KEYBOARD;
    key_down.ki.wVk = key;
    key_down.ki.wScan = static_cast<WORD>(MapVirtualKeyW(key, MAPVK_VK_TO_VSC));
    inputs.push_back(key_down);

    INPUT key_up = key_down;
    key_up.ki.dwFlags = KEYEVENTF_KEYUP;
    inputs.push_back(key_up);

    INPUT up = down;
    up.ki.dwFlags = KEYEVENTF_KEYUP;
    inputs.push_back(up);
    return send_inputs(inputs);
}

class UniqueClipboard {
public:
    UniqueClipboard() {
        for (int attempt = 0; attempt < 8; ++attempt) {
            if (OpenClipboard(g_owner_window) != FALSE) {
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

class LockedGlobal {
public:
    explicit LockedGlobal(HANDLE handle) : handle_(handle) {
        if (handle_ != nullptr) {
            pointer_ = GlobalLock(handle_);
        }
    }

    ~LockedGlobal() {
        if (pointer_ != nullptr) {
            GlobalUnlock(handle_);
        }
    }

    LockedGlobal(const LockedGlobal&) = delete;
    LockedGlobal& operator=(const LockedGlobal&) = delete;

    [[nodiscard]] void* get() const noexcept { return pointer_; }

private:
    HANDLE handle_ = nullptr;
    void* pointer_ = nullptr;
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

    const SIZE_T bytes = GlobalSize(data);
    if (bytes < sizeof(wchar_t) || bytes % sizeof(wchar_t) != 0) {
        return std::nullopt;
    }
    const std::size_t character_count =
        std::min<std::size_t>(bytes / sizeof(wchar_t), kMaxClipboardCharacters + 1);

    LockedGlobal lock(data);
    const auto* text = static_cast<const wchar_t*>(lock.get());
    if (text == nullptr) {
        return std::nullopt;
    }

    const auto* terminator = std::find(text, text + character_count, L'\0');
    if (terminator == text + character_count ||
        static_cast<std::size_t>(terminator - text) > kMaxClipboardCharacters) {
        return std::nullopt;
    }
    return std::wstring(text, terminator);
}

bool set_clipboard_text(std::wstring_view text) {
    if (text.size() > (std::numeric_limits<std::size_t>::max() / sizeof(wchar_t)) - 1) {
        return false;
    }

    const std::size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (memory == nullptr) {
        return false;
    }

    {
        LockedGlobal lock(memory);
        auto* dest = static_cast<wchar_t*>(lock.get());
        if (dest == nullptr) {
            GlobalFree(memory);
            return false;
        }
        std::memcpy(dest, text.data(), text.size() * sizeof(wchar_t));
        dest[text.size()] = L'\0';
    }

    UniqueClipboard clipboard;
    if (!clipboard || !EmptyClipboard()) {
        GlobalFree(memory);
        return false;
    }

    if (SetClipboardData(CF_UNICODETEXT, memory) == nullptr) {
        GlobalFree(memory);
        return false;
    }
    return true;
}

class ClipboardTextRestore {
public:
    explicit ClipboardTextRestore(std::optional<std::wstring> backup)
        : backup_(std::move(backup)) {}

    ~ClipboardTextRestore() {
        if (backup_.has_value()) {
            set_clipboard_text(*backup_);
        }
    }

    ClipboardTextRestore(const ClipboardTextRestore&) = delete;
    ClipboardTextRestore& operator=(const ClipboardTextRestore&) = delete;

private:
    std::optional<std::wstring> backup_;
};

[[nodiscard]] HWND focused_input_window(HWND target) {
    if (target == nullptr) {
        return nullptr;
    }

    DWORD thread_id = GetWindowThreadProcessId(target, nullptr);
    GUITHREADINFO info{};
    info.cbSize = sizeof(info);
    if (GetGUIThreadInfo(thread_id, &info) && info.hwndFocus != nullptr) {
        return info.hwndFocus;
    }
    return target;
}

[[nodiscard]] bool wait_for_target_input(HWND target, DWORD timeout_ms) {
    if (target == nullptr) {
        return false;
    }

    DWORD_PTR ignored = 0;
    return SendMessageTimeoutW(
               target,
               WM_NULL,
               0,
               0,
               SMTO_ABORTIFHUNG | SMTO_BLOCK,
               timeout_ms,
               &ignored) != 0;
}

[[nodiscard]] bool wait_for_clipboard_change(DWORD previous_sequence, HWND target) {
    const ULONGLONG deadline = GetTickCount64() + kClipboardTimeoutMs;
    do {
        if (GetForegroundWindow() != target) {
            return false;
        }
        if (GetClipboardSequenceNumber() != previous_sequence) {
            return true;
        }
        Sleep(10);
    } while (GetTickCount64() < deadline);
    return false;
}

[[nodiscard]] bool prepare_target(HWND target, bool async_input) {
    if (target == nullptr || target != GetForegroundWindow()) {
        return false;
    }

    static_cast<void>(wait_for_target_input(focused_input_window(target), kReplaceFlushTimeoutMs));
    Sleep(async_input ? 80 : 15);
    return target == GetForegroundWindow();
}

[[nodiscard]] wchar_t terminator_char(UINT trailing_vk) noexcept {
    switch (trailing_vk) {
    case VK_SPACE:
        return L' ';
    case VK_RETURN:
        return L'\n';
    case VK_TAB:
        return L'\t';
    default:
        return 0;
    }
}

[[nodiscard]] bool send_key(WORD vk, DWORD extra_flags = 0) {
    std::vector<INPUT> inputs;
    append_key(inputs, vk, extra_flags);
    return send_inputs(inputs);
}

[[nodiscard]] bool send_modifier(WORD vk, bool down) {
    INPUT input{};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = vk;
    input.ki.wScan = static_cast<WORD>(MapVirtualKeyW(vk, MAPVK_VK_TO_VSC));
    input.ki.dwFlags = down ? 0 : KEYEVENTF_KEYUP;
    return SendInput(1, &input, sizeof(INPUT)) == 1;
}

[[nodiscard]] bool select_previous_characters(std::size_t count, bool async_input) {
    if (count == 0) {
        return true;
    }
    if (!send_modifier(VK_SHIFT, true)) {
        return false;
    }

    bool ok = true;
    for (std::size_t i = 0; i < count && ok; ++i) {
        ok = send_key(VK_LEFT, KEYEVENTF_EXTENDEDKEY);
        if (async_input) {
            Sleep(8);
        }
    }

    if (!send_modifier(VK_SHIFT, false)) {
        return false;
    }
    return ok;
}

[[nodiscard]] bool select_previous_word() {
    if (!send_modifier(VK_CONTROL, true) || !send_modifier(VK_SHIFT, true)) {
        static_cast<void>(send_modifier(VK_SHIFT, false));
        static_cast<void>(send_modifier(VK_CONTROL, false));
        return false;
    }
    const bool ok = send_key(VK_LEFT, KEYEVENTF_EXTENDEDKEY);
    Sleep(15);
    const bool released_shift = send_modifier(VK_SHIFT, false);
    const bool released_ctrl = send_modifier(VK_CONTROL, false);
    return ok && released_shift && released_ctrl;
}

[[nodiscard]] bool replace_via_clipboard(HWND target, std::size_t delete_count, std::wstring_view text, UINT trailing_vk) {
    std::wstring paste(text);
    const wchar_t extra = terminator_char(trailing_vk);
    const bool delimiter_already_typed = extra != 0;
    if (extra == L' ' || extra == L'\t') {
        paste.push_back(extra);
    }

    if (delimiter_already_typed) {
        if (!send_key(VK_BACK)) {
            return false;
        }
        Sleep(20);
    }

    ClipboardTextRestore restore(clipboard_text());
    if (!set_clipboard_text(paste)) {
        return false;
    }
    const std::size_t letter_count =
        (delimiter_already_typed && delete_count > 0) ? delete_count - 1 : delete_count;
    if (!select_previous_word() && !select_previous_characters(letter_count, true)) {
        return false;
    }
    Sleep(25);
    if (GetForegroundWindow() != target) {
        return false;
    }
    if (!send_chord(VK_CONTROL, 'V')) {
        return false;
    }
    static_cast<void>(wait_for_target_input(focused_input_window(target), kClipboardTimeoutMs));
    Sleep(80);

    if (extra == L'\n') {
        if (!send_key(VK_RETURN)) {
            return false;
        }
    }
    return GetForegroundWindow() == target;
}

}  // namespace

void InputSimulator::set_owner_window(HWND window) noexcept {
    g_owner_window = window;
}

bool InputSimulator::replace_text(std::size_t delete_count, std::wstring_view text) {
    return replace_text(GetForegroundWindow(), delete_count, text, 0);
}

bool InputSimulator::replace_text(std::size_t delete_count, std::wstring_view text, UINT trailing_vk) {
    return replace_text(GetForegroundWindow(), delete_count, text, trailing_vk);
}

bool InputSimulator::replace_text(
    HWND target,
    std::size_t delete_count,
    std::wstring_view text,
    UINT trailing_vk) {
    const bool async_input = FocusGuard::uses_async_input(target);
    if (!prepare_target(target, async_input)) {
        return false;
    }

    release_held_modifiers();

    if (async_input) {
        return replace_via_clipboard(target, delete_count, text, trailing_vk);
    }

    for (std::size_t i = 0; i < delete_count; ++i) {
        if (!send_key(VK_BACK)) {
            return false;
        }
    }

    if (delete_count != 0) {
        static_cast<void>(wait_for_target_input(focused_input_window(target), kReplaceFlushTimeoutMs));
        if (target != GetForegroundWindow()) {
            return false;
        }
    }

    for (const wchar_t ch : text) {
        std::vector<INPUT> glyph;
        append_unicode(glyph, ch);
        if (!send_inputs(glyph)) {
            return false;
        }
    }

    if (trailing_vk != 0) {
        if (!send_key(static_cast<WORD>(trailing_vk))) {
            return false;
        }
    }

    return target == GetForegroundWindow();
}

bool InputSimulator::convert_selection() {
    const HWND target = GetForegroundWindow();
    if (target == nullptr) {
        return false;
    }

    ClipboardTextRestore restore(clipboard_text());
    const DWORD sequence = GetClipboardSequenceNumber();

    if (!send_chord(VK_CONTROL, 'C')) {
        return false;
    }
    if (!wait_for_clipboard_change(sequence, target)) {
        return false;
    }

    const auto selected = clipboard_text();
    if (!selected.has_value() || selected->empty() || selected->size() > 8192) {
        return false;
    }

    const std::wstring converted = Translator::convert(*selected);
    if (converted == *selected) {
        return false;
    }

    if (GetForegroundWindow() != target) {
        return false;
    }
    if (!set_clipboard_text(converted)) {
        return false;
    }
    if (!send_chord(VK_CONTROL, 'V')) {
        return false;
    }
    if (!wait_for_target_input(target, kClipboardTimeoutMs)) {
        return false;
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
