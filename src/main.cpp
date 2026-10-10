/* Swaply — свободный переключатель раскладки для Windows.
 * Copyright (C) 2026 Tap3ah
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "config.h"
#include "hook_manager.h"
#include "input_sim.h"
#include "layout_detector.h"
#include "resource.h"
#include "settings_dialog.h"
#include "text_tracker.h"
#include "translator.h"
#include "tray_icon.h"
#include "updater.h"
#include "version.h"

#include <algorithm>
#include <atomic>
#include <memory>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>
#include <utility>

#include <objbase.h>

namespace {

constexpr wchar_t kWindowClassName[] = L"Swaply.HiddenTrayWindow";
constexpr wchar_t kInstanceMutexName[] = L"Local\\Swaply.single_instance";
constexpr UINT update_check_message = WM_APP + 4;

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
            if (!already_running_) {
                ReleaseMutex(mutex_);
            }
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
            L"Swaply " SWAPLY_VERSION_STRW,
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

struct LastConversion {
    std::wstring original;
    std::wstring converted;
    Translator::Layout source = Translator::Layout::En;
    Translator::Layout target = Translator::Layout::Ru;
    UINT terminator = 0;
    HWND window = nullptr;
    bool valid = false;

    void clear() noexcept {
        original.clear();
        converted.clear();
        terminator = 0;
        window = nullptr;
        valid = false;
    }

    void invert() {
        std::swap(original, converted);
        std::swap(source, target);
    }

    [[nodiscard]] std::size_t delete_count() const noexcept {
        return converted.size() + (terminator != 0 ? 1 : 0);
    }
};

struct AppState {
    AppConfig* config = nullptr;
    TrayIcon* tray = nullptr;
    HookManager* hook = nullptr;
    TextTracker* tracker = nullptr;
    LastConversion last{};
    UINT taskbar_created = 0;
    HWND window = nullptr;
    std::atomic<bool> update_check_running{false};
};

void start_update_check(AppState& state, bool silent) {
    if (state.window == nullptr || state.update_check_running.exchange(true)) {
        return;
    }

    const HWND owner = state.window;
    std::thread([owner, silent]() {
        auto* info = new UpdateInfo(Updater::check());
        if (!PostMessageW(owner, update_check_message, silent ? 1 : 0, reinterpret_cast<LPARAM>(info))) {
            delete info;
        }
    }).detach();
}

void handle_update_result(AppState& state, const UpdateInfo& info, bool silent) {
    state.update_check_running.store(false);

    if (info.available) {
        const std::wstring text =
            L"Доступна новая версия Swaply " + info.version +
            L".\n\nСкачать и установить сейчас? Программа будет закрыта.";
        if (MessageBoxW(
                state.window, text.c_str(), L"Обновление Swaply",
                MB_YESNO | MB_ICONINFORMATION) == IDYES) {
            if (!Updater::download_and_run(info.url)) {
                MessageBoxW(
                    state.window, L"Не удалось скачать или запустить установщик.",
                    L"Swaply — ошибка", MB_OK | MB_ICONERROR);
            } else if (state.window != nullptr && IsWindow(state.window)) {
                DestroyWindow(state.window);
            }
        }
        return;
    }

    if (!silent) {
        const wchar_t* text = info.check_failed
                                  ? L"Не удалось проверить обновления. Проверьте подключение к интернету."
                                  : L"Установлена последняя версия Swaply.";
        MessageBoxW(state.window, text, L"Обновление Swaply", MB_OK | MB_ICONINFORMATION);
    }
}

void sync_tray(AppState& state) {
    if (state.tray != nullptr && state.config != nullptr) {
        state.tray->set_menu_state(
            state.config->enabled,
            state.config->auto_switch,
            state.config->start_with_windows,
            state.config->keep_after_enter_tab);
    }
}

void apply_runtime_config(AppState& state) {
    if (state.config == nullptr) {
        return;
    }

    LayoutDetector::set_user_words(state.config->extra_en, state.config->extra_ru);
    LayoutDetector::set_exceptions(state.config->exceptions);
    if (state.hook != nullptr) {
        state.hook->set_enabled(state.config->enabled);
        state.hook->set_eat_delimiters(state.config->enabled && state.config->auto_switch);
        state.hook->set_hotkeys(
            state.config->convert_word.pack(),
            state.config->convert_selection.pack(),
            state.config->learn_word.pack(),
            state.config->undo_conversion.pack());
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

void remember_conversion(
    AppState& state,
    std::wstring original,
    std::wstring converted,
    Translator::Layout source,
    UINT terminator,
    HWND window) {
    if (original.empty() || original == converted) {
        state.last.clear();
        return;
    }

    state.last.original = std::move(original);
    state.last.converted = std::move(converted);
    state.last.source = source;
    state.last.target = Translator::opposite(source);
    state.last.terminator = terminator;
    state.last.window = window;
    state.last.valid = true;
}

void undo_last_conversion(AppState& state) {
    if (!state.last.valid) {
        return;
    }

    const HWND target = state.last.window != nullptr ? state.last.window : GetForegroundWindow();
    if (target == nullptr || target != GetForegroundWindow()) {
        state.last.clear();
        return;
    }

    if (!InputSimulator::replace_text(
            target,
            state.last.delete_count(),
            state.last.original,
            state.last.terminator,
            state.last.source)) {
        state.last.clear();
        return;
    }

    state.last.invert();
    if (state.tracker != nullptr) {
        state.tracker->clear();
    }
}

void convert_current_word(AppState& state, bool auto_convert) {
    if (state.tracker == nullptr) {
        return;
    }

    TextTracker& tracker = *state.tracker;
    const std::wstring word = tracker.current_word();
    if (word.empty()) {
        return;
    }
    const HWND target_window = tracker.target_window();
    if (target_window == nullptr || target_window != GetForegroundWindow()) {
        tracker.clear();
        state.last.clear();
        return;
    }

    const std::wstring converted = tracker.converted_word();
    const auto source = tracker.source_layout();
    const auto target = Translator::opposite(source);
    const bool was_finalized = tracker.finalized();
    // A finalized word already has its delimiter in the document, so a manual
    // conversion must delete it too. An auto conversion at a delimiter had the
    // delimiter swallowed, so it only deletes the word itself.
    const UINT terminator = (auto_convert || was_finalized) ? tracker.terminator() : 0;
    const std::size_t delete_count = word.size() + ((!auto_convert && was_finalized) ? 1 : 0);

    if (converted != word) {
        if (!InputSimulator::replace_text(target_window, delete_count, converted, terminator, target)) {
            tracker.clear();
            state.last.clear();
            return;
        }
        remember_conversion(state, word, converted, source, terminator, target_window);
    } else {
        state.last.clear();
        InputSimulator::activate_layout(target);
        return;
    }

    if (terminator != 0) {
        // Word ended at a delimiter (auto) or was converted via the hotkey after
        // it: keep word + delimiter so the hotkey can keep toggling the layout.
        tracker.finalize(converted, terminator);
    } else {
        // Early prefix match: keep the word so the rest can be typed in the
        // already switched layout.
        tracker.assign_converted(converted);
    }
}

void drain_key_queue(AppState& state) {
    if (state.hook == nullptr || state.tracker == nullptr) {
        return;
    }

    state.hook->begin_drain();
    bool deferred_auto_convert = false;
    HookManager::KeyEvent event{};
    const auto is_swallowed_delimiter = [](const HookManager::KeyEvent& key) {
        if (key.wparam != WM_KEYDOWN && key.wparam != WM_SYSKEYDOWN) {
            return false;
        }
        if (key.ctrl || key.alt || key.win || key.shift) {
            return false;
        }
        const UINT vk = key.info.vkCode;
        return vk == VK_SPACE || vk == VK_RETURN || vk == VK_TAB;
    };

    while (state.hook->try_pop(event)) {
        const auto action = state.tracker->on_key(event);
        switch (action) {
        case TextTracker::Action::ConvertWord:
            convert_current_word(state, false);
            break;
        case TextTracker::Action::AutoConvert:
            if (state.tracker->terminator() == 0 && state.hook->has_pending_events()) {
                deferred_auto_convert = true;
            } else {
                convert_current_word(state, true);
                deferred_auto_convert = false;
            }
            break;
        case TextTracker::Action::ConvertSelection:
            InputSimulator::convert_selection();
            state.last.clear();
            if (state.tracker != nullptr) {
                state.tracker->clear();
            }
            break;
        case TextTracker::Action::LearnWord:
            learn_current_word(state);
            break;
        case TextTracker::Action::Undo:
            if (state.last.valid) {
                undo_last_conversion(state);
            } else {
                InputSimulator::convert_selection();
                state.last.clear();
                if (state.tracker != nullptr) {
                    state.tracker->clear();
                }
            }
            break;
        case TextTracker::Action::DiscardUndo:
            state.last.clear();
            break;
        case TextTracker::Action::None:
            break;
        }
        if (is_swallowed_delimiter(event) && action != TextTracker::Action::AutoConvert &&
            state.config != nullptr && state.config->enabled && state.config->auto_switch) {
            InputSimulator::send_virtual_key(event.info.vkCode);
        }
    }

    if (deferred_auto_convert && state.tracker->should_auto_convert()) {
        convert_current_word(state, true);
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
        if (state.hook != nullptr) {
            state.hook->set_enabled(state.config->enabled);
            state.hook->set_eat_delimiters(state.config->enabled && state.config->auto_switch);
        }
        sync_tray(state);
        if (state.tracker != nullptr) {
            state.tracker->clear();
        }
        state.last.clear();
        return true;
    case TrayIcon::auto_switch_command_id:
        state.config->auto_switch = !state.config->auto_switch;
        state.config->save();
        if (state.hook != nullptr) {
            state.hook->set_eat_delimiters(state.config->enabled && state.config->auto_switch);
        }
        sync_tray(state);
        return true;
    case TrayIcon::enter_tab_command_id:
        state.config->keep_after_enter_tab = !state.config->keep_after_enter_tab;
        state.config->save();
        sync_tray(state);
        return true;
    case TrayIcon::undo_command_id:
        undo_last_conversion(state);
        return true;
    case TrayIcon::about_command_id:
        show_about_dialog(hwnd);
        return true;
    case TrayIcon::update_command_id:
        start_update_check(state, false);
        return true;
    case TrayIcon::settings_command_id:
        if (show_settings_dialog(hwnd, *state.config)) {
            state.config->save();
            state.config->apply_autostart();
            apply_runtime_config(state);
            if (state.tracker != nullptr) {
                state.tracker->clear();
            }
            state.last.clear();
        }
        return true;
    case TrayIcon::autostart_command_id:
        if (is_portable_install()) {
            return true;
        }
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

        if (msg == update_check_message) {
            const std::unique_ptr<UpdateInfo> info(reinterpret_cast<UpdateInfo*>(lparam));
            if (info != nullptr) {
                handle_update_result(*state, *info, wparam != 0);
            }
            return 0;
        }

        if (msg == HookManager::reset_message) {
            if (state->tracker != nullptr) {
                state->tracker->clear();
            }
            state->last.clear();
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
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
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
    LayoutDetector::load_bundled_dictionaries();
    LayoutDetector::set_user_words(config.extra_en, config.extra_ru);
    LayoutDetector::set_exceptions(config.exceptions);

    WindowClass window_class(instance, hidden_wnd_proc);
    HiddenWindow window(instance, kWindowClassName);

    TrayIcon tray(instance, window.get());
    HookManager hook(window.get());
    TextTracker tracker(config);

    AppState state{};
    state.config = &config;
    state.tray = &tray;
    state.hook = &hook;
    state.tracker = &tracker;
    state.window = window.get();
    state.taskbar_created = RegisterWindowMessageW(L"TaskbarCreated");
    InputSimulator::restore_system_layouts();
    apply_runtime_config(state);
    SetWindowLongPtrW(window.get(), GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&state));

    if (config.check_updates) {
        start_update_check(state, true);
    }

    MSG msg{};
    while (true) {
        const BOOL result = GetMessageW(&msg, nullptr, 0, 0);
        if (result == 0) {
            break;
        }
        if (result == -1) {
            SetWindowLongPtrW(window.get(), GWLP_USERDATA, 0);
            if (IsWindow(window.get())) {
                DestroyWindow(window.get());
            }
            return 1;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    return static_cast<int>(msg.wParam);
}

}  // namespace

class ComInit {
public:
    ComInit() {
        const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        initialized_ = hr == S_OK || hr == S_FALSE;
    }

    ~ComInit() {
        if (initialized_) {
            CoUninitialize();
        }
    }

    ComInit(const ComInit&) = delete;
    ComInit& operator=(const ComInit&) = delete;

private:
    bool initialized_ = false;
};

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    try {
        ComInit com;
        return run(instance);
    } catch (const std::exception& ex) {
        const std::string what = ex.what();
        const std::wstring message(what.begin(), what.end());
        MessageBoxW(nullptr, message.c_str(), L"Swaply — ошибка", MB_OK | MB_ICONERROR);
        return 1;
    }
}
