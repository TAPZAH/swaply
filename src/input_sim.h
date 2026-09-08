#pragma once

#include "translator.h"

#include <cstddef>
#include <string_view>

class InputSimulator {
public:
    static bool replace_text(
        HWND target,
        std::size_t delete_count,
        std::wstring_view text,
        UINT trailing_vk,
        Translator::Layout type_layout);
    static bool activate_layout(Translator::Layout layout);
    static bool convert_selection();
    static bool send_virtual_key(UINT vk);
    static void restore_system_layouts();
};
