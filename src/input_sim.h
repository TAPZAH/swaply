#pragma once

#include "translator.h"

#include <cstddef>
#include <string_view>

class InputSimulator {
public:
    static bool replace_text(std::size_t delete_count, std::wstring_view text);
    static bool replace_text(std::size_t delete_count, std::wstring_view text, UINT trailing_vk);
    static bool activate_layout(Translator::Layout layout);
    static bool convert_selection();
};
