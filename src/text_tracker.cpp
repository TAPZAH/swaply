#include "text_tracker.h"

#include "focus_guard.h"
#include "layout_detector.h"

#include <algorithm>

namespace {

[[nodiscard]] bool is_key_up(WPARAM wparam) noexcept {
    return wparam == WM_KEYUP || wparam == WM_SYSKEYUP;
}

[[nodiscard]] bool is_modifier(UINT vk) noexcept {
    switch (vk) {
    case VK_SHIFT:
    case VK_LSHIFT:
    case VK_RSHIFT:
    case VK_CONTROL:
    case VK_LCONTROL:
    case VK_RCONTROL:
    case VK_MENU:
    case VK_LMENU:
    case VK_RMENU:
    case VK_LWIN:
    case VK_RWIN:
    case VK_CAPITAL:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] bool is_auto_trigger(UINT vk) noexcept {
    return vk == VK_SPACE || vk == VK_RETURN || vk == VK_TAB;
}

[[nodiscard]] bool is_word_break(UINT vk) noexcept {
    switch (vk) {
    case VK_SPACE:
    case VK_RETURN:
    case VK_TAB:
    case VK_ESCAPE:
    case VK_LEFT:
    case VK_RIGHT:
    case VK_UP:
    case VK_DOWN:
    case VK_HOME:
    case VK_END:
    case VK_PRIOR:
    case VK_NEXT:
    case VK_DELETE:
    case VK_INSERT:
        return true;
    default:
        return vk >= VK_F1 && vk <= VK_F24;
    }
}

[[nodiscard]] std::wstring join_glyphs(const std::vector<TextTracker::Glyph>& glyphs) {
    std::wstring text;
    text.reserve(glyphs.size());
    for (const auto& glyph : glyphs) {
        text.push_back(glyph.ch);
    }
    return text;
}

}  // namespace

TextTracker::TextTracker(AppConfig& config) : config_(&config) {}

TextTracker::Action TextTracker::on_key(const HookManager::KeyEvent& event) {
    if (!glyphs_.empty() && event.target_window != target_window_) {
        clear();
    }

    if (config_ != nullptr && !config_->enabled) {
        return Action::None;
    }

    const UINT vk = event.info.vkCode;
    if (config_ != nullptr) {
        const auto matches = [&](const Hotkey& key) {
            return key.matches(vk, event.ctrl, event.alt, event.shift, event.win);
        };

        if (matches(config_->convert_word) || matches(config_->convert_selection) ||
            matches(config_->learn_word) || matches(config_->undo_conversion)) {
            if (is_key_up(event.wparam)) {
                return Action::None;
            }
            if (matches(config_->undo_conversion)) {
                return Action::Undo;
            }
            if (matches(config_->convert_selection) ||
                (matches(config_->convert_word) && glyphs_.empty())) {
                return Action::ConvertSelection;
            }
            if (matches(config_->learn_word) && !glyphs_.empty()) {
                return Action::LearnWord;
            }
            if (matches(config_->convert_word)) {
                return Action::ConvertWord;
            }
            return Action::None;
        }
    }

    if (config_ != nullptr && FocusGuard::should_ignore(*config_)) {
        clear();
        return Action::None;
    }

    if (is_key_up(event.wparam) || is_modifier(vk)) {
        return Action::None;
    }

    // Any real key means the word kept after the last delimiter is left behind.
    if (finalized_) {
        clear();
    }

    if (event.ctrl || event.alt || event.win) {
        clear();
        return Action::DiscardUndo;
    }

    if (vk == VK_BACK) {
        if (!glyphs_.empty()) {
            glyphs_.pop_back();
        }
        return Action::DiscardUndo;
    }

    if (is_word_break(vk)) {
        if (is_auto_trigger(vk) && !glyphs_.empty() && (config_ == nullptr || config_->auto_switch)) {
            const std::wstring typed = current_word();
            const std::wstring converted = converted_word();
            const std::size_t min_length =
                config_ != nullptr ? config_->min_word_length : LayoutDetector::min_word_length;
            if (LayoutDetector::should_switch(typed, converted, source_layout(), min_length)) {
                terminator_ = vk;
                return Action::AutoConvert;
            }
        }

        // Keep the finished word together with its delimiter so the convert
        // hotkey can still act on it. Enter/Tab only when enabled in the menu.
        const bool keep = vk == VK_SPACE ||
                          (config_ != nullptr && config_->keep_after_enter_tab &&
                           (vk == VK_RETURN || vk == VK_TAB));
        if (keep && !glyphs_.empty()) {
            terminator_ = vk;
            finalized_ = true;
            return Action::DiscardUndo;
        }
        clear();
        return Action::DiscardUndo;
    }

    auto layout = Translator::detect_layout(
        event.layout != nullptr ? event.layout : GetKeyboardLayout(event.target_thread));
    if (layout == Translator::Layout::Other) {
        layout = Translator::Layout::En;
    }

    const auto ch = Translator::char_from_vk(vk, event.shift, event.caps, layout);
    if (!ch.has_value()) {
        clear();
        return Action::DiscardUndo;
    }

    if (glyphs_.size() < max_word_length) {
        if (glyphs_.empty()) {
            target_window_ = event.target_window;
        }
        glyphs_.push_back(Glyph{*ch, vk, event.shift, event.caps, layout});
    }

    // Correct as soon as the first letters unambiguously belong to the other
    // layout, without waiting for space/Enter/Tab. Only from min_word_length
    // letters on, so single/double letter particles still wait for a delimiter.
    const std::size_t min_length =
        config_ != nullptr ? config_->min_word_length : LayoutDetector::min_word_length;
    if (config_ != nullptr && config_->auto_switch && glyphs_.size() >= min_length &&
        LayoutDetector::should_switch(current_word(), converted_word(), source_layout(), min_length)) {
        terminator_ = 0;
        return Action::AutoConvert;
    }

    return Action::DiscardUndo;
}

void TextTracker::clear() noexcept {
    glyphs_.clear();
    terminator_ = 0;
    target_window_ = nullptr;
    finalized_ = false;
}

void TextTracker::finalize(std::wstring_view converted, UINT terminator) {
    assign_converted(converted);
    if (glyphs_.empty()) {
        return;
    }
    terminator_ = terminator;
    finalized_ = true;
}

void TextTracker::assign_converted(std::wstring_view converted) {
    if (converted.size() != glyphs_.size()) {
        clear();
        return;
    }

    std::vector<Glyph> next;
    next.reserve(converted.size());

    const auto target = Translator::opposite(source_layout());
    for (std::size_t i = 0; i < converted.size(); ++i) {
        Glyph glyph = glyphs_[i];
        glyph.ch = converted[i];
        glyph.layout = target;
        next.push_back(glyph);
    }

    glyphs_ = std::move(next);
}

std::wstring TextTracker::current_word() const {
    return join_glyphs(glyphs_);
}

std::wstring TextTracker::converted_word() const {
    std::wstring text;
    text.reserve(glyphs_.size());
    const auto target = Translator::opposite(source_layout());

    for (const auto& glyph : glyphs_) {
        const auto converted = Translator::char_from_vk(
            glyph.vk,
            glyph.shift,
            glyph.caps,
            target);
        text.push_back(converted.value_or(glyph.ch));
    }

    return text;
}

bool TextTracker::should_auto_convert() const {
    if (glyphs_.empty() || config_ == nullptr || !config_->auto_switch) {
        return false;
    }
    return LayoutDetector::should_switch(
        current_word(),
        converted_word(),
        source_layout(),
        config_->min_word_length);
}

Translator::Layout TextTracker::source_layout() const {
    if (glyphs_.empty()) {
        return Translator::Layout::En;
    }

    const auto inferred = Translator::infer_layout(current_word());
    if (inferred == Translator::Layout::En || inferred == Translator::Layout::Ru) {
        int latin = 0;
        int cyrillic = 0;
        for (const auto& glyph : glyphs_) {
            const wchar_t ch = glyph.ch;
            const bool is_latin = (ch >= L'A' && ch <= L'Z') || (ch >= L'a' && ch <= L'z');
            const bool is_cyrillic = ch >= 0x0400 && ch <= 0x04FF;
            latin += static_cast<int>(is_latin);
            cyrillic += static_cast<int>(is_cyrillic);
        }
        if (latin != cyrillic) {
            return latin > cyrillic ? Translator::Layout::En : Translator::Layout::Ru;
        }
    }

    const auto count_layout = [&](Translator::Layout layout) {
        return std::count_if(glyphs_.begin(), glyphs_.end(), [&](const Glyph& glyph) {
            return glyph.layout == layout;
        });
    };
    const auto en_count = count_layout(Translator::Layout::En);
    const auto ru_count = count_layout(Translator::Layout::Ru);
    if (en_count == ru_count) {
        const auto first = glyphs_.front().layout;
        return first == Translator::Layout::Other ? inferred : first;
    }
    return en_count > ru_count
               ? Translator::Layout::En
               : Translator::Layout::Ru;
}
