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

[[nodiscard]] bool send_key(WORD vk, DWORD extra_flags = 0);
[[nodiscard]] wchar_t terminator_char(UINT trailing_vk) noexcept;

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
        VK_PAUSE,
        VK_CANCEL,
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

[[nodiscard]] std::wstring class_name_lowered(HWND window) {
    if (window == nullptr) {
        return {};
    }
    wchar_t class_name[256]{};
    if (GetClassNameW(window, class_name, 256) == 0) {
        return {};
    }
    std::wstring lowered = class_name;
    for (wchar_t& ch : lowered) {
        if (ch >= L'A' && ch <= L'Z') {
            ch = static_cast<wchar_t>(ch - L'A' + L'a');
        }
    }
    return lowered;
}

[[nodiscard]] int text_control_score(const std::wstring& lowered) noexcept {
    if (lowered.find(L"richeditd2d") != std::wstring::npos) {
        return 100;
    }
    if (lowered == L"edit") {
        return 90;
    }
    if (lowered.find(L"richedit") != std::wstring::npos) {
        return 80;
    }
    if (lowered.find(L"scintilla") != std::wstring::npos) {
        return 70;
    }
    if (lowered == L"notepadtextbox") {
        return 40;
    }
    return 0;
}

struct FindTextControlState {
    HWND best = nullptr;
    int score = 0;
};

BOOL CALLBACK find_text_control_proc(HWND hwnd, LPARAM lparam) {
    auto* state = reinterpret_cast<FindTextControlState*>(lparam);
    const int score = text_control_score(class_name_lowered(hwnd));
    if (score > state->score) {
        state->best = hwnd;
        state->score = score;
    }
    return TRUE;
}

[[nodiscard]] HWND find_text_control(HWND root) {
    if (root == nullptr) {
        return nullptr;
    }

    HWND focus = focused_input_window(root);
    const int focus_score = text_control_score(class_name_lowered(focus));
    const int root_score = text_control_score(class_name_lowered(root));
    if (focus_score >= 80) {
        return focus;
    }
    if (root_score >= 80) {
        return root;
    }

    FindTextControlState state{};
    if (focus_score > 0) {
        state.best = focus;
        state.score = focus_score;
    } else if (root_score > 0) {
        state.best = root;
        state.score = root_score;
    }
    EnumChildWindows(root, find_text_control_proc, reinterpret_cast<LPARAM>(&state));
    if (focus != nullptr && focus != root) {
        EnumChildWindows(focus, find_text_control_proc, reinterpret_cast<LPARAM>(&state));
    }
    return state.best != nullptr ? state.best : focus;
}

[[nodiscard]] bool timed_message(HWND window, UINT message, WPARAM wparam, LPARAM lparam, DWORD timeout_ms = 200) {
    DWORD_PTR ignored = 0;
    return SendMessageTimeoutW(
               window,
               message,
               wparam,
               lparam,
               SMTO_ABORTIFHUNG | SMTO_BLOCK,
               timeout_ms,
               &ignored) != 0;
}

[[nodiscard]] std::size_t edit_text_length(HWND edit) {
    DWORD_PTR length = 0;
    if (SendMessageTimeoutW(
            edit,
            WM_GETTEXTLENGTH,
            0,
            0,
            SMTO_ABORTIFHUNG | SMTO_BLOCK,
            200,
            &length) == 0) {
        return 0;
    }
    return static_cast<std::size_t>(length);
}

[[nodiscard]] bool replace_via_edit_messages(
    HWND target,
    std::size_t delete_count,
    std::wstring_view text,
    UINT trailing_vk) {
    HWND edit = find_text_control(target);
    if (edit == nullptr || text_control_score(class_name_lowered(edit)) == 0) {
        return false;
    }

    std::wstring paste(text);
    const wchar_t extra = terminator_char(trailing_vk);
    if (extra == L' ' || extra == L'\t') {
        paste.push_back(extra);
    }

    ClipboardTextRestore restore(clipboard_text());
    if (!set_clipboard_text(paste)) {
        return false;
    }

    const std::size_t length = edit_text_length(edit);
    DWORD_PTR sel = 0;
    const bool got_sel = SendMessageTimeoutW(
                             edit,
                             EM_GETSEL,
                             0,
                             0,
                             SMTO_ABORTIFHUNG | SMTO_BLOCK,
                             200,
                             &sel) != 0;
    const std::size_t caret = (got_sel && HIWORD(sel) != 0)
                                 ? static_cast<std::size_t>(HIWORD(sel))
                                 : length;
    if (delete_count != 0 && caret == 0 && length == 0) {
        return false;
    }
    const std::size_t actual_delete = std::min(delete_count, caret);
    const std::size_t start = caret - actual_delete;
    if (!timed_message(edit, EM_SETSEL, start, static_cast<LPARAM>(caret))) {
        return false;
    }
    if (!timed_message(edit, WM_PASTE, 0, 0, 400)) {
        return false;
    }
    Sleep(150);

    if (extra == L'\n') {
        if (!send_key(VK_RETURN)) {
            return false;
        }
    }
    return GetForegroundWindow() == target;
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

[[nodiscard]] bool send_key(WORD vk, DWORD extra_flags) {
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

[[nodiscard]] bool send_left_arrow() {
    INPUT inputs[2]{};
    const WORD scan = static_cast<WORD>(MapVirtualKeyW(VK_LEFT, MAPVK_VK_TO_VSC));
    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wVk = VK_LEFT;
    inputs[0].ki.wScan = scan;
    inputs[0].ki.dwFlags = KEYEVENTF_EXTENDEDKEY;
    inputs[1] = inputs[0];
    inputs[1].ki.dwFlags = KEYEVENTF_EXTENDEDKEY | KEYEVENTF_KEYUP;
    return SendInput(2, inputs, sizeof(INPUT)) == 2;
}

[[nodiscard]] bool select_previous_characters(std::size_t count, bool async_input) {
    if (count == 0) {
        return true;
    }
    if (!send_modifier(VK_SHIFT, true)) {
        return false;
    }

    bool ok = true;
    if (async_input) {
        Sleep(20);
    }

    for (std::size_t i = 0; i < count && ok; ++i) {
        ok = send_left_arrow();
        if (async_input) {
            Sleep(20);
        }
    }

    const bool released = send_modifier(VK_SHIFT, false);
    if (!released) {
        static_cast<void>(send_modifier(VK_LSHIFT, false));
        static_cast<void>(send_modifier(VK_RSHIFT, false));
    }
    return ok && released;
}

[[nodiscard]] bool replace_via_clipboard(HWND target, std::size_t delete_count, std::wstring_view text, UINT trailing_vk) {
    std::wstring paste(text);
    const wchar_t extra = terminator_char(trailing_vk);
    if (extra == L' ' || extra == L'\t') {
        paste.push_back(extra);
    }

    ClipboardTextRestore restore(clipboard_text());
    if (!set_clipboard_text(paste)) {
        return false;
    }
    if (!select_previous_characters(delete_count, true)) {
        return false;
    }
    Sleep(40);
    if (GetForegroundWindow() != target) {
        return false;
    }
    if (!send_chord(VK_CONTROL, 'V')) {
        release_held_modifiers();
        return false;
    }
    static_cast<void>(wait_for_target_input(focused_input_window(target), kClipboardTimeoutMs));
    Sleep(150);
    release_held_modifiers();

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

    if (replace_via_edit_messages(target, delete_count, text, trailing_vk)) {
        return true;
    }

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

    release_held_modifiers();
    Sleep(30);

    ClipboardTextRestore restore(clipboard_text());
    HWND edit = find_text_control(target);
    const HWND copy_target = (edit != nullptr) ? edit : focused_input_window(target);

    std::optional<std::wstring> selected;
    for (int attempt = 0; attempt < 3; ++attempt) {
        const DWORD sequence = GetClipboardSequenceNumber();
        bool copied = false;
        if (copy_target != nullptr) {
            DWORD_PTR ignored = 0;
            copied = SendMessageTimeoutW(
                         copy_target,
                         WM_COPY,
                         0,
                         0,
                         SMTO_ABORTIFHUNG | SMTO_BLOCK,
                         200,
                         &ignored) != 0 &&
                     GetClipboardSequenceNumber() != sequence;
        }
        if (!copied) {
            if (!send_chord(VK_CONTROL, 'C')) {
                return false;
            }
            copied = wait_for_clipboard_change(sequence, target);
        }
        if (copied) {
            Sleep(20);
        }
        selected = clipboard_text();
        if (selected.has_value() && !selected->empty() && selected->size() <= 8192) {
            break;
        }
        Sleep(50);
    }

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

    bool pasted = false;
    if (edit != nullptr && text_control_score(class_name_lowered(edit)) != 0) {
        pasted = timed_message(edit, WM_PASTE, 0, 0, 400);
    }
    if (!pasted) {
        if (!send_chord(VK_CONTROL, 'V')) {
            release_held_modifiers();
            return false;
        }
    }
    if (!wait_for_target_input(focused_input_window(target), kClipboardTimeoutMs)) {
        release_held_modifiers();
        return false;
    }
    Sleep(150);
    release_held_modifiers();

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
        return false;
    }

    HWND focus = focused_input_window(foreground);
    if (focus == nullptr) {
        focus = foreground;
    }

    const auto request = [&](HWND hwnd) {
        if (hwnd == nullptr || !IsWindow(hwnd)) {
            return;
        }
        PostMessageW(
            hwnd,
            WM_INPUTLANGCHANGEREQUEST,
            INPUTLANGCHANGE_SYSCHARSET,
            reinterpret_cast<LPARAM>(hkl));
    };
    request(focus);
    if (foreground != focus) {
        request(foreground);
    }
    return true;
}

bool InputSimulator::send_virtual_key(UINT vk) {
    if (vk == 0) {
        return false;
    }
    release_held_modifiers();
    return send_key(static_cast<WORD>(vk));
}

void InputSimulator::restore_system_layouts() {
    static_cast<void>(load_layout(Translator::Layout::En));
    static_cast<void>(load_layout(Translator::Layout::Ru));
}
