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

[[nodiscard]] HKL foreground_layout() noexcept {
    HWND foreground = GetForegroundWindow();
    if (foreground == nullptr) {
        return GetKeyboardLayout(0);
    }

    const DWORD thread_id = GetWindowThreadProcessId(foreground, nullptr);
    return GetKeyboardLayout(thread_id);
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
    if (config_ != nullptr && !config_->enabled) {
        return Action::None;
    }

    if (config_ != nullptr && FocusGuard::should_ignore(*config_)) {
        clear();
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
            if (matches(config_->convert_selection)) {
                return Action::ConvertSelection;
            }
            if (matches(config_->learn_word) && !glyphs_.empty()) {
                return Action::LearnWord;
            }
            if (matches(config_->convert_word)) {
                return glyphs_.empty() ? Action::Undo : Action::ConvertWord;
            }
            return Action::None;
        }
    }

    if (is_key_up(event.wparam) || is_modifier(vk)) {
        return Action::None;
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
        clear();
        return Action::DiscardUndo;
    }

    const auto layout = Translator::detect_layout(foreground_layout());
    const auto ch = Translator::char_from_vk(vk, event.shift, event.caps, layout);
    if (!ch.has_value()) {
        clear();
        return Action::DiscardUndo;
    }

    if (glyphs_.size() < max_word_length) {
        glyphs_.push_back(Glyph{*ch, vk, event.shift, event.caps, layout});
    }

    if (config_ != nullptr && config_->auto_switch) {
        const std::size_t min_length = config_->min_word_length;
        if (LayoutDetector::should_switch(current_word(), converted_word(), source_layout(), min_length)) {
            terminator_ = 0;
            return Action::AutoConvert;
        }
    }

    return Action::DiscardUndo;
}

void TextTracker::clear() noexcept {
    glyphs_.clear();
    terminator_ = 0;
}

void TextTracker::assign_converted(std::wstring_view converted) {
    std::vector<Glyph> next;
    next.reserve(converted.size());

    const auto target = Translator::opposite(source_layout());
    const auto limit = std::min(converted.size(), glyphs_.size());
    for (std::size_t i = 0; i < limit; ++i) {
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

    for (const auto& glyph : glyphs_) {
        const auto converted = Translator::char_from_vk(
            glyph.vk,
            glyph.shift,
            glyph.caps,
            Translator::opposite(glyph.layout));
        text.push_back(converted.value_or(glyph.ch));
    }

    return text;
}

Translator::Layout TextTracker::source_layout() const noexcept {
    if (glyphs_.empty()) {
        return Translator::Layout::En;
    }
    return glyphs_.front().layout;
}
