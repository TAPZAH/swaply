#pragma once

#include "config.h"
#include "hook_manager.h"
#include "translator.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

class TextTracker {
public:
    enum class Action {
        None,
        ConvertWord,
        AutoConvert,
        ConvertSelection,
        LearnWord,
        Undo,
        DiscardUndo,
    };

    struct Glyph {
        wchar_t ch = 0;
        UINT vk = 0;
        bool shift = false;
        bool caps = false;
        Translator::Layout layout = Translator::Layout::En;
    };

    static constexpr std::size_t max_word_length = 128;

    explicit TextTracker(AppConfig& config);

    [[nodiscard]] Action on_key(const HookManager::KeyEvent& event);
    void clear() noexcept;
    void assign_converted(std::wstring_view converted);

    [[nodiscard]] std::wstring current_word() const;
    [[nodiscard]] std::wstring converted_word() const;
    [[nodiscard]] Translator::Layout source_layout() const noexcept;
    [[nodiscard]] UINT terminator() const noexcept { return terminator_; }
    [[nodiscard]] bool empty() const noexcept { return glyphs_.empty(); }

private:
    AppConfig* config_ = nullptr;
    std::vector<Glyph> glyphs_;
    UINT terminator_ = 0;
};
