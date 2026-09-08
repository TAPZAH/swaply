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

}  // namespace

bool FocusGuard::should_ignore(const AppConfig& config) {
    if (config.ignore_password_fields && is_password_field()) {
        return true;
    }
    return is_excluded_process(config.excluded_processes);
}
