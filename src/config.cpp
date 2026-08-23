#include "config.h"

#include <cctype>
#include <sstream>
#include <string_view>

namespace {

constexpr wchar_t kAppFolder[] = L"wxneur";
constexpr wchar_t kConfigName[] = L"config.json";
constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kRunValue[] = L"wxneur";

[[nodiscard]] std::wstring appdata_dir() {
    wchar_t appdata[MAX_PATH]{};
    const DWORD length = GetEnvironmentVariableW(L"APPDATA", appdata, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return {};
    }

    std::wstring dir = appdata;
    dir += L'\\';
    dir += kAppFolder;
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir;
}

[[nodiscard]] std::string read_utf8_file(const std::wstring& path) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return {};
    }

    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size) || size.QuadPart <= 0 || size.QuadPart > 256 * 1024) {
        CloseHandle(file);
        return {};
    }

    std::string text(static_cast<std::size_t>(size.QuadPart), '\0');
    DWORD read = 0;
    const BOOL ok = ReadFile(file, text.data(), static_cast<DWORD>(text.size()), &read, nullptr);
    CloseHandle(file);
    if (!ok) {
        return {};
    }
    text.resize(read);
    return text;
}

void write_utf8_file(const std::wstring& path, std::string_view text) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }

    DWORD written = 0;
    WriteFile(file, text.data(), static_cast<DWORD>(text.size()), &written, nullptr);
    CloseHandle(file);
}

void skip_ws(std::string_view text, std::size_t& i) {
    while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) {
        ++i;
    }
}

[[nodiscard]] bool parse_string(std::string_view text, std::size_t& i, std::string& value) {
    skip_ws(text, i);
    if (i >= text.size() || text[i] != '"') {
        return false;
    }
    ++i;

    value.clear();
    while (i < text.size() && text[i] != '"') {
        if (text[i] == '\\' && i + 1 < text.size()) {
            ++i;
        }
        value.push_back(text[i]);
        ++i;
    }
    if (i >= text.size()) {
        return false;
    }
    ++i;
    return true;
}

[[nodiscard]] bool parse_bool(std::string_view text, std::size_t& i, bool& value) {
    skip_ws(text, i);
    if (text.substr(i, 4) == "true") {
        i += 4;
        value = true;
        return true;
    }
    if (text.substr(i, 5) == "false") {
        i += 5;
        value = false;
        return true;
    }
    return false;
}

[[nodiscard]] bool parse_size(std::string_view text, std::size_t& i, std::size_t& value) {
    skip_ws(text, i);
    if (i >= text.size() || !std::isdigit(static_cast<unsigned char>(text[i]))) {
        return false;
    }

    std::size_t parsed = 0;
    while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i]))) {
        parsed = parsed * 10 + static_cast<std::size_t>(text[i] - '0');
        ++i;
    }
    value = parsed;
    return true;
}

void skip_value(std::string_view text, std::size_t& i) {
    skip_ws(text, i);
    if (i >= text.size()) {
        return;
    }

    if (text[i] == '"') {
        std::string unused;
        static_cast<void>(parse_string(text, i, unused));
        return;
    }

    if (text[i] == '{' || text[i] == '[') {
        const char open = text[i];
        const char close = (open == '{') ? '}' : ']';
        int depth = 1;
        ++i;
        while (i < text.size() && depth > 0) {
            if (text[i] == '"') {
                std::string unused;
                static_cast<void>(parse_string(text, i, unused));
                continue;
            }
            if (text[i] == open) {
                ++depth;
            } else if (text[i] == close) {
                --depth;
            }
            ++i;
        }
        return;
    }

    while (i < text.size() && text[i] != ',' && text[i] != '}' && text[i] != ']') {
        ++i;
    }
}

[[nodiscard]] bool parse_string_array(std::string_view text, std::size_t& i, std::vector<std::string>& values) {
    skip_ws(text, i);
    if (i >= text.size() || text[i] != '[') {
        return false;
    }
    ++i;
    values.clear();

    while (i < text.size()) {
        skip_ws(text, i);
        if (i < text.size() && text[i] == ']') {
            ++i;
            return true;
        }

        std::string item;
        if (!parse_string(text, i, item)) {
            skip_value(text, i);
        } else if (!item.empty()) {
            values.push_back(std::move(item));
        }

        skip_ws(text, i);
        if (i < text.size() && text[i] == ',') {
            ++i;
        }
    }
    return false;
}

void apply_field(AppConfig& config, const std::string& key, std::string_view text, std::size_t& i) {
    if (key == "enabled") {
        static_cast<void>(parse_bool(text, i, config.enabled));
        return;
    }
    if (key == "auto_switch") {
        static_cast<void>(parse_bool(text, i, config.auto_switch));
        return;
    }
    if (key == "ignore_password_fields") {
        static_cast<void>(parse_bool(text, i, config.ignore_password_fields));
        return;
    }
    if (key == "start_with_windows") {
        static_cast<void>(parse_bool(text, i, config.start_with_windows));
        return;
    }
    if (key == "min_word_length") {
        std::size_t value = config.min_word_length;
        if (parse_size(text, i, value) && value >= 2 && value <= 32) {
            config.min_word_length = value;
        }
        return;
    }
    if (key == "convert_hotkey") {
        std::string value;
        if (parse_string(text, i, value) && !value.empty()) {
            Hotkey parsed = Hotkey::parse(value);
            if (parsed.vk != 0) {
                config.convert_word = parsed;
            }
        }
        return;
    }
    if (key == "convert_word_hotkey") {
        std::string value;
        if (parse_string(text, i, value) && !value.empty()) {
            Hotkey parsed = Hotkey::parse(value);
            if (parsed.vk != 0) {
                config.convert_word = parsed;
            }
        }
        return;
    }
    if (key == "convert_selection_hotkey") {
        std::string value;
        if (parse_string(text, i, value) && !value.empty()) {
            Hotkey parsed = Hotkey::parse(value);
            if (parsed.vk != 0) {
                config.convert_selection = parsed;
            }
        }
        return;
    }
    if (key == "learn_word_hotkey") {
        std::string value;
        if (parse_string(text, i, value) && !value.empty()) {
            Hotkey parsed = Hotkey::parse(value);
            if (parsed.vk != 0) {
                config.learn_word = parsed;
            }
        }
        return;
    }
    if (key == "excluded_processes") {
        static_cast<void>(parse_string_array(text, i, config.excluded_processes));
        return;
    }
    if (key == "extra_en") {
        static_cast<void>(parse_string_array(text, i, config.extra_en));
        return;
    }
    if (key == "extra_ru") {
        static_cast<void>(parse_string_array(text, i, config.extra_ru));
        return;
    }
    if (key == "exceptions") {
        static_cast<void>(parse_string_array(text, i, config.exceptions));
        return;
    }

    skip_value(text, i);
}

void parse_object(AppConfig& config, std::string_view text) {
    std::size_t i = 0;
    skip_ws(text, i);
    if (i >= text.size() || text[i] != '{') {
        return;
    }
    ++i;

    while (i < text.size()) {
        skip_ws(text, i);
        if (i < text.size() && text[i] == '}') {
            break;
        }

        std::string key;
        if (!parse_string(text, i, key)) {
            break;
        }
        skip_ws(text, i);
        if (i >= text.size() || text[i] != ':') {
            break;
        }
        ++i;
        apply_field(config, key, text, i);
        skip_ws(text, i);
        if (i < text.size() && text[i] == ',') {
            ++i;
        }
    }
}

[[nodiscard]] std::string json_escape(std::string_view text) {
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

void write_string_array(std::ostringstream& ss, const char* key, const std::vector<std::string>& values) {
    ss << "  \"" << key << "\": [";
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i != 0) {
            ss << ", ";
        }
        ss << '"' << json_escape(values[i]) << '"';
    }
    ss << ']';
}

[[nodiscard]] bool registry_autostart_enabled() {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) {
        return false;
    }

    wchar_t value[MAX_PATH]{};
    DWORD size = sizeof(value);
    const LSTATUS status = RegQueryValueExW(key, kRunValue, nullptr, nullptr, reinterpret_cast<LPBYTE>(value), &size);
    RegCloseKey(key);
    return status == ERROR_SUCCESS && value[0] != L'\0';
}

[[nodiscard]] std::wstring exe_path() {
    wchar_t path[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return {};
    }
    return path;
}

}  // namespace

std::wstring config_dir() {
    return appdata_dir();
}

std::wstring config_path() {
    const std::wstring dir = appdata_dir();
    if (dir.empty()) {
        return {};
    }
    return dir + L'\\' + kConfigName;
}

void AppConfig::apply_autostart() const {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &key) != ERROR_SUCCESS) {
        return;
    }

    if (start_with_windows) {
        const std::wstring path = exe_path();
        if (!path.empty()) {
            const DWORD bytes = static_cast<DWORD>((path.size() + 1) * sizeof(wchar_t));
            RegSetValueExW(
                key,
                kRunValue,
                0,
                REG_SZ,
                reinterpret_cast<const BYTE*>(path.c_str()),
                bytes);
        }
    } else {
        RegDeleteValueW(key, kRunValue);
    }

    RegCloseKey(key);
}

AppConfig AppConfig::load() {
    AppConfig config{};
    config.excluded_processes = {"keepass.exe", "keepassxc.exe", "1password.exe"};

    const std::wstring user_path = config_path();
    std::string text = user_path.empty() ? std::string{} : read_utf8_file(user_path);

    if (text.empty()) {
        const std::wstring exe = exe_path();
        const auto slash = exe.find_last_of(L"\\/");
        if (slash != std::wstring::npos) {
            text = read_utf8_file(exe.substr(0, slash) + L"\\default_config.json");
        }
    }

    if (!text.empty()) {
        parse_object(config, text);
    }

    if (!user_path.empty() && read_utf8_file(user_path).empty()) {
        config.save();
    }

    config.start_with_windows = registry_autostart_enabled();
    return config;
}

void AppConfig::save() const {
    const std::wstring path = config_path();
    if (path.empty()) {
        return;
    }

    std::ostringstream ss;
    ss << "{\n"
       << "  \"enabled\": " << (enabled ? "true" : "false") << ",\n"
       << "  \"auto_switch\": " << (auto_switch ? "true" : "false") << ",\n"
       << "  \"ignore_password_fields\": " << (ignore_password_fields ? "true" : "false") << ",\n"
       << "  \"min_word_length\": " << min_word_length << ",\n"
       << "  \"convert_word_hotkey\": \"" << json_escape(convert_word.to_string()) << "\",\n"
       << "  \"convert_selection_hotkey\": \"" << json_escape(convert_selection.to_string()) << "\",\n"
       << "  \"learn_word_hotkey\": \"" << json_escape(learn_word.to_string()) << "\",\n"
       << "  \"start_with_windows\": " << (start_with_windows ? "true" : "false") << ",\n";
    write_string_array(ss, "excluded_processes", excluded_processes);
    ss << ",\n";
    write_string_array(ss, "extra_en", extra_en);
    ss << ",\n";
    write_string_array(ss, "extra_ru", extra_ru);
    ss << ",\n";
    write_string_array(ss, "exceptions", exceptions);
    ss << "\n}\n";
    write_utf8_file(path, ss.str());
}
