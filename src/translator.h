#pragma once

#ifdef _WIN32
#include <Windows.h>
#endif

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

#ifdef _WIN32
    [[nodiscard]] static Layout detect_layout(HKL hkl) noexcept;
#endif
    [[nodiscard]] static Layout infer_layout(std::wstring_view text) noexcept;
    [[nodiscard]] static Layout opposite(Layout layout) noexcept;

    struct PhysicalKey {
        unsigned int vk = 0;
        bool shift = false;
    };

    [[nodiscard]] static std::optional<wchar_t> char_from_vk(
        unsigned int vk,
        bool shift,
        bool caps,
        Layout layout) noexcept;

    [[nodiscard]] static std::optional<PhysicalKey> key_from_char(wchar_t ch, Layout layout) noexcept;

    [[nodiscard]] static std::wstring convert(std::wstring_view text);
};
