#include "settings_dialog.h"

#include "resource.h"

#include <commctrl.h>

#include <string>
#include <vector>

namespace {

struct DialogState {
    AppConfig* config = nullptr;
    Hotkey convert_word{};
    Hotkey convert_selection{};
    Hotkey learn_word{};
    int capture = 0;
};

[[nodiscard]] bool is_modifier_vk(UINT vk) noexcept {
    switch (vk) {
    case VK_SHIFT:
    case VK_LSHIFT:
    case VK_RSHIFT:
    case VK_CONTROL:
    case VK_LCONTROL:
    case VK_RCONTROL:
    case VK_MENU:
    case VK_LMENU:
    case VK_RMENU:
    case VK_LWIN:
    case VK_RWIN:
    case VK_CAPITAL:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] std::wstring utf8_to_wide(std::string_view text) {
    if (text.empty()) {
        return {};
    }
    const int needed = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (needed <= 0) {
        return {};
    }
    std::wstring wide(static_cast<std::size_t>(needed), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), needed);
    return wide;
}

[[nodiscard]] std::string wide_to_utf8(std::wstring_view text) {
    if (text.empty()) {
        return {};
    }
    const int needed = WideCharToMultiByte(
        CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (needed <= 0) {
        return {};
    }
    std::string utf8(static_cast<std::size_t>(needed), '\0');
    WideCharToMultiByte(
        CP_UTF8, 0, text.data(), static_cast<int>(text.size()), utf8.data(), needed, nullptr, nullptr);
    return utf8;
}

[[nodiscard]] std::wstring join_lines(const std::vector<std::string>& values) {
    std::wstring text;
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i != 0) {
            text += L"\r\n";
        }
        text += utf8_to_wide(values[i]);
    }
    return text;
}

[[nodiscard]] std::vector<std::string> split_lines(const std::wstring& text) {
    std::vector<std::string> values;
    std::wstring current;
    for (std::size_t i = 0; i <= text.size(); ++i) {
        const wchar_t ch = (i < text.size()) ? text[i] : L'\n';
        if (ch == L'\r') {
            continue;
        }
        if (ch == L'\n' || i == text.size()) {
            while (!current.empty() && (current.back() == L' ' || current.back() == L'\t')) {
                current.pop_back();
            }
            std::size_t start = 0;
            while (start < current.size() && (current[start] == L' ' || current[start] == L'\t')) {
                ++start;
            }
            if (start < current.size()) {
                values.push_back(wide_to_utf8(current.substr(start)));
            }
            current.clear();
        } else {
            current.push_back(ch);
        }
    }
    return values;
}

void set_check(HWND hwnd, int id, bool value) {
    SendDlgItemMessageW(hwnd, id, BM_SETCHECK, value ? BST_CHECKED : BST_UNCHECKED, 0);
}

[[nodiscard]] bool get_check(HWND hwnd, int id) {
    return SendDlgItemMessageW(hwnd, id, BM_GETCHECK, 0, 0) == BST_CHECKED;
}

void set_text(HWND hwnd, int id, const std::wstring& text) {
    SetDlgItemTextW(hwnd, id, text.c_str());
}

[[nodiscard]] std::wstring get_text(HWND hwnd, int id) {
    const int length = GetWindowTextLengthW(GetDlgItem(hwnd, id));
    if (length <= 0) {
        return {};
    }
    std::wstring text(static_cast<std::size_t>(length) + 1, L'\0');
    GetDlgItemTextW(hwnd, id, text.data(), length + 1);
    text.resize(static_cast<std::size_t>(length));
    return text;
}

void refresh_hotkey_buttons(HWND hwnd, const DialogState& state) {
    const wchar_t* capture_text = L"Нажмите комбинацию...";
    set_text(hwnd, IDC_BTN_CONVERT, state.capture == 1 ? capture_text : state.convert_word.to_wstring());
    set_text(hwnd, IDC_BTN_SELECTION, state.capture == 2 ? capture_text : state.convert_selection.to_wstring());
    set_text(hwnd, IDC_BTN_LEARN, state.capture == 3 ? capture_text : state.learn_word.to_wstring());
}

void apply_captured_hotkey(DialogState& state, const Hotkey& key) {
    if (state.capture == 1) {
        state.convert_word = key;
    } else if (state.capture == 2) {
        state.convert_selection = key;
    } else if (state.capture == 3) {
        state.learn_word = key;
    }
    state.capture = 0;
}

INT_PTR CALLBACK settings_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    auto* state = reinterpret_cast<DialogState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    if (msg == WM_INITDIALOG) {
        state = reinterpret_cast<DialogState*>(lparam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
        if (state == nullptr || state->config == nullptr) {
            EndDialog(hwnd, IDCANCEL);
            return TRUE;
        }

        set_check(hwnd, IDC_CHK_ENABLED, state->config->enabled);
        set_check(hwnd, IDC_CHK_AUTO, state->config->auto_switch);
        set_check(hwnd, IDC_CHK_PASSWORD, state->config->ignore_password_fields);
        set_check(hwnd, IDC_CHK_AUTOSTART, state->config->start_with_windows);
        SetDlgItemInt(hwnd, IDC_EDIT_MINLEN, static_cast<UINT>(state->config->min_word_length), FALSE);
        set_text(hwnd, IDC_EDIT_EXCLUDED, join_lines(state->config->excluded_processes));
        set_text(hwnd, IDC_EDIT_EXTRA_EN, join_lines(state->config->extra_en));
        set_text(hwnd, IDC_EDIT_EXTRA_RU, join_lines(state->config->extra_ru));
        set_text(hwnd, IDC_EDIT_EXCEPTIONS, join_lines(state->config->exceptions));
        refresh_hotkey_buttons(hwnd, *state);
        return TRUE;
    }

    if (state == nullptr) {
        return FALSE;
    }

    if ((msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) && state->capture != 0) {
        const UINT vk_raw = static_cast<UINT>(wparam);
        if (vk_raw == VK_ESCAPE) {
            state->capture = 0;
            refresh_hotkey_buttons(hwnd, *state);
            return TRUE;
        }
        if (is_modifier_vk(vk_raw)) {
            return TRUE;
        }

        Hotkey key{};
        key.vk = (vk_raw == VK_CANCEL) ? VK_PAUSE : vk_raw;
        key.ctrl = GetKeyState(VK_CONTROL) < 0;
        key.alt = GetKeyState(VK_MENU) < 0;
        key.shift = GetKeyState(VK_SHIFT) < 0;
        key.win = GetKeyState(VK_LWIN) < 0 || GetKeyState(VK_RWIN) < 0;
        apply_captured_hotkey(*state, key);
        refresh_hotkey_buttons(hwnd, *state);
        return TRUE;
    }

    if (msg == WM_COMMAND) {
        const int id = LOWORD(wparam);
        if (id == IDC_BTN_CONVERT || id == IDC_BTN_SELECTION || id == IDC_BTN_LEARN) {
            if (id == IDC_BTN_CONVERT) {
                state->capture = 1;
            } else if (id == IDC_BTN_SELECTION) {
                state->capture = 2;
            } else {
                state->capture = 3;
            }
            refresh_hotkey_buttons(hwnd, *state);
            SetFocus(hwnd);
            return TRUE;
        }

        if (id == IDOK) {
            BOOL translated = FALSE;
            const UINT min_length = GetDlgItemInt(hwnd, IDC_EDIT_MINLEN, &translated, FALSE);
            if (translated && min_length >= 2 && min_length <= 32) {
                state->config->min_word_length = min_length;
            }

            state->config->enabled = get_check(hwnd, IDC_CHK_ENABLED);
            state->config->auto_switch = get_check(hwnd, IDC_CHK_AUTO);
            state->config->ignore_password_fields = get_check(hwnd, IDC_CHK_PASSWORD);
            state->config->start_with_windows = get_check(hwnd, IDC_CHK_AUTOSTART);
            state->config->convert_word = state->convert_word;
            state->config->convert_selection = state->convert_selection;
            state->config->learn_word = state->learn_word;
            state->config->excluded_processes = split_lines(get_text(hwnd, IDC_EDIT_EXCLUDED));
            state->config->extra_en = split_lines(get_text(hwnd, IDC_EDIT_EXTRA_EN));
            state->config->extra_ru = split_lines(get_text(hwnd, IDC_EDIT_EXTRA_RU));
            state->config->exceptions = split_lines(get_text(hwnd, IDC_EDIT_EXCEPTIONS));
            EndDialog(hwnd, IDOK);
            return TRUE;
        }

        if (id == IDCANCEL) {
            EndDialog(hwnd, IDCANCEL);
            return TRUE;
        }
    }

    return FALSE;
}

}  // namespace

bool show_settings_dialog(HWND parent, AppConfig& config) {
    INITCOMMONCONTROLSEX icc{};
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES;
    InitCommonControlsEx(&icc);

    DialogState state{};
    state.config = &config;
    state.convert_word = config.convert_word;
    state.convert_selection = config.convert_selection;
    state.learn_word = config.learn_word;

    const HINSTANCE instance = GetModuleHandleW(nullptr);
    const INT_PTR result = DialogBoxParamW(
        instance,
        MAKEINTRESOURCEW(IDD_SETTINGS),
        parent,
        settings_proc,
        reinterpret_cast<LPARAM>(&state));
    return result == IDOK;
}
