#pragma once

#include <Windows.h>

#include <atomic>
#include <deque>
#include <mutex>

class HookManager {
public:
    static constexpr UINT queue_message = WM_APP + 2;
    static constexpr UINT reset_message = WM_APP + 3;

    struct KeyEvent {
        WPARAM wparam = 0;
        KBDLLHOOKSTRUCT info{};
        bool shift = false;
        bool ctrl = false;
        bool alt = false;
        bool win = false;
        bool caps = false;
    };

    explicit HookManager(HWND notify_window);
    ~HookManager();

    HookManager(const HookManager&) = delete;
    HookManager& operator=(const HookManager&) = delete;
    HookManager(HookManager&&) = delete;
    HookManager& operator=(HookManager&&) = delete;

    void uninstall() noexcept;
    void set_hotkeys(UINT convert_word, UINT convert_selection, UINT learn_word) noexcept;
    [[nodiscard]] bool try_pop(KeyEvent& out);

private:
    static LRESULT CALLBACK keyboard_proc(int code, WPARAM wparam, LPARAM lparam);
    static LRESULT CALLBACK mouse_proc(int code, WPARAM wparam, LPARAM lparam);

    void enqueue(const KeyEvent& event);
    [[nodiscard]] bool matches_hotkey(const KeyEvent& event) const noexcept;
    static KeyEvent snapshot(WPARAM wparam, const KBDLLHOOKSTRUCT& info) noexcept;

    static HookManager* instance_;

    HWND notify_window_;
    HHOOK keyboard_hook_ = nullptr;
    HHOOK mouse_hook_ = nullptr;
    std::atomic<UINT> convert_hotkey_{0};
    std::atomic<UINT> selection_hotkey_{0};
    std::atomic<UINT> learn_hotkey_{0};
    std::mutex mutex_;
    std::deque<KeyEvent> queue_;
};
