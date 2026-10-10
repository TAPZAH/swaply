#pragma once

#include "hotkey.h"

#include <Windows.h>

#include <cstddef>
#include <string>
#include <vector>

struct AppConfig {
    bool enabled = true;
    bool auto_switch = true;
    bool ignore_password_fields = true;
    std::size_t min_word_length = 3;
    Hotkey convert_word{VK_PAUSE, false, false, false, false};
    Hotkey convert_selection{VK_PAUSE, true, false, false, false};
    Hotkey learn_word{VK_PAUSE, false, true, false, false};
    Hotkey undo_conversion{VK_PAUSE, false, false, true, false};
    bool start_with_windows = false;
    bool check_updates = true;
    bool keep_after_enter_tab = false;
    std::vector<std::string> excluded_processes;
    std::vector<std::string> extra_en;
    std::vector<std::string> extra_ru;
    std::vector<std::string> exceptions;

    [[nodiscard]] static AppConfig load();
    bool save() const;
    bool apply_autostart() const;
};

[[nodiscard]] std::wstring config_path();
[[nodiscard]] bool is_portable_install();
