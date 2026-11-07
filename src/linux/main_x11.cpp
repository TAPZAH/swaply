// Swaply for Linux/X11 — global layout switcher.
// Copyright (C) 2026 Tap3ah. GNU GPL v3+.
//
// X11 backend: records global key events with XRecord, recognises words typed
// in the wrong EN/RU layout and retypes them with XTest while switching the
// XKB group. Wayland is not supported.

#include "layout_detector.h"
#include "utf8.h"
#include "version.h"

#include <X11/XKBlib.h>
#include <X11/Xlib.h>
#include <X11/extensions/XTest.h>
#include <X11/extensions/record.h>
#include <X11/keysym.h>

#include <cctype>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <unistd.h>

namespace {

struct Hotkey {
    bool ctrl = false;
    bool alt = false;
    bool shift = false;
    bool super = false;
    KeySym sym = NoSymbol;

    [[nodiscard]] bool valid() const noexcept { return sym != NoSymbol; }
};

struct Config {
    bool enabled = true;
    bool auto_switch = true;
    std::size_t min_word_length = 3;
    bool start_with_windows = false;
    Hotkey convert_word{false, false, false, false, XK_Pause};
    std::string convert_word_text = "Pause";
    std::vector<std::string> extra_en;
    std::vector<std::string> extra_ru;
    std::vector<std::string> exceptions;
};

[[nodiscard]] std::string read_all(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return {};
    }
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

[[nodiscard]] std::string config_directory() {
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg != nullptr && *xdg != '\0') {
        return std::string(xdg) + "/swaply";
    }
    if (const char* home = std::getenv("HOME"); home != nullptr && *home != '\0') {
        return std::string(home) + "/.config/swaply";
    }
    return ".swaply";
}

[[nodiscard]] std::string trim_copy(const std::string& text) {
    std::size_t begin = 0;
    std::size_t end = text.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(text[begin]))) {
        ++begin;
    }
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1]))) {
        --end;
    }
    return text.substr(begin, end - begin);
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

[[nodiscard]] KeySym key_name_to_sym(const std::string& name) {
    const std::string trimmed = trim_copy(name);
    if (trimmed.empty()) {
        return NoSymbol;
    }
    const KeySym direct = XStringToKeysym(trimmed.c_str());
    if (direct != NoSymbol) {
        return direct;
    }
    if (trimmed.size() == 1) {
        return static_cast<KeySym>(static_cast<unsigned char>(trimmed[0]));
    }
    return NoSymbol;
}

[[nodiscard]] Hotkey parse_hotkey(const std::string& text) {
    Hotkey hotkey;
    std::istringstream input(text);
    std::string token;
    while (std::getline(input, token, '+')) {
        const std::string name = trim_copy(token);
        std::string lower;
        lower.reserve(name.size());
        for (const char ch : name) {
            lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
        }
        if (lower == "ctrl" || lower == "control") {
            hotkey.ctrl = true;
        } else if (lower == "alt" || lower == "mod1") {
            hotkey.alt = true;
        } else if (lower == "shift") {
            hotkey.shift = true;
        } else if (lower == "super" || lower == "win") {
            hotkey.super = true;
        } else {
            hotkey.sym = key_name_to_sym(name);
        }
    }
    return hotkey;
}

[[nodiscard]] Config load_config() {
    Config config;
    const std::string dir = config_directory();
    std::error_code error;
    std::filesystem::create_directories(dir, error);

    const std::string path = dir + "/config.json";
    const std::string text = read_all(path);
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
    config.convert_word_text = parse_string(text, "convert_word_hotkey", config.convert_word_text);
    config.convert_word = parse_hotkey(config.convert_word_text);
    config.extra_en = parse_string_array(text, "extra_en");
    config.extra_ru = parse_string_array(text, "extra_ru");
    config.exceptions = parse_string_array(text, "exceptions");
    return config;
}

[[nodiscard]] std::string executable_path() {
    char path[4096]{};
    const ssize_t length = readlink("/proc/self/exe", path, sizeof(path) - 1);
    if (length <= 0) {
        return {};
    }
    return std::string(path, static_cast<std::size_t>(length));
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
    out << "[Desktop Entry]\n"
        << "Type=Application\n"
        << "Name=Swaply\n"
        << "Comment=EN/RU layout switcher\n"
        << "Exec=" << executable_path() << "\n"
        << "X-GNOME-Autostart-enabled=true\n";
}

[[nodiscard]] unsigned keysym_to_ucs(KeySym sym) noexcept {
    if ((sym & 0xFF000000u) == 0x01000000u) {
        return static_cast<unsigned>(sym & 0x00FFFFFFu);
    }
    if (sym >= 0x20 && sym <= 0x7E) {
        return static_cast<unsigned>(sym);
    }
    if (sym >= 0xA0 && sym <= 0xFF) {
        return static_cast<unsigned>(sym);
    }
    return 0;
}

[[nodiscard]] bool is_latin(unsigned code) noexcept {
    return (code >= 'A' && code <= 'Z') || (code >= 'a' && code <= 'z');
}

[[nodiscard]] bool is_cyrillic(unsigned code) noexcept {
    return code >= 0x0400 && code <= 0x04FF;
}

[[nodiscard]] bool is_letter(wchar_t ch) noexcept {
    return (ch >= L'a' && ch <= L'z') || (ch >= L'A' && ch <= L'Z') || (ch >= 0x0400 && ch <= 0x04FF);
}

[[nodiscard]] wchar_t toggle_case(wchar_t ch) noexcept {
    if (ch >= L'a' && ch <= L'z') {
        return static_cast<wchar_t>(ch - L'a' + L'A');
    }
    if (ch >= L'A' && ch <= L'Z') {
        return static_cast<wchar_t>(ch - L'A' + L'a');
    }
    if (ch >= 0x0430 && ch <= 0x044F) {
        return static_cast<wchar_t>(ch - 0x0430 + 0x0410);
    }
    if (ch >= 0x0410 && ch <= 0x042F) {
        return static_cast<wchar_t>(ch - 0x0410 + 0x0430);
    }
    return ch;
}

struct Glyph {
    KeyCode keycode = 0;
    bool shift = false;
    wchar_t ch = 0;
    wchar_t alt = 0;
};

struct PendingConvert {
    std::vector<Glyph> glyphs;
    KeyCode terminator = 0;
    bool terminator_shift = false;
    int target_group = 0;
};

class X11App {
public:
    X11App(Display* display, Config config)
        : display_(display), config_(std::move(config)) {}

    void init_keys() {
        backspace_key_ = XKeysymToKeycode(display_, XK_BackSpace);
        return_key_ = XKeysymToKeycode(display_, XK_Return);
        tab_key_ = XKeysymToKeycode(display_, XK_Tab);
        space_key_ = XKeysymToKeycode(display_, XK_space);
        shift_key_ = XKeysymToKeycode(display_, XK_Shift_L);

        const KeySym navigation[] = {
            XK_Escape, XK_Left, XK_Right, XK_Up, XK_Down, XK_Home, XK_End,
            XK_Prior, XK_Next, XK_Delete, XK_Insert,
        };
        for (const KeySym sym : navigation) {
            const KeyCode keycode = XKeysymToKeycode(display_, sym);
            if (keycode != 0) {
                navigation_keys_.insert(keycode);
            }
        }
        for (int i = 0; i < 24; ++i) {
            const KeyCode keycode = XKeysymToKeycode(display_, XK_F1 + i);
            if (keycode != 0) {
                navigation_keys_.insert(keycode);
            }
        }
    }

    void detect_layouts() {
        en_group_ = -1;
        ru_group_ = -1;
        const KeyCode key = XKeysymToKeycode(display_, XK_a);
        if (key == 0) {
            return;
        }
        for (int group = 0; group < 4; ++group) {
            const unsigned code = keysym_to_ucs(XkbKeycodeToKeysym(display_, key, group, 0));
            if (is_latin(code) && en_group_ < 0) {
                en_group_ = group;
            } else if (is_cyrillic(code) && ru_group_ < 0) {
                ru_group_ = group;
            }
        }
    }

    [[nodiscard]] bool ready() const noexcept { return en_group_ >= 0 && ru_group_ >= 0; }

    void set_context(XRecordContext context) noexcept { context_ = context; }

    void handle_key(KeyCode keycode, unsigned int state, bool pressed) {
        if (wait_release_) {
            if (!pressed && keycode == wait_release_key_) {
                wait_release_ = false;
                wait_release_key_ = 0;
                if (context_ != 0) {
                    XRecordDisableContext(display_, context_);
                }
            }
            return;
        }
        if (!pressed) {
            return;
        }
        if (pending_.has_value()) {
            return;
        }
        const bool ctrl = (state & ControlMask) != 0;
        const bool shift = (state & ShiftMask) != 0;
        const bool alt = (state & Mod1Mask) != 0;
        const bool super = (state & Mod4Mask) != 0;
        const bool caps = (state & LockMask) != 0;
        const int group = XkbGroupForCoreState(state);

        if (matches_hotkey(config_.convert_word, keycode, ctrl, alt, shift, super)) {
            convert_current(0, false, group);
            return;
        }

        if (ctrl || alt || super) {
            word_.clear();
            return;
        }

        if (keycode == backspace_key_) {
            if (!word_.empty()) {
                word_.pop_back();
            }
            return;
        }

        if (keycode == return_key_ || keycode == tab_key_ || keycode == space_key_) {
            handle_delimiter(keycode, shift, group);
            return;
        }

        if (navigation_keys_.count(keycode) != 0) {
            word_.clear();
            return;
        }

        const wchar_t ch = char_for(keycode, group, shift, caps);
        if (ch == 0) {
            return;
        }
        const wchar_t alt_char = char_for(keycode, opposite_group(group), shift, caps);
        if (word_.size() < 128) {
            word_.push_back(Glyph{keycode, shift, ch, alt_char});
        }
    }

    void run_pending() {
        if (!pending_.has_value()) {
            return;
        }
        const PendingConvert pending = std::move(*pending_);
        pending_.reset();

        const std::size_t deletes = pending.glyphs.size() + (pending.terminator != 0 ? 1 : 0);
        for (std::size_t i = 0; i < deletes; ++i) {
            tap(backspace_key_, false);
        }

        XkbLockGroup(display_, XkbUseCoreKbd, static_cast<unsigned int>(pending.target_group));
        XFlush(display_);

        for (const Glyph& glyph : pending.glyphs) {
            tap(glyph.keycode, glyph.shift);
        }
        if (pending.terminator != 0) {
            tap(pending.terminator, pending.terminator_shift);
        }
        XFlush(display_);
    }

private:
    [[nodiscard]] int opposite_group(int group) const noexcept {
        return group == ru_group_ ? en_group_ : ru_group_;
    }

    [[nodiscard]] wchar_t char_for(KeyCode keycode, int group, bool shift, bool caps) const noexcept {
        if (group < 0) {
            return 0;
        }
        const KeySym sym = XkbKeycodeToKeysym(display_, keycode, group, shift ? 1 : 0);
        const unsigned code = keysym_to_ucs(sym);
        if (code == 0) {
            return 0;
        }
        wchar_t ch = static_cast<wchar_t>(code);
        if (caps && is_letter(ch)) {
            ch = toggle_case(ch);
        }
        return ch;
    }

    [[nodiscard]] bool matches_hotkey(
        const Hotkey& hotkey,
        KeyCode keycode,
        bool ctrl,
        bool alt,
        bool shift,
        bool super) const {
        if (!hotkey.valid() || hotkey.ctrl != ctrl || hotkey.alt != alt ||
            hotkey.shift != shift || hotkey.super != super) {
            return false;
        }
        return XKeysymToKeycode(display_, hotkey.sym) == keycode;
    }

    void handle_delimiter(KeyCode keycode, bool shift, int group) {
        if (config_.enabled && config_.auto_switch && !word_.empty()) {
            convert_current(keycode, shift, group);
        } else {
            word_.clear();
        }
    }

    void convert_current(KeyCode terminator, bool terminator_shift, int group) {
        if (word_.empty() || !ready()) {
            word_.clear();
            return;
        }

        std::wstring typed;
        std::wstring converted;
        typed.reserve(word_.size());
        converted.reserve(word_.size());
        for (const Glyph& glyph : word_) {
            typed.push_back(glyph.ch);
            converted.push_back(glyph.alt);
        }

        Translator::Layout source = Translator::Layout::En;
        if (group == ru_group_) {
            source = Translator::Layout::Ru;
        } else if (group != en_group_) {
            source = Translator::infer_layout(typed);
        }

        if (!LayoutDetector::should_switch(typed, converted, source, config_.min_word_length)) {
            word_.clear();
            return;
        }

        pending_ = PendingConvert{
            word_,
            terminator,
            terminator_shift,
            (source == Translator::Layout::En) ? ru_group_ : en_group_,
        };
        word_.clear();

        if (terminator != 0) {
            // Wait for the delimiter to be released before injecting, otherwise
            // the retyped space/enter may be swallowed by the still-held key.
            wait_release_ = true;
            wait_release_key_ = terminator;
        } else if (context_ != 0) {
            XRecordDisableContext(display_, context_);
        }
    }

    void tap(KeyCode keycode, bool shift) {
        if (keycode == 0) {
            return;
        }
        if (shift && shift_key_ != 0) {
            XTestFakeKeyEvent(display_, shift_key_, True, CurrentTime);
        }
        XTestFakeKeyEvent(display_, keycode, True, CurrentTime);
        XTestFakeKeyEvent(display_, keycode, False, CurrentTime);
        if (shift && shift_key_ != 0) {
            XTestFakeKeyEvent(display_, shift_key_, False, CurrentTime);
        }
    }

    Display* display_ = nullptr;
    Config config_;
    int en_group_ = -1;
    int ru_group_ = -1;
    std::vector<Glyph> word_;
    std::optional<PendingConvert> pending_;
    XRecordContext context_ = 0;
    bool wait_release_ = false;
    KeyCode wait_release_key_ = 0;

    KeyCode backspace_key_ = 0;
    KeyCode return_key_ = 0;
    KeyCode tab_key_ = 0;
    KeyCode space_key_ = 0;
    KeyCode shift_key_ = 0;
    std::set<KeyCode> navigation_keys_;
};

void record_callback(XPointer closure, XRecordInterceptData* data) {
    if (data->category == XRecordFromServer && data->data != nullptr && data->data_len >= 32) {
        const auto* raw = reinterpret_cast<const unsigned char*>(data->data);
        const int type = raw[0] & 0x7F;
        if (type == KeyPress || type == KeyRelease) {
            auto* app = reinterpret_cast<X11App*>(closure);
            const KeyCode keycode = static_cast<KeyCode>(raw[1]);
            unsigned short state = 0;
            std::memcpy(&state, raw + 28, sizeof(state));
            app->handle_key(keycode, state, type == KeyPress);
        }
    }
    XRecordFreeData(data);
}

}  // namespace

int main() {
    std::signal(SIGPIPE, SIG_IGN);
    std::signal(SIGINT, [](int) { std::_Exit(0); });
    std::signal(SIGTERM, [](int) { std::_Exit(0); });

    Config config = load_config();
    if (!config.enabled) {
        std::fprintf(stderr, "Swaply: disabled in config\n");
        return 0;
    }

    LayoutDetector::load_bundled_dictionaries();
    LayoutDetector::set_user_words(config.extra_en, config.extra_ru);
    LayoutDetector::set_exceptions(config.exceptions);
    apply_autostart(config.start_with_windows);

    Display* control = XOpenDisplay(nullptr);
    if (control == nullptr) {
        std::fprintf(stderr, "Swaply: cannot open X display (X11 only)\n");
        return 1;
    }
    Display* data = XOpenDisplay(nullptr);
    if (data == nullptr) {
        std::fprintf(stderr, "Swaply: cannot open second X display\n");
        return 1;
    }

    int record_major = 0;
    int record_minor = 0;
    if (!XRecordQueryVersion(control, &record_major, &record_minor)) {
        std::fprintf(stderr, "Swaply: RECORD extension is not available\n");
        return 1;
    }

    int event_base = 0;
    int error_base = 0;
    int major = 0;
    int minor = 0;
    if (!XTestQueryExtension(control, &event_base, &error_base, &major, &minor)) {
        std::fprintf(stderr, "Swaply: XTEST extension is not available\n");
        return 1;
    }

    Bool supported = False;
    XkbSetDetectableAutoRepeat(control, True, &supported);

    X11App app(control, config);
    app.init_keys();
    app.detect_layouts();
    if (!app.ready()) {
        std::fprintf(stderr, "Swaply: both EN and RU keyboard layouts must be present in the XKB group list\n");
        return 1;
    }

    XRecordRange* range = XRecordAllocRange();
    if (range == nullptr) {
        std::fprintf(stderr, "Swaply: XRecordAllocRange failed\n");
        return 1;
    }
    range->device_events.first = KeyPress;
    range->device_events.last = KeyRelease;

    XRecordClientSpec clients = XRecordAllClients;
    XRecordContext context = XRecordCreateContext(control, 0, &clients, 1, &range, 1);
    XFree(range);
    if (context == 0) {
        std::fprintf(stderr, "Swaply: XRecordCreateContext failed\n");
        return 1;
    }
    XSync(control, False);
    app.set_context(context);

    std::fprintf(stderr, "Swaply %s (X11) started\n", SWAPLY_VERSION_STR);

    for (;;) {
        XRecordEnableContext(data, context, record_callback, reinterpret_cast<XPointer>(&app));
        app.run_pending();
    }
}
