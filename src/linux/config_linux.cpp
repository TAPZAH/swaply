#include "config_linux.h"

#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <system_error>

#include <unistd.h>

namespace {

[[nodiscard]] std::string read_all(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return {};
    }
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

[[nodiscard]] std::size_t key_position(const std::string& text, const std::string& key) {
    return text.find("\"" + key + "\"");
}

[[nodiscard]] bool parse_bool(const std::string& text, const std::string& key, bool fallback) {
    std::size_t pos = key_position(text, key);
    if (pos == std::string::npos) {
        return fallback;
    }
    pos = text.find(':', pos);
    if (pos == std::string::npos) {
        return fallback;
    }
    const std::size_t yes = text.find("true", pos);
    const std::size_t no = text.find("false", pos);
    if (yes != std::string::npos && (no == std::string::npos || yes < no)) {
        return true;
    }
    if (no != std::string::npos) {
        return false;
    }
    return fallback;
}

[[nodiscard]] long parse_int(const std::string& text, const std::string& key, long fallback) {
    std::size_t pos = key_position(text, key);
    if (pos == std::string::npos) {
        return fallback;
    }
    pos = text.find(':', pos);
    if (pos == std::string::npos) {
        return fallback;
    }
    ++pos;
    while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) {
        ++pos;
    }
    char* end = nullptr;
    const long value = std::strtol(text.c_str() + pos, &end, 10);
    if (end == text.c_str() + pos) {
        return fallback;
    }
    return value;
}

[[nodiscard]] std::string parse_string(const std::string& text, const std::string& key, const std::string& fallback) {
    std::size_t pos = key_position(text, key);
    if (pos == std::string::npos) {
        return fallback;
    }
    pos = text.find(':', pos);
    if (pos == std::string::npos) {
        return fallback;
    }
    pos = text.find('"', pos + 1);
    if (pos == std::string::npos) {
        return fallback;
    }
    std::string value;
    for (++pos; pos < text.size(); ++pos) {
        const char ch = text[pos];
        if (ch == '\\' && pos + 1 < text.size()) {
            value.push_back(text[++pos]);
            continue;
        }
        if (ch == '"') {
            return value;
        }
        value.push_back(ch);
    }
    return fallback;
}

[[nodiscard]] std::vector<std::string> parse_string_array(const std::string& text, const std::string& key) {
    std::vector<std::string> result;
    std::size_t pos = key_position(text, key);
    if (pos == std::string::npos) {
        return result;
    }
    pos = text.find('[', pos);
    if (pos == std::string::npos) {
        return result;
    }
    while (pos < text.size() && text[pos] != ']') {
        if (text[pos] != '"') {
            ++pos;
            continue;
        }
        std::string value;
        for (++pos; pos < text.size(); ++pos) {
            const char ch = text[pos];
            if (ch == '\\' && pos + 1 < text.size()) {
                value.push_back(text[++pos]);
                continue;
            }
            if (ch == '"') {
                break;
            }
            value.push_back(ch);
        }
        result.push_back(std::move(value));
        ++pos;
    }
    return result;
}

[[nodiscard]] std::string escape_json(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (const char ch : text) {
        if (ch == '"' || ch == '\\') {
            out.push_back('\\');
        }
        out.push_back(ch);
    }
    return out;
}

[[nodiscard]] std::string array_json(const std::vector<std::string>& values) {
    std::string out = "[";
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i != 0) {
            out += ", ";
        }
        out += "\"" + escape_json(values[i]) + "\"";
    }
    out += "]";
    return out;
}

[[nodiscard]] std::string executable_path() {
    char path[4096]{};
    const ssize_t length = readlink("/proc/self/exe", path, sizeof(path) - 1);
    if (length <= 0) {
        return {};
    }
    return std::string(path, static_cast<std::size_t>(length));
}

}  // namespace

std::string config_directory() {
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg != nullptr && *xdg != '\0') {
        return std::string(xdg) + "/swaply";
    }
    if (const char* home = std::getenv("HOME"); home != nullptr && *home != '\0') {
        return std::string(home) + "/.config/swaply";
    }
    return ".swaply";
}

std::string config_path() {
    return config_directory() + "/config.json";
}

LinuxConfig load_config() {
    LinuxConfig config;
    const std::string text = read_all(config_path());
    if (text.empty()) {
        return config;
    }

    config.enabled = parse_bool(text, "enabled", config.enabled);
    config.auto_switch = parse_bool(text, "auto_switch", config.auto_switch);
    config.start_with_windows = parse_bool(text, "start_with_windows", config.start_with_windows);
    const long min_length = parse_int(text, "min_word_length", static_cast<long>(config.min_word_length));
    if (min_length >= 1 && min_length <= 8) {
        config.min_word_length = static_cast<std::size_t>(min_length);
    }
    config.convert_word_hotkey = parse_string(text, "convert_word_hotkey", config.convert_word_hotkey);
    config.extra_en = parse_string_array(text, "extra_en");
    config.extra_ru = parse_string_array(text, "extra_ru");
    config.exceptions = parse_string_array(text, "exceptions");
    return config;
}

bool save_config(const LinuxConfig& config) {
    std::error_code error;
    std::filesystem::create_directories(config_directory(), error);
    std::ofstream out(config_path(), std::ios::binary | std::ios::trunc);
    if (!out) {
        return false;
    }
    out << "{\n"
        << "  \"enabled\": " << (config.enabled ? "true" : "false") << ",\n"
        << "  \"auto_switch\": " << (config.auto_switch ? "true" : "false") << ",\n"
        << "  \"min_word_length\": " << config.min_word_length << ",\n"
        << "  \"start_with_windows\": " << (config.start_with_windows ? "true" : "false") << ",\n"
        << "  \"convert_word_hotkey\": \"" << escape_json(config.convert_word_hotkey) << "\",\n"
        << "  \"extra_en\": " << array_json(config.extra_en) << ",\n"
        << "  \"extra_ru\": " << array_json(config.extra_ru) << ",\n"
        << "  \"exceptions\": " << array_json(config.exceptions) << "\n"
        << "}\n";
    return static_cast<bool>(out);
}

void apply_autostart(bool enabled) {
    const char* home = std::getenv("HOME");
    if (home == nullptr || *home == '\0') {
        return;
    }
    const std::string dir = std::string(home) + "/.config/autostart";
    const std::string path = dir + "/swaply.desktop";
    std::error_code error;
    if (!enabled) {
        std::filesystem::remove(path, error);
        return;
    }
    std::filesystem::create_directories(dir, error);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        return;
    }
    std::string exec = executable_path();
    char own[4096]{};
    const ssize_t own_length = readlink("/proc/self/exe", own, sizeof(own) - 1);
    if (own_length > 0) {
        const std::string self(own, static_cast<std::size_t>(own_length));
        const auto slash = self.find_last_of('/');
        if (slash != std::string::npos) {
            const std::string tray = self.substr(0, slash + 1) + "swaply-tray";
            if (access(tray.c_str(), X_OK) == 0) {
                exec = tray;
            }
        }
    }
    out << "[Desktop Entry]\n"
        << "Type=Application\n"
        << "Name=Swaply\n"
        << "Comment=EN/RU layout switcher\n"
        << "Exec=" << exec << "\n"
        << "X-GNOME-Autostart-enabled=true\n";
}
