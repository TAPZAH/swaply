#pragma once

#include <cstddef>
#include <string>
#include <vector>

struct LinuxConfig {
    bool enabled = true;
    bool auto_switch = true;
    std::size_t min_word_length = 3;
    bool start_with_windows = false;
    std::string convert_word_hotkey = "Pause";
    std::vector<std::string> extra_en;
    std::vector<std::string> extra_ru;
    std::vector<std::string> exceptions;
};

[[nodiscard]] std::string config_directory();
[[nodiscard]] std::string config_path();
[[nodiscard]] LinuxConfig load_config();
bool save_config(const LinuxConfig& config);
void apply_autostart(bool enabled);
