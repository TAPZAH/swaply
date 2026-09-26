#include "settings_dialog.h"

#include "hotkey.h"
#include "resource.h"

#include <commctrl.h>

#include <string>
#include <vector>

namespace {

struct DialogState {
    AppConfig* config = nullptr;
};

struct HotkeyControls {
    int ctrl_id;
    int alt_id;
    int shift_id;
    int combo_id;
};

constexpr HotkeyControls kConvert{IDC_CHK_CONVERT_CTRL, IDC_CHK_CONVERT_ALT, IDC_CHK_CONVERT_SHIFT, IDC_CMB_CONVERT};
constexpr HotkeyControls kSelection{IDC_CHK_SEL_CTRL, IDC_CHK_SEL_ALT, IDC_CHK_SEL_SHIFT, IDC_CMB_SEL};
constexpr HotkeyControls kLearn{IDC_CHK_LEARN_CTRL, IDC_CHK_LEARN_ALT, IDC_CHK_LEARN_SHIFT, IDC_CMB_LEARN};
constexpr HotkeyControls kUndo{IDC_CHK_UNDO_CTRL, IDC_CHK_UNDO_ALT, IDC_CHK_UNDO_SHIFT, IDC_CMB_UNDO};

constexpr UINT kKeyChoices[] = {
    VK_PAUSE, VK_SCROLL, VK_CAPITAL, VK_INSERT, VK_DELETE, VK_HOME, VK_END,
    VK_PRIOR, VK_NEXT,   VK_SPACE,   VK_BACK,   VK_TAB,    VK_SNAPSHOT,
    VK_F1,    VK_F2,     VK_F3,      VK_F4,     VK_F5,     VK_F6,
    VK_F7,    VK_F8,     VK_F9,      VK_F10,    VK_F11,    VK_F12,
    'A',      'B',       'C',        'D',      'E',       'F',
    'G',      'H',       'I',        'J',      'K',       'L',
    'M',      'N',       'O',        'P',      'Q',       'R',
    'S',      'T',       'U',        'V',      'W',       'X',
    'Y',      'Z',       '0',        '1',      '2',       '3',
    '4',      '5',       '6',        '7',      '8',       '9',
};

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

void fill_key_combo(HWND combo, UINT selected) {
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);

    int select_index = 0;
    bool found = false;
    const UINT normalized = (selected == VK_CANCEL) ? VK_PAUSE : selected;

    for (const UINT vk : kKeyChoices) {
        const std::wstring name = Hotkey::key_name(vk);
        const int index = static_cast<int>(SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name.c_str())));
        if (index < 0) {
            continue;
        }
        SendMessageW(combo, CB_SETITEMDATA, static_cast<WPARAM>(index), static_cast<LPARAM>(vk));
        if (vk == normalized) {
            select_index = index;
            found = true;
        }
    }

    if (!found && normalized != 0) {
        const std::wstring name = Hotkey::key_name(normalized);
        const int index =
            static_cast<int>(SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name.c_str())));
        if (index >= 0) {
            SendMessageW(combo, CB_SETITEMDATA, static_cast<WPARAM>(index), static_cast<LPARAM>(normalized));
            select_index = index;
        }
    }

    SendMessageW(combo, CB_SETCURSEL, static_cast<WPARAM>(select_index), 0);
}

[[nodiscard]] UINT combo_vk(HWND combo) {
    const int index = static_cast<int>(SendMessageW(combo, CB_GETCURSEL, 0, 0));
    if (index < 0) {
        return 0;
    }
    return static_cast<UINT>(SendMessageW(combo, CB_GETITEMDATA, static_cast<WPARAM>(index), 0));
}

void write_hotkey(HWND hwnd, const HotkeyControls& ids, const Hotkey& key) {
    set_check(hwnd, ids.ctrl_id, key.ctrl);
    set_check(hwnd, ids.alt_id, key.alt);
    set_check(hwnd, ids.shift_id, key.shift);
    fill_key_combo(GetDlgItem(hwnd, ids.combo_id), key.vk);
}

[[nodiscard]] Hotkey read_hotkey(HWND hwnd, const HotkeyControls& ids) {
    Hotkey key{};
    key.ctrl = get_check(hwnd, ids.ctrl_id);
    key.alt = get_check(hwnd, ids.alt_id);
    key.shift = get_check(hwnd, ids.shift_id);
    key.vk = combo_vk(GetDlgItem(hwnd, ids.combo_id));
    return key;
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
        set_check(hwnd, IDC_CHK_UPDATES, state->config->check_updates);
        set_check(hwnd, IDC_CHK_AUTO, state->config->auto_switch);
        set_check(hwnd, IDC_CHK_PASSWORD, state->config->ignore_password_fields);
        set_check(hwnd, IDC_CHK_AUTOSTART, state->config->start_with_windows);
        SetDlgItemInt(hwnd, IDC_EDIT_MINLEN, static_cast<UINT>(state->config->min_word_length), FALSE);
        set_text(hwnd, IDC_EDIT_EXCLUDED, join_lines(state->config->excluded_processes));
        set_text(hwnd, IDC_EDIT_EXTRA_EN, join_lines(state->config->extra_en));
        set_text(hwnd, IDC_EDIT_EXTRA_RU, join_lines(state->config->extra_ru));
        set_text(hwnd, IDC_EDIT_EXCEPTIONS, join_lines(state->config->exceptions));
        write_hotkey(hwnd, kConvert, state->config->convert_word);
        write_hotkey(hwnd, kSelection, state->config->convert_selection);
        write_hotkey(hwnd, kLearn, state->config->learn_word);
        write_hotkey(hwnd, kUndo, state->config->undo_conversion);
        if (is_portable_install()) {
            EnableWindow(GetDlgItem(hwnd, IDC_CHK_AUTOSTART), FALSE);
        }
        return TRUE;
    }

    if (state == nullptr) {
        return FALSE;
    }

    if (msg == WM_COMMAND) {
        const int id = LOWORD(wparam);
        if (id == IDOK) {
            BOOL translated = FALSE;
            const UINT min_length = GetDlgItemInt(hwnd, IDC_EDIT_MINLEN, &translated, FALSE);
            if (translated && min_length >= 2 && min_length <= 32) {
                state->config->min_word_length = min_length;
            }

            state->config->enabled = get_check(hwnd, IDC_CHK_ENABLED);
            state->config->check_updates = get_check(hwnd, IDC_CHK_UPDATES);
            state->config->auto_switch = get_check(hwnd, IDC_CHK_AUTO);
            state->config->ignore_password_fields = get_check(hwnd, IDC_CHK_PASSWORD);
            state->config->start_with_windows =
                !is_portable_install() && get_check(hwnd, IDC_CHK_AUTOSTART);
            state->config->convert_word = read_hotkey(hwnd, kConvert);
            state->config->convert_selection = read_hotkey(hwnd, kSelection);
            state->config->learn_word = read_hotkey(hwnd, kLearn);
            state->config->undo_conversion = read_hotkey(hwnd, kUndo);
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

INT_PTR CALLBACK about_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM) {
    if (msg == WM_INITDIALOG) {
        return TRUE;
    }
    if (msg == WM_COMMAND && (LOWORD(wparam) == IDOK || LOWORD(wparam) == IDCANCEL)) {
        EndDialog(hwnd, IDOK);
        return TRUE;
    }
    return FALSE;
}

void show_about_dialog(HWND parent) {
    DialogBoxParamW(
        GetModuleHandleW(nullptr),
        MAKEINTRESOURCEW(IDD_ABOUT),
        parent,
        about_proc,
        0);
}

bool show_settings_dialog(HWND parent, AppConfig& config) {
    INITCOMMONCONTROLSEX icc{};
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES;
    InitCommonControlsEx(&icc);

    DialogState state{};
    state.config = &config;

    const HINSTANCE instance = GetModuleHandleW(nullptr);
    const INT_PTR result = DialogBoxParamW(
        instance,
        MAKEINTRESOURCEW(IDD_SETTINGS),
        parent,
        settings_proc,
        reinterpret_cast<LPARAM>(&state));
    return result == IDOK;
}
