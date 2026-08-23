#include "focus_guard.h"

#include <algorithm>
#include <string>

namespace {

[[nodiscard]] std::wstring to_lower(std::wstring text) {
    std::transform(text.begin(), text.end(), text.begin(), [](wchar_t ch) {
        if (ch >= L'A' && ch <= L'Z') {
            return static_cast<wchar_t>(ch - L'A' + L'a');
        }
        return ch;
    });
    return text;
}

[[nodiscard]] bool is_password_field() {
    HWND foreground = GetForegroundWindow();
    if (foreground == nullptr) {
        return false;
    }

    DWORD thread_id = GetWindowThreadProcessId(foreground, nullptr);
    GUITHREADINFO info{};
    info.cbSize = sizeof(info);
    if (!GetGUIThreadInfo(thread_id, &info) || info.hwndFocus == nullptr) {
        return false;
    }

    const LONG style = GetWindowLongW(info.hwndFocus, GWL_STYLE);
    if ((style & ES_PASSWORD) != 0) {
        return true;
    }

    wchar_t class_name[256]{};
    if (GetClassNameW(info.hwndFocus, class_name, 256) == 0) {
        return false;
    }

    const std::wstring lowered = to_lower(class_name);
    return lowered.find(L"password") != std::wstring::npos;
}

[[nodiscard]] std::wstring process_file_name(DWORD pid) {
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (process == nullptr) {
        return {};
    }

    wchar_t path[MAX_PATH]{};
    DWORD size = MAX_PATH;
    const BOOL ok = QueryFullProcessImageNameW(process, 0, path, &size);
    CloseHandle(process);
    if (!ok) {
        return {};
    }

    std::wstring full = path;
    const auto slash = full.find_last_of(L"\\/");
    if (slash != std::wstring::npos) {
        full = full.substr(slash + 1);
    }
    return to_lower(std::move(full));
}

[[nodiscard]] bool is_excluded_process(const std::vector<std::string>& names) {
    if (names.empty()) {
        return false;
    }

    HWND foreground = GetForegroundWindow();
    if (foreground == nullptr) {
        return false;
    }

    DWORD pid = 0;
    GetWindowThreadProcessId(foreground, &pid);
    if (pid == 0) {
        return false;
    }

    const std::wstring file = process_file_name(pid);
    if (file.empty()) {
        return false;
    }

    for (const auto& name : names) {
        const std::wstring wanted(name.begin(), name.end());
        if (file == to_lower(wanted)) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] std::wstring window_class_lowered(HWND window) {
    if (window == nullptr) {
        return {};
    }

    wchar_t class_name[256]{};
    if (GetClassNameW(window, class_name, 256) == 0) {
        return {};
    }
    return to_lower(class_name);
}

[[nodiscard]] bool class_looks_chromium(HWND window) {
    const std::wstring lowered = window_class_lowered(window);
    return lowered.find(L"chrome_") != std::wstring::npos ||
           lowered.find(L"chromium") != std::wstring::npos ||
           lowered.find(L"intermediate d3d") != std::wstring::npos ||
           lowered.find(L"cef-") != std::wstring::npos;
}

[[nodiscard]] bool is_win32_text_control(HWND window) {
    const std::wstring lowered = window_class_lowered(window);
    if (lowered.empty()) {
        return false;
    }
    return lowered == L"edit" || lowered.find(L"richedit") != std::wstring::npos ||
           lowered.find(L"scintilla") != std::wstring::npos;
}

[[nodiscard]] bool is_console_window(HWND window) {
    const std::wstring lowered = window_class_lowered(window);
    return lowered.find(L"consolewindowclass") != std::wstring::npos ||
           lowered.find(L"conhost") != std::wstring::npos;
}

[[nodiscard]] bool chromium_in_owner_chain(HWND window) {
    HWND current = window;
    for (int depth = 0; depth < 8 && current != nullptr; ++depth) {
        if (class_looks_chromium(current)) {
            return true;
        }
        HWND next = GetParent(current);
        if (next == nullptr) {
            next = GetWindow(current, GW_OWNER);
        }
        current = next;
    }
    return false;
}

[[nodiscard]] bool process_uses_async_input(DWORD pid) {
    const std::wstring file = process_file_name(pid);
    if (file.empty()) {
        return false;
    }
    if (file == L"wxneur_tests.exe") {
        return false;
    }

    static constexpr const wchar_t* kNeedles[] = {
        L"cursor",
        L"code.exe",
        L"code - insiders.exe",
        L"chrome.exe",
        L"msedge.exe",
        L"brave.exe",
        L"discord.exe",
        L"slack.exe",
        L"telegram.exe",
        L"teams.exe",
        L"notion.exe",
        L"electron.exe",
    };
    for (const wchar_t* needle : kNeedles) {
        if (file.find(needle) != std::wstring::npos) {
            return true;
        }
    }
    return false;
}

}  // namespace

bool FocusGuard::should_ignore(const AppConfig& config) {
    if (config.ignore_password_fields && is_password_field()) {
        return true;
    }
    return is_excluded_process(config.excluded_processes);
}

bool FocusGuard::uses_async_input(HWND window) {
    if (window == nullptr || !IsWindow(window)) {
        return false;
    }

    DWORD pid = 0;
    const DWORD thread_id = GetWindowThreadProcessId(window, &pid);
    GUITHREADINFO info{};
    info.cbSize = sizeof(info);
    const HWND focus = (GetGUIThreadInfo(thread_id, &info) && info.hwndFocus != nullptr)
                           ? info.hwndFocus
                           : window;

    if (is_console_window(window) || is_console_window(focus)) {
        return false;
    }

    const std::wstring file = process_file_name(pid);
    if (file == L"wxneur_tests.exe") {
        return false;
    }

    const std::wstring focus_class = window_class_lowered(focus);
    const std::wstring window_class = window_class_lowered(window);
    // Win11 Notepad hosts RichEditD2DPT. It matches "richedit" but paints on the
    // compositor thread: Unicode SendInput coalesces to the last glyph (аааааа).
    if (file == L"notepad.exe" || focus_class.find(L"richeditd2d") != std::wstring::npos ||
        window_class.find(L"richeditd2d") != std::wstring::npos) {
        return true;
    }

    if (is_win32_text_control(focus) || is_win32_text_control(window)) {
        return false;
    }

    if (process_uses_async_input(pid) || chromium_in_owner_chain(window) ||
        chromium_in_owner_chain(focus)) {
        return true;
    }

    return false;
}
