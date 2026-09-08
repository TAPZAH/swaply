#include "input_sim.h"

#include <Windows.h>
#include <objbase.h>
#include <ole2.h>
#include <UIAutomation.h>

#include <array>
#include <optional>
#include <string>
#include <vector>

namespace {

constexpr wchar_t kEnglishLayoutId[] = L"00000409";
constexpr wchar_t kRussianLayoutId[] = L"00000419";
constexpr DWORD kSyncTimeoutMs = 80;
constexpr std::size_t kMaxSelectionCharacters = 8192;

class ThreadInputAttach {
public:
    explicit ThreadInputAttach(DWORD thread_id) {
        const DWORD self = GetCurrentThreadId();
        if (thread_id != 0 && thread_id != self) {
            attached_ = AttachThreadInput(self, thread_id, TRUE) != FALSE;
            thread_id_ = thread_id;
        }
    }

    ~ThreadInputAttach() {
        if (attached_) {
            AttachThreadInput(GetCurrentThreadId(), thread_id_, FALSE);
        }
    }

    ThreadInputAttach(const ThreadInputAttach&) = delete;
    ThreadInputAttach& operator=(const ThreadInputAttach&) = delete;

private:
    DWORD thread_id_ = 0;
    bool attached_ = false;
};

[[nodiscard]] bool is_extended_key(UINT vk) noexcept {
    switch (vk) {
    case VK_INSERT:
    case VK_DELETE:
    case VK_HOME:
    case VK_END:
    case VK_PRIOR:
    case VK_NEXT:
    case VK_LEFT:
    case VK_RIGHT:
    case VK_UP:
    case VK_DOWN:
    case VK_NUMLOCK:
    case VK_DIVIDE:
    case VK_RCONTROL:
    case VK_RMENU:
        return true;
    default:
        return false;
    }
}

void append_key_event(std::vector<INPUT>& inputs, WORD vk, bool down) {
    INPUT input{};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = vk;
    input.ki.wScan = static_cast<WORD>(MapVirtualKeyW(vk, MAPVK_VK_TO_VSC));
    input.ki.dwFlags = (down ? 0 : KEYEVENTF_KEYUP) | (is_extended_key(vk) ? KEYEVENTF_EXTENDEDKEY : 0);
    inputs.push_back(input);
}

void append_key(std::vector<INPUT>& inputs, WORD vk) {
    append_key_event(inputs, vk, true);
    append_key_event(inputs, vk, false);
}

void append_character(std::vector<INPUT>& inputs, const Translator::PhysicalKey& key) {
    if (key.shift) {
        append_key_event(inputs, VK_SHIFT, true);
    }
    append_key(inputs, static_cast<WORD>(key.vk));
    if (key.shift) {
        append_key_event(inputs, VK_SHIFT, false);
    }
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
        append_key_event(ups, vk, false);
    }
    static_cast<void>(send_inputs(ups));
}

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

[[nodiscard]] bool wait_for_target(HWND target) {
    if (target == nullptr) {
        return false;
    }

    DWORD_PTR ignored = 0;
    return SendMessageTimeoutW(
               focused_input_window(target),
               WM_NULL,
               0,
               0,
               SMTO_ABORTIFHUNG | SMTO_BLOCK,
               kSyncTimeoutMs,
               &ignored) != 0;
}

[[nodiscard]] bool append_text_keys(std::vector<INPUT>& inputs, std::wstring_view text, Translator::Layout layout) {
    inputs.reserve(inputs.size() + text.size() * 6);
    for (const wchar_t ch : text) {
        const auto key = Translator::key_from_char(ch, layout);
        if (!key.has_value()) {
            return false;
        }
        append_character(inputs, *key);
    }
    return true;
}

bool request_layout(HWND hwnd, HKL hkl) {
    if (hwnd == nullptr || !IsWindow(hwnd) || hkl == nullptr) {
        return false;
    }

    DWORD_PTR ignored = 0;
    return SendMessageTimeoutW(
               hwnd,
               WM_INPUTLANGCHANGEREQUEST,
               INPUTLANGCHANGE_SYSCHARSET,
               reinterpret_cast<LPARAM>(hkl),
               SMTO_ABORTIFHUNG | SMTO_BLOCK,
               kSyncTimeoutMs,
               &ignored) != 0;
}

[[nodiscard]] bool switch_layout(HWND target, Translator::Layout layout) {
    if (layout == Translator::Layout::Other) {
        return false;
    }

    const HKL hkl = load_layout(layout);
    if (hkl == nullptr || target == nullptr || target != GetForegroundWindow()) {
        return false;
    }

    HWND focus = focused_input_window(target);
    if (focus == nullptr) {
        focus = target;
    }

    DWORD thread_id = 0;
    GetWindowThreadProcessId(focus, &thread_id);
    ThreadInputAttach attach(thread_id);
    ActivateKeyboardLayout(hkl, 0);
    request_layout(focus, hkl);
    if (focus != target) {
        request_layout(target, hkl);
    }
    static_cast<void>(wait_for_target(target));
    return target == GetForegroundWindow();
}

class UniqueAutomation {
public:
    UniqueAutomation() {
        if (FAILED(CoCreateInstance(
                CLSID_CUIAutomation,
                nullptr,
                CLSCTX_INPROC_SERVER,
                IID_PPV_ARGS(&automation_)))) {
            automation_ = nullptr;
        }
    }

    ~UniqueAutomation() {
        if (automation_ != nullptr) {
            automation_->Release();
        }
    }

    UniqueAutomation(const UniqueAutomation&) = delete;
    UniqueAutomation& operator=(const UniqueAutomation&) = delete;

    [[nodiscard]] IUIAutomation* get() const noexcept { return automation_; }

private:
    IUIAutomation* automation_ = nullptr;
};

[[nodiscard]] std::optional<std::wstring> focused_selection_text() {
    UniqueAutomation automation;
    if (automation.get() == nullptr) {
        return std::nullopt;
    }

    IUIAutomationElement* focused = nullptr;
    if (FAILED(automation.get()->GetFocusedElement(&focused)) || focused == nullptr) {
        return std::nullopt;
    }

    IUIAutomationTextPattern* pattern = nullptr;
    const HRESULT pattern_hr = focused->GetCurrentPatternAs(
        UIA_TextPatternId, IID_PPV_ARGS(&pattern));
    focused->Release();
    if (FAILED(pattern_hr) || pattern == nullptr) {
        return std::nullopt;
    }

    IUIAutomationTextRangeArray* ranges = nullptr;
    if (FAILED(pattern->GetSelection(&ranges)) || ranges == nullptr) {
        pattern->Release();
        return std::nullopt;
    }

    int length = 0;
    ranges->get_Length(&length);
    std::wstring text;
    for (int i = 0; i < length; ++i) {
        IUIAutomationTextRange* range = nullptr;
        if (FAILED(ranges->GetElement(i, &range)) || range == nullptr) {
            continue;
        }
        BSTR chunk = nullptr;
        if (SUCCEEDED(range->GetText(-1, &chunk)) && chunk != nullptr) {
            text.append(chunk, SysStringLen(chunk));
            SysFreeString(chunk);
        }
        range->Release();
    }

    ranges->Release();
    pattern->Release();
    if (text.empty() || text.size() > kMaxSelectionCharacters) {
        return std::nullopt;
    }
    return text;
}

}  // namespace

bool InputSimulator::replace_text(
    HWND target,
    std::size_t delete_count,
    std::wstring_view text,
    UINT trailing_vk,
    Translator::Layout type_layout) {
    if (target == nullptr || target != GetForegroundWindow()) {
        return false;
    }

    if (type_layout == Translator::Layout::Other) {
        type_layout = Translator::infer_layout(text);
    }

    release_held_modifiers();
    if (!switch_layout(target, type_layout)) {
        return false;
    }

    std::vector<INPUT> inputs;
    inputs.reserve(delete_count * 2 + text.size() * 6 + 2);
    for (std::size_t i = 0; i < delete_count; ++i) {
        append_key(inputs, VK_BACK);
    }
    if (!append_text_keys(inputs, text, type_layout)) {
        return false;
    }
    if (trailing_vk != 0) {
        append_key(inputs, static_cast<WORD>(trailing_vk));
    }

    if (!send_inputs(inputs)) {
        return false;
    }
    static_cast<void>(wait_for_target(target));
    return target == GetForegroundWindow();
}

bool InputSimulator::convert_selection() {
    const HWND target = GetForegroundWindow();
    if (target == nullptr) {
        return false;
    }

    const auto selected = focused_selection_text();
    if (!selected.has_value()) {
        return false;
    }

    const std::wstring converted = Translator::convert(*selected);
    if (converted.empty() || converted == *selected) {
        return false;
    }

    const auto source = Translator::infer_layout(*selected);
    const auto target_layout = Translator::opposite(source);

    release_held_modifiers();
    if (!switch_layout(target, target_layout)) {
        return false;
    }

    std::vector<INPUT> inputs;
    append_key(inputs, VK_DELETE);
    if (!append_text_keys(inputs, converted, target_layout)) {
        return false;
    }
    if (!send_inputs(inputs)) {
        return false;
    }
    static_cast<void>(wait_for_target(target));
    return target == GetForegroundWindow();
}

bool InputSimulator::activate_layout(Translator::Layout layout) {
    return switch_layout(GetForegroundWindow(), layout);
}

bool InputSimulator::send_virtual_key(UINT vk) {
    if (vk == 0) {
        return false;
    }
    release_held_modifiers();
    std::vector<INPUT> inputs;
    append_key(inputs, static_cast<WORD>(vk));
    return send_inputs(inputs);
}

void InputSimulator::restore_system_layouts() {
    static_cast<void>(load_layout(Translator::Layout::En));
    static_cast<void>(load_layout(Translator::Layout::Ru));
}
