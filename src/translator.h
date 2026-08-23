#pragma once

#include <Windows.h>

#include <optional>
#include <string>
#include <string_view>

class Translator {
public:
    enum class Layout {
        En,
        Ru,
        Other,
    };

    [[nodiscard]] static Layout detect_layout(HKL hkl) noexcept;
    [[nodiscard]] static Layout infer_layout(std::wstring_view text) noexcept;
    [[nodiscard]] static Layout opposite(Layout layout) noexcept;

    [[nodiscard]] static std::optional<wchar_t> char_from_vk(
        UINT vk,
        bool shift,
        bool caps,
        Layout layout) noexcept;

    [[nodiscard]] static std::wstring convert(std::wstring_view text);
};
