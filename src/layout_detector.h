#pragma once

#include "translator.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

class LayoutDetector {
public:
    static constexpr std::size_t min_word_length = 3;

    static void load_bundled_dictionaries();
    static void set_user_words(const std::vector<std::string>& extra_en, const std::vector<std::string>& extra_ru);
    static void set_exceptions(const std::vector<std::string>& words);

    [[nodiscard]] static bool is_technical_token(std::wstring_view text) noexcept;
    [[nodiscard]] static bool is_exception_word(std::wstring_view text, Translator::Layout layout);
    // True when text is a valid in-progress word (dictionary prefix) in its own
    // language; used to avoid an over-eager early conversion.
    [[nodiscard]] static bool is_word_prefix(std::wstring_view text, Translator::Layout layout);

    [[nodiscard]] static bool should_switch(
        std::wstring_view typed,
        std::wstring_view converted,
        Translator::Layout source,
        std::size_t min_length = min_word_length);
};
