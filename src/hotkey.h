#pragma once

#include <Windows.h>

#include <string>
#include <string_view>

struct Hotkey {
    UINT vk = 0;
    bool ctrl = false;
    bool alt = false;
    bool shift = false;
    bool win = false;

    [[nodiscard]] bool empty() const noexcept { return vk == 0; }
    [[nodiscard]] bool matches(UINT event_vk, bool event_ctrl, bool event_alt, bool event_shift, bool event_win) const noexcept;
    [[nodiscard]] UINT pack() const noexcept;
    [[nodiscard]] std::wstring to_wstring() const;
    [[nodiscard]] std::string to_string() const;

    static Hotkey unpack(UINT packed) noexcept;
    static Hotkey parse(std::string_view text);
};
