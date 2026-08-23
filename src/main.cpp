#include "config.h"
#include "hook_manager.h"
#include "input_sim.h"
#include "layout_detector.h"
#include "resource.h"
#include "settings_dialog.h"
#include "text_tracker.h"
#include "translator.h"
#include "tray_icon.h"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <system_error>

namespace {

constexpr wchar_t kWindowClassName[] = L"wxneur.HiddenTrayWindow";
constexpr wchar_t kInstanceMutexName[] = L"Local\\wxneur.single_instance";

[[noreturn]] void throw_last_error(const char* what) {
    throw std::system_error(
        static_cast<int>(GetLastError()),
        std::system_category(),
        what);
}

class SingleInstance {
public:
    SingleInstance() {
        mutex_ = CreateMutexW(nullptr, TRUE, kInstanceMutexName);
        already_running_ = mutex_ != nullptr && GetLastError() == ERROR_ALREADY_EXISTS;
    }

    ~SingleInstance() {
        if (mutex_ != nullptr) {
            ReleaseMutex(mutex_);
            CloseHandle(mutex_);
        }
    }

    SingleInstance(const SingleInstance&) = delete;
    SingleInstance& operator=(const SingleInstance&) = delete;

    [[nodiscard]] bool already_running() const noexcept { return already_running_; }

private:
    HANDLE mutex_ = nullptr;
    bool already_running_ = false;
};

class WindowClass {
public:
    WindowClass(HINSTANCE instance, WNDPROC wnd_proc) : instance_(instance) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = wnd_proc;
        wc.hInstance = instance_;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hIcon = LoadIconW(instance_, MAKEINTRESOURCEW(IDI_APPICON));
        if (wc.hIcon == nullptr) {
            wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
        }
        wc.lpszClassName = kWindowClassName;

        if (RegisterClassExW(&wc) == 0) {
            throw_last_error("RegisterClassExW failed");
        }
    }

    ~WindowClass() {
        UnregisterClassW(kWindowClassName, instance_);
    }

    WindowClass(const WindowClass&) = delete;
    WindowClass& operator=(const WindowClass&) = delete;

private:
    HINSTANCE instance_;
};

class HiddenWindow {
public:
    HiddenWindow(HINSTANCE instance, const wchar_t* class_name) {
        hwnd_ = CreateWindowExW(
            WS_EX_TOOLWINDOW,
            class_name,
            L"wxneur",
            WS_POPUP,
            0,
            0,
            0,
            0,
            nullptr,
            nullptr,
            instance,
            nullptr);

        if (hwnd_ == nullptr) {
            throw_last_error("CreateWindowExW failed");
        }
    }

    ~HiddenWindow() {
        if (hwnd_ != nullptr && IsWindow(hwnd_)) {
            DestroyWindow(hwnd_);
        }
    }

    HiddenWindow(const HiddenWindow&) = delete;
    HiddenWindow& operator=(const HiddenWindow&) = delete;

    [[nodiscard]] HWND get() const noexcept { return hwnd_; }

private:
    HWND hwnd_ = nullptr;
};

struct AppState {
    AppConfig* config = nullptr;
    TrayIcon* tray = nullptr;
    HookManager* hook = nullptr;
    TextTracker* tracker = nullptr;
    UINT taskbar_created = 0;
};

void sync_tray(AppState& state) {
    if (state.tray != nullptr && state.config != nullptr) {
        state.tray->set_menu_state(
            state.config->enabled,
            state.config->auto_switch,
            state.config->start_with_windows);
    }
}

void apply_runtime_config(AppState& state) {
    if (state.config == nullptr) {
        return;
    }

    LayoutDetector::set_user_words(state.config->extra_en, state.config->extra_ru);
    LayoutDetector::set_exceptions(state.config->exceptions);
    if (state.hook != nullptr) {
        state.hook->set_hotkeys(
            state.config->convert_word.pack(),
            state.config->convert_selection.pack(),
            state.config->learn_word.pack());
    }
    sync_tray(state);
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

void learn_current_word(AppState& state) {
    if (state.config == nullptr || state.tracker == nullptr || state.tracker->empty()) {
        return;
    }

    const std::string word = wide_to_utf8(state.tracker->current_word());
    if (word.empty()) {
        return;
    }

    auto& extra = (state.tracker->source_layout() == Translator::Layout::Ru)
                      ? state.config->extra_ru
                      : state.config->extra_en;
    if (std::find(extra.begin(), extra.end(), word) == extra.end()) {
        extra.push_back(word);
        if (std::find(state.config->exceptions.begin(), state.config->exceptions.end(), word) ==
            state.config->exceptions.end()) {
            state.config->exceptions.push_back(word);
        }
        state.config->save();
        LayoutDetector::set_user_words(state.config->extra_en, state.config->extra_ru);
        LayoutDetector::set_exceptions(state.config->exceptions);
    }
    state.tracker->clear();
    MessageBeep(MB_OK);
}

void convert_current_word(TextTracker& tracker, bool auto_convert) {
    const std::wstring word = tracker.current_word();
    if (word.empty()) {
        return;
    }

    const std::wstring converted = tracker.converted_word();
    const auto target = Translator::opposite(tracker.source_layout());
    const UINT terminator = auto_convert ? tracker.terminator() : 0;
    const std::size_t delete_count = word.size() + (terminator != 0 ? 1 : 0);

    if (converted != word) {
        if (terminator != 0) {
            Sleep(15);
        }
        if (!InputSimulator::replace_text(delete_count, converted, terminator)) {
            tracker.clear();
            return;
        }
    }

    if (auto_convert) {
        tracker.clear();
    } else {
        tracker.assign_converted(converted);
    }

    InputSimulator::activate_layout(target);
}

void drain_key_queue(AppState& state) {
    if (state.hook == nullptr || state.tracker == nullptr) {
        return;
    }

    HookManager::KeyEvent event{};
    while (state.hook->try_pop(event)) {
        switch (state.tracker->on_key(event)) {
        case TextTracker::Action::ConvertWord:
            convert_current_word(*state.tracker, false);
            break;
        case TextTracker::Action::AutoConvert:
            convert_current_word(*state.tracker, true);
            break;
        case TextTracker::Action::ConvertSelection:
            InputSimulator::convert_selection();
            if (state.tracker != nullptr) {
                state.tracker->clear();
            }
            break;
        case TextTracker::Action::LearnWord:
            learn_current_word(state);
            break;
        case TextTracker::Action::None:
            break;
        }
    }
}

bool handle_command(AppState& state, HWND hwnd, WPARAM wparam) {
    if (state.config == nullptr) {
        return false;
    }

    switch (LOWORD(wparam)) {
    case TrayIcon::enabled_command_id:
        state.config->enabled = !state.config->enabled;
        state.config->save();
        sync_tray(state);
        if (state.tracker != nullptr) {
            state.tracker->clear();
        }
        return true;
    case TrayIcon::auto_switch_command_id:
        state.config->auto_switch = !state.config->auto_switch;
        state.config->save();
        sync_tray(state);
        return true;
    case TrayIcon::settings_command_id:
        if (show_settings_dialog(hwnd, *state.config)) {
            state.config->save();
            state.config->apply_autostart();
            apply_runtime_config(state);
            if (state.tracker != nullptr) {
                state.tracker->clear();
            }
        }
        return true;
    case TrayIcon::autostart_command_id:
        state.config->start_with_windows = !state.config->start_with_windows;
        state.config->apply_autostart();
        state.config->save();
        sync_tray(state);
        return true;
    case TrayIcon::exit_command_id:
        DestroyWindow(hwnd);
        return true;
    default:
        return false;
    }
}

LRESULT CALLBACK hidden_wnd_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    auto* state = reinterpret_cast<AppState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    if (state != nullptr) {
        if (msg == HookManager::queue_message) {
            drain_key_queue(*state);
            return 0;
        }

        if (msg == HookManager::reset_message) {
            if (state->tracker != nullptr) {
                state->tracker->clear();
            }
            return 0;
        }

        if (msg == WM_COMMAND && handle_command(*state, hwnd, wparam)) {
            return 0;
        }

        if (state->taskbar_created != 0 && msg == state->taskbar_created) {
            if (state->tray != nullptr) {
                state->tray->restore_after_explorer_restart();
            }
            return 0;
        }

        if (state->tray != nullptr && state->tray->handle_message(msg, wparam, lparam)) {
            return 0;
        }
    }

    if (msg == WM_DESTROY) {
        if (state != nullptr && state->hook != nullptr) {
            state->hook->uninstall();
        }
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

int run(HINSTANCE instance) {
    SingleInstance instance_guard;
    if (instance_guard.already_running()) {
        return 0;
    }

    AppConfig config = AppConfig::load();
    LayoutDetector::set_user_words(config.extra_en, config.extra_ru);
    LayoutDetector::set_exceptions(config.exceptions);

    WindowClass window_class(instance, hidden_wnd_proc);
    HiddenWindow window(instance, kWindowClassName);

    TrayIcon tray(instance, window.get());
    HookManager hook(window.get());
    hook.set_hotkeys(config.convert_word.pack(), config.convert_selection.pack(), config.learn_word.pack());
    TextTracker tracker(config);

    AppState state{};
    state.config = &config;
    state.tray = &tray;
    state.hook = &hook;
    state.tracker = &tracker;
    state.taskbar_created = RegisterWindowMessageW(L"TaskbarCreated");
    sync_tray(state);
    SetWindowLongPtrW(window.get(), GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&state));

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    return static_cast<int>(msg.wParam);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    try {
        return run(instance);
    } catch (const std::exception& ex) {
        const std::string what = ex.what();
        const std::wstring message(what.begin(), what.end());
        MessageBoxW(nullptr, message.c_str(), L"wxneur — ошибка", MB_OK | MB_ICONERROR);
        return 1;
    }
}
