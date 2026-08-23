#include "hook_manager.h"

#include "hotkey.h"

#include <cstddef>
#include <stdexcept>
#include <system_error>

namespace {

constexpr std::size_t kMaxQueuedEvents = 256;

[[noreturn]] void throw_last_error(const char* what) {
    throw std::system_error(
        static_cast<int>(GetLastError()),
        std::system_category(),
        what);
}

void close_hook(HHOOK& hook) noexcept {
    if (hook != nullptr) {
        UnhookWindowsHookEx(hook);
        hook = nullptr;
    }
}

[[nodiscard]] bool is_injected_key(const KBDLLHOOKSTRUCT& info) noexcept {
    return (info.flags & (LLKHF_INJECTED | LLKHF_LOWER_IL_INJECTED)) != 0;
}

[[nodiscard]] bool is_injected_mouse(const MSLLHOOKSTRUCT& info) noexcept {
    return (info.flags & (LLMHF_INJECTED | LLMHF_LOWER_IL_INJECTED)) != 0;
}

}  // namespace

HookManager* HookManager::instance_ = nullptr;

HookManager::HookManager(HWND notify_window) : notify_window_(notify_window) {
    if (notify_window_ == nullptr) {
        throw std::invalid_argument("HookManager requires a notify window");
    }

    instance_ = this;

    const HMODULE module = GetModuleHandleW(nullptr);
    keyboard_hook_ = SetWindowsHookExW(WH_KEYBOARD_LL, keyboard_proc, module, 0);
    if (keyboard_hook_ == nullptr) {
        instance_ = nullptr;
        throw_last_error("SetWindowsHookExW(WH_KEYBOARD_LL) failed");
    }

    mouse_hook_ = SetWindowsHookExW(WH_MOUSE_LL, mouse_proc, module, 0);
    if (mouse_hook_ == nullptr) {
        close_hook(keyboard_hook_);
        instance_ = nullptr;
        throw_last_error("SetWindowsHookExW(WH_MOUSE_LL) failed");
    }
}

HookManager::~HookManager() {
    uninstall();
}

void HookManager::set_enabled(bool enabled) noexcept {
    enabled_.store(enabled, std::memory_order_relaxed);
}

void HookManager::set_hotkeys(UINT convert_word, UINT convert_selection, UINT learn_word, UINT undo) noexcept {
    convert_hotkey_.store(convert_word, std::memory_order_relaxed);
    selection_hotkey_.store(convert_selection, std::memory_order_relaxed);
    learn_hotkey_.store(learn_word, std::memory_order_relaxed);
    undo_hotkey_.store(undo, std::memory_order_relaxed);
}

bool HookManager::matches_hotkey(const KeyEvent& event) const noexcept {
    const UINT vk = event.info.vkCode;
    const auto matches_packed = [&](UINT packed) {
        return Hotkey::unpack(packed).matches(vk, event.ctrl, event.alt, event.shift, event.win);
    };
    return matches_packed(convert_hotkey_.load(std::memory_order_relaxed)) ||
           matches_packed(selection_hotkey_.load(std::memory_order_relaxed)) ||
           matches_packed(learn_hotkey_.load(std::memory_order_relaxed)) ||
           matches_packed(undo_hotkey_.load(std::memory_order_relaxed));
}

void HookManager::uninstall() noexcept {
    close_hook(mouse_hook_);
    close_hook(keyboard_hook_);
    if (instance_ == this) {
        instance_ = nullptr;
    }
}

bool HookManager::try_pop(KeyEvent& out) {
    std::lock_guard lock(mutex_);
    if (queue_.empty()) {
        return false;
    }

    out = queue_.front();
    queue_.pop_front();
    return true;
}

void HookManager::begin_drain() noexcept {
    std::lock_guard lock(mutex_);
    notification_pending_ = false;
}

bool HookManager::has_pending_events() const {
    std::lock_guard lock(mutex_);
    return !queue_.empty();
}

void HookManager::enqueue(const KeyEvent& event) {
    bool notify = false;
    {
        std::lock_guard lock(mutex_);
        if (queue_.size() >= kMaxQueuedEvents) {
            queue_.pop_front();
        }
        queue_.push_back(event);
        if (!notification_pending_) {
            notification_pending_ = true;
            notify = true;
        }
    }

    if (notify && !PostMessageW(notify_window_, queue_message, 0, 0)) {
        std::lock_guard lock(mutex_);
        notification_pending_ = false;
    }
}

HookManager::KeyEvent HookManager::snapshot(WPARAM wparam, const KBDLLHOOKSTRUCT& info) noexcept {
    KeyEvent event{};
    event.wparam = wparam;
    event.info = info;
    event.target_window = GetForegroundWindow();
    if (event.target_window != nullptr) {
        event.target_thread = GetWindowThreadProcessId(event.target_window, nullptr);
    }
    event.layout = GetKeyboardLayout(event.target_thread);
    event.shift = GetKeyState(VK_SHIFT) < 0;
    event.ctrl = GetKeyState(VK_CONTROL) < 0;
    event.alt = GetKeyState(VK_MENU) < 0;
    event.win = GetKeyState(VK_LWIN) < 0 || GetKeyState(VK_RWIN) < 0;
    event.caps = (GetKeyState(VK_CAPITAL) & 1) != 0;
    return event;
}

LRESULT CALLBACK HookManager::keyboard_proc(int code, WPARAM wparam, LPARAM lparam) {
    if (code == HC_ACTION && instance_ != nullptr && lparam != 0) {
        const auto& info = *reinterpret_cast<const KBDLLHOOKSTRUCT*>(lparam);
        if (!is_injected_key(info)) {
            const KeyEvent event = snapshot(wparam, info);
            instance_->enqueue(event);
            if (instance_->enabled_.load(std::memory_order_relaxed) && instance_->matches_hotkey(event)) {
                return 1;
            }
        }
    }

    return CallNextHookEx(nullptr, code, wparam, lparam);
}

LRESULT CALLBACK HookManager::mouse_proc(int code, WPARAM wparam, LPARAM lparam) {
    if (code == HC_ACTION && instance_ != nullptr && lparam != 0) {
        const auto& info = *reinterpret_cast<const MSLLHOOKSTRUCT*>(lparam);
        if (!is_injected_mouse(info)) {
            switch (wparam) {
            case WM_LBUTTONDOWN:
            case WM_RBUTTONDOWN:
            case WM_MBUTTONDOWN:
            case WM_XBUTTONDOWN:
                PostMessageW(instance_->notify_window_, reset_message, 0, 0);
                break;
            default:
                break;
            }
        }
    }

    return CallNextHookEx(nullptr, code, wparam, lparam);
}
