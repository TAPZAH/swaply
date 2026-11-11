// Swaply for Linux/X11 — global layout switcher.
// Copyright (C) 2026 Tap3ah. GNU GPL v3+.
//
// X11 backend: records global key events with XRecord, recognises words typed
// in the wrong EN/RU layout and retypes them with XTest while switching the
// XKB group. Wayland is not supported.

#include "config_linux.h"
#include "layout_detector.h"
#include "version.h"

#include <X11/XKBlib.h>
#include <X11/Xlib.h>
#include <X11/extensions/XTest.h>
#include <X11/extensions/record.h>
#include <X11/keysym.h>

#include <gio/gio.h>

#include <atomic>
#include <cctype>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <thread>
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
    // Legacy Cyrillic keysyms (0x06c0–0x06ff) used by xkeyboard-config for the
    // `ru` layout are not in Unicode order: 0x06c1=а, 0x06c6=ф and so on.
    if (sym >= 0x06C0 && sym <= 0x06FF) {
        static const unsigned short kCyrillicLower[32] = {
            0x044E,  // ю
            0x0430,  // а
            0x0431,  // б
            0x0446,  // ц
            0x0434,  // д
            0x0435,  // е
            0x0444,  // ф
            0x0433,  // г
            0x0445,  // х
            0x0438,  // и
            0x0439,  // й
            0x043A,  // к
            0x043B,  // л
            0x043C,  // м
            0x043D,  // н
            0x043E,  // о
            0x043F,  // п
            0x044F,  // я
            0x0440,  // р
            0x0441,  // с
            0x0442,  // т
            0x0443,  // у
            0x0436,  // ж
            0x0432,  // в
            0x044C,  // ь
            0x044B,  // ы
            0x0437,  // з
            0x0448,  // ш
            0x044D,  // э
            0x0449,  // щ
            0x0447,  // ч
            0x044A,  // ъ
        };
        const unsigned index = static_cast<unsigned>(sym - 0x06C0) & 31u;
        const unsigned code = kCyrillicLower[index];
        return (sym >= 0x06E0) ? code - 0x20u : code;
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
    int group = -1;
    bool caps = false;
};

struct PendingConvert {
    std::vector<Glyph> glyphs;
    KeyCode terminator = 0;
    bool terminator_shift = false;
    int target_group = 0;
};

class X11App {
public:
    X11App(Display* display, LinuxConfig config)
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

    // Cinnamon (and GNOME) revert XkbLockGroup performed by foreign clients: the
    // shell keeps its own notion of the current input source and re-locks the
    // group whenever an external change is detected.  When the shell exposes
    // org.Cinnamon we switch layouts through its D-Bus API instead, so the
    // change is accepted (and the panel indicator follows along).
    void init_session_activator() {
        bus_ = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, nullptr);
        if (bus_ == nullptr) {
            return;
        }
        GError* error = nullptr;
        GVariant* reply = g_dbus_connection_call_sync(
            bus_, "org.Cinnamon", "/org/Cinnamon", "org.Cinnamon", "GetInputSources",
            nullptr, G_VARIANT_TYPE("(a(ssisssssssib))"),
            G_DBUS_CALL_FLAGS_NONE, 500, nullptr, &error);
        if (error != nullptr) {
            g_error_free(error);
            return;
        }
        GVariant* list = g_variant_get_child_value(reply, 0);
        const gsize count = g_variant_n_children(list);
        for (gsize i = 0; i < count; ++i) {
            GVariant* item = g_variant_get_child_value(list, i);
            gchar* backend = nullptr;
            gchar* id = nullptr;
            gint index = -1;
            g_variant_get_child(item, 0, "s", &backend);
            g_variant_get_child(item, 1, "s", &id);
            g_variant_get_child(item, 2, "i", &index);
            if (std::strcmp(backend, "xkb") == 0 && index >= 0 && index < 4) {
                xkb_source_ids_[index] = id;
            }
            g_free(backend);
            g_free(id);
            g_variant_unref(item);
        }
        g_variant_unref(list);
        g_variant_unref(reply);
    }

    // Called after detect_layouts(): use the D-Bus path only when the shell's
    // source ordering matches the XKB groups we probed.
    void enable_session_activator() {
        const auto en = xkb_source_ids_.find(en_group_);
        const auto ru = xkb_source_ids_.find(ru_group_);
        if (en == xkb_source_ids_.end() || ru == xkb_source_ids_.end()) {
            return;
        }
        const std::string& en_id = en->second;
        const std::string& ru_id = ru->second;
        const bool en_ok = en_id.rfind("us", 0) == 0 || en_id.rfind("en", 0) == 0 || en_id.rfind("gb", 0) == 0;
        const bool ru_ok = ru_id.rfind("ru", 0) == 0;
        use_cinnamon_ = en_ok && ru_ok;
        if (use_cinnamon_) {
            std::fprintf(stderr, "Swaply: Cinnamon shell detected, switching via D-Bus\n");
        }
    }

    void set_context(XRecordContext context) noexcept { context_ = context; }

    void apply_config(const LinuxConfig& config) {
        std::lock_guard<std::mutex> lock(mutex_);
        config_ = config;
        hotkey_ = parse_hotkey(config_.convert_word_hotkey);
        LayoutDetector::set_user_words(config_.extra_en, config_.extra_ru);
        LayoutDetector::set_exceptions(config_.exceptions);
        apply_autostart(config_.start_with_windows);
    }

    void handle_key(KeyCode keycode, unsigned int state, bool pressed) {
        if (!pressed) {
            return;
        }
        if (pending_.has_value()) {
            return;
        }

        std::lock_guard<std::mutex> lock(mutex_);
        if (!config_.enabled) {
            word_.clear();
            return;
        }

        const bool ctrl = (state & ControlMask) != 0;
        const bool shift = (state & ShiftMask) != 0;
        const bool alt = (state & Mod1Mask) != 0;
        const bool super = (state & Mod4Mask) != 0;
        const bool caps = (state & LockMask) != 0;
        const int group = XkbGroupForCoreState(state);

        if (matches_hotkey(hotkey_, keycode, ctrl, alt, shift, super)) {
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
            word_.push_back(Glyph{keycode, shift, ch, alt_char, group, caps});
        }
    }

    void lock_group(int group) {
        // Lock the XKB group immediately: this is a single server request, so
        // the whole retype pipeline stays within ~1 ms and cannot interleave
        // with fast typing in the middle of the next word.
        XkbLockGroup(display_, XkbUseCoreKbd, static_cast<unsigned>(group));
        if (use_cinnamon_) {
            // Tell the shell to adopt the group so that its layout keeper does
            // not revert it.  Fire-and-forget: waiting for the D-Bus reply (or
            // polling the group) would stall the retype for tens of ms and
            // corrupt words typed during the stall.  The shell's revert (if it
            // races) is overwritten by this activation a few ms later.
            g_dbus_connection_call(
                bus_, "org.Cinnamon", "/org/Cinnamon", "org.Cinnamon",
                "ActivateInputSourceIndex", g_variant_new("(i)", group),
                nullptr, G_DBUS_CALL_FLAGS_NONE, -1, nullptr, nullptr, nullptr);
        }
    }

    void run_pending() {
        if (!pending_.has_value()) {
            return;
        }
        const PendingConvert pending = std::move(*pending_);
        pending_.reset();

        // The user is usually still holding the terminator key (space, Enter).
        // The X server suppresses a KeyPress for a key it considers already
        // down, so release the logical state first; the fake press then reaches
        // the application and the pending physical release becomes a no-op.
        // Queried upfront so the whole conversion below is one uninterrupted
        // request burst: no hardware key can be processed in the middle of it.
        if (pending.terminator != 0) {
            release_if_down(pending.terminator);
        }

        const std::size_t deletes = pending.glyphs.size() + (pending.terminator != 0 ? 1 : 0);
        for (std::size_t i = 0; i < deletes; ++i) {
            tap(backspace_key_, false);
        }

        lock_group(pending.target_group);

        for (const Glyph& glyph : pending.glyphs) {
            tap(glyph.keycode, glyph.shift);
        }
        if (pending.terminator != 0) {
            tap(pending.terminator, pending.terminator_shift);
        }
        // The context is disabled, but the fake events travel on the control
        // connection while the next XRecordEnableContext goes on the data
        // connection.  XSync flushes the burst and makes sure the server has
        // processed every fake event before recording is switched back on,
        // so the daemon never sees (or deletes) its own keystrokes.
        XSync(display_, False);
    }

private:
    [[nodiscard]] int opposite_group(int group) const noexcept {
        return group == ru_group_ ? en_group_ : ru_group_;
    }

    [[nodiscard]] Translator::Layout layout_of(int group, const std::wstring& typed) const {
        if (group == ru_group_) {
            return Translator::Layout::Ru;
        }
        if (group == en_group_) {
            return Translator::Layout::En;
        }
        return Translator::infer_layout(typed);
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
        if (config_.auto_switch && !word_.empty()) {
            convert_current(keycode, shift, group);
        } else {
            word_.clear();
        }
    }

    // Reading of the whole keystroke stream as if every key had been typed
    // in the given group.
    [[nodiscard]] std::wstring stream_reading(const std::vector<Glyph>& glyphs, int group) const {
        std::wstring reading;
        reading.reserve(glyphs.size());
        for (const Glyph& glyph : glyphs) {
            wchar_t ch = char_for(glyph.keycode, group, glyph.shift, glyph.caps);
            if (ch == 0) {
                ch = glyph.ch;  // unmapped in that group: keep as typed
            }
            reading.push_back(ch);
        }
        return reading;
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

        // Normal case: the word was typed in one layout — try its opposite.
        // Mixed case: the user switched layout in the middle of the word (a
        // hybrid like "gj" + "дними"). Then re-reading the whole keystroke
        // stream in the current layout usually restores the intended word,
        // so that reading is tried first (target = same layout, only the text
        // is retyped), and the opposite one second.
        struct Candidate {
            std::wstring reading;
            int target_group = -1;
            Translator::Layout source = Translator::Layout::En;
        };
        std::vector<Candidate> candidates;
        int last_group = group;
        for (const Glyph& glyph : word_) {
            if (glyph.group >= 0) {
                last_group = glyph.group;
            }
        }
        const int primary = (last_group == en_group_ || last_group == ru_group_)
                                ? opposite_group(last_group)
                                : ru_group_;
        candidates.push_back(Candidate{converted, primary, layout_of(last_group, typed)});
        if (last_group == en_group_ || last_group == ru_group_) {
            const std::wstring same_group_reading = stream_reading(word_, last_group);
            if (same_group_reading != typed) {
                candidates.push_back(
                    Candidate{same_group_reading, last_group, Translator::opposite(layout_of(last_group, typed))});
            }
        }

        for (const Candidate& candidate : candidates) {
            if (candidate.target_group < 0 || candidate.reading == typed) {
                continue;
            }
            if (!LayoutDetector::should_switch(
                    typed, candidate.reading, candidate.source, config_.min_word_length)) {
                continue;
            }
            pending_ = PendingConvert{
                word_,
                terminator,
                terminator_shift,
                candidate.target_group,
            };
            word_.clear();
            if (context_ != 0) {
                XRecordDisableContext(display_, context_);
                // Must flush the control connection: XRecordEnableContext on the
                // data connection blocks until the server sees the disable, and
                // nothing else will ever flush this connection in the meantime.
                XFlush(display_);
            }
            return;
        }
        word_.clear();
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

    void release_if_down(KeyCode keycode) {
        unsigned char keys[32] = {};
        XQueryKeymap(display_, reinterpret_cast<char*>(keys));
        const bool down = (keys[keycode / 8] & (1u << (keycode % 8))) != 0;
        if (down) {
            XTestFakeKeyEvent(display_, keycode, False, CurrentTime);
        }
    }

    Display* display_ = nullptr;
    LinuxConfig config_;
    Hotkey hotkey_;
    mutable std::mutex mutex_;
    int en_group_ = -1;
    int ru_group_ = -1;
    GDBusConnection* bus_ = nullptr;
    bool use_cinnamon_ = false;
    std::map<int, std::string> xkb_source_ids_;
    std::vector<Glyph> word_;
    std::optional<PendingConvert> pending_;
    XRecordContext context_ = 0;

    KeyCode backspace_key_ = 0;
    KeyCode return_key_ = 0;
    KeyCode tab_key_ = 0;
    KeyCode space_key_ = 0;
    KeyCode shift_key_ = 0;
    std::set<KeyCode> navigation_keys_;
};

void record_callback(XPointer closure, XRecordInterceptData* data) {
    // data_len is counted in 4-byte units; a core key event is 32 bytes = 8 units.
    if (data->category == XRecordFromServer && data->data != nullptr && data->data_len >= 8) {
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

void write_pid_file() {
    std::error_code error;
    std::filesystem::create_directories(config_directory(), error);
    std::ofstream out(config_directory() + "/swaply.pid", std::ios::trunc);
    if (out) {
        out << static_cast<long>(getpid());
    }
}

void watch_config(X11App& app) {
    std::error_code error;
    auto last = std::filesystem::last_write_time(config_path(), error);
    for (;;) {
        std::this_thread::sleep_for(std::chrono::seconds(2));
        std::error_code current_error;
        const auto current = std::filesystem::last_write_time(config_path(), current_error);
        if (current_error) {
            continue;
        }
        if (current != last) {
            last = current;
            app.apply_config(load_config());
        }
    }
}

}  // namespace

int main() {
    std::signal(SIGPIPE, SIG_IGN);
    std::signal(SIGINT, [](int) { std::_Exit(0); });
    std::signal(SIGTERM, [](int) { std::_Exit(0); });

    LinuxConfig config = load_config();

    LayoutDetector::load_bundled_dictionaries();
    LayoutDetector::set_user_words(config.extra_en, config.extra_ru);
    LayoutDetector::set_exceptions(config.exceptions);
    apply_autostart(config.start_with_windows);
    write_pid_file();

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
    app.init_session_activator();
    app.enable_session_activator();
    app.apply_config(config);

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

    std::thread(watch_config, std::ref(app)).detach();

    std::fprintf(stderr, "Swaply %s (X11) started\n", SWAPLY_VERSION_STR);

    for (;;) {
        XRecordEnableContext(data, context, record_callback, reinterpret_cast<XPointer>(&app));
        app.run_pending();
    }
}
