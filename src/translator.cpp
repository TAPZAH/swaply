#include "translator.h"

#include <array>
#include <unordered_map>

namespace {

struct KeyGlyphs {
    UINT vk;
    wchar_t en;
    wchar_t en_shift;
    wchar_t ru;
    wchar_t ru_shift;
    bool caps_sensitive;
};

constexpr std::array<KeyGlyphs, 47> kKeys{{
    {VK_OEM_3, L'`', L'~', L'ё', L'Ё', true},
    {'1', L'1', L'!', L'1', L'!', false},
    {'2', L'2', L'@', L'2', L'"', false},
    {'3', L'3', L'#', L'3', L'№', false},
    {'4', L'4', L'$', L'4', L';', false},
    {'5', L'5', L'%', L'5', L'%', false},
    {'6', L'6', L'^', L'6', L':', false},
    {'7', L'7', L'&', L'7', L'?', false},
    {'8', L'8', L'*', L'8', L'*', false},
    {'9', L'9', L'(', L'9', L'(', false},
    {'0', L'0', L')', L'0', L')', false},
    {VK_OEM_MINUS, L'-', L'_', L'-', L'_', false},
    {VK_OEM_PLUS, L'=', L'+', L'=', L'+', false},

    {'Q', L'q', L'Q', L'й', L'Й', true},
    {'W', L'w', L'W', L'ц', L'Ц', true},
    {'E', L'e', L'E', L'у', L'У', true},
    {'R', L'r', L'R', L'к', L'К', true},
    {'T', L't', L'T', L'е', L'Е', true},
    {'Y', L'y', L'Y', L'н', L'Н', true},
    {'U', L'u', L'U', L'г', L'Г', true},
    {'I', L'i', L'I', L'ш', L'Ш', true},
    {'O', L'o', L'O', L'щ', L'Щ', true},
    {'P', L'p', L'P', L'з', L'З', true},
    {VK_OEM_4, L'[', L'{', L'х', L'Х', true},
    {VK_OEM_6, L']', L'}', L'ъ', L'Ъ', true},

    {'A', L'a', L'A', L'ф', L'Ф', true},
    {'S', L's', L'S', L'ы', L'Ы', true},
    {'D', L'd', L'D', L'в', L'В', true},
    {'F', L'f', L'F', L'а', L'А', true},
    {'G', L'g', L'G', L'п', L'П', true},
    {'H', L'h', L'H', L'р', L'Р', true},
    {'J', L'j', L'J', L'о', L'О', true},
    {'K', L'k', L'K', L'л', L'Л', true},
    {'L', L'l', L'L', L'д', L'Д', true},
    {VK_OEM_1, L';', L':', L'ж', L'Ж', true},
    {VK_OEM_7, L'\'', L'"', L'э', L'Э', true},

    {'Z', L'z', L'Z', L'я', L'Я', true},
    {'X', L'x', L'X', L'ч', L'Ч', true},
    {'C', L'c', L'C', L'с', L'С', true},
    {'V', L'v', L'V', L'м', L'М', true},
    {'B', L'b', L'B', L'и', L'И', true},
    {'N', L'n', L'N', L'т', L'Т', true},
    {'M', L'm', L'M', L'ь', L'Ь', true},
    {VK_OEM_COMMA, L',', L'<', L'б', L'Б', true},
    {VK_OEM_PERIOD, L'.', L'>', L'ю', L'Ю', true},
    {VK_OEM_2, L'/', L'?', L'.', L',', false},
    {VK_OEM_5, L'\\', L'|', L'\\', L'/', false},
}};

[[nodiscard]] const KeyGlyphs* find_vk(UINT vk) noexcept {
    for (const auto& key : kKeys) {
        if (key.vk == vk) {
            return &key;
        }
    }
    return nullptr;
}

[[nodiscard]] wchar_t pick(const KeyGlyphs& key, Translator::Layout layout, bool upper) noexcept {
    if (layout == Translator::Layout::Ru) {
        return upper ? key.ru_shift : key.ru;
    }
    return upper ? key.en_shift : key.en;
}

[[nodiscard]] const std::unordered_map<wchar_t, wchar_t>& map_en_to_ru() {
    static const auto map = [] {
        std::unordered_map<wchar_t, wchar_t> result;
        result.reserve(kKeys.size() * 2);
        for (const auto& key : kKeys) {
            result.emplace(key.en, key.ru);
            result.emplace(key.en_shift, key.ru_shift);
        }
        return result;
    }();
    return map;
}

[[nodiscard]] const std::unordered_map<wchar_t, wchar_t>& map_ru_to_en() {
    static const auto map = [] {
        std::unordered_map<wchar_t, wchar_t> result;
        result.reserve(kKeys.size() * 2);
        for (const auto& key : kKeys) {
            result.emplace(key.ru, key.en);
            result.emplace(key.ru_shift, key.en_shift);
        }
        return result;
    }();
    return map;
}

[[nodiscard]] bool is_latin(wchar_t ch) noexcept {
    return (ch >= L'A' && ch <= L'Z') || (ch >= L'a' && ch <= L'z');
}

[[nodiscard]] bool is_cyrillic(wchar_t ch) noexcept {
    return ch >= 0x0400 && ch <= 0x04FF;
}

}  // namespace

Translator::Layout Translator::detect_layout(HKL hkl) noexcept {
    switch (PRIMARYLANGID(LOWORD(reinterpret_cast<ULONG_PTR>(hkl)))) {
    case LANG_ENGLISH:
        return Layout::En;
    case LANG_RUSSIAN:
        return Layout::Ru;
    default:
        return Layout::Other;
    }
}

Translator::Layout Translator::infer_layout(std::wstring_view text) noexcept {
    int latin = 0;
    int cyrillic = 0;
    for (const wchar_t ch : text) {
        latin += static_cast<int>(is_latin(ch));
        cyrillic += static_cast<int>(is_cyrillic(ch));
    }

    if (cyrillic > latin) {
        return Layout::Ru;
    }
    if (latin > 0 || cyrillic == 0) {
        return Layout::En;
    }
    return Layout::Ru;
}

Translator::Layout Translator::opposite(Layout layout) noexcept {
    return layout == Layout::Ru ? Layout::En : Layout::Ru;
}

std::optional<wchar_t> Translator::char_from_vk(UINT vk, bool shift, bool caps, Layout layout) noexcept {
    if (layout == Layout::Other) {
        layout = Layout::En;
    }

    UINT normalized = vk;
    if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) {
        normalized = static_cast<UINT>('0' + (vk - VK_NUMPAD0));
    }

    const KeyGlyphs* key = find_vk(normalized);
    if (key == nullptr) {
        return std::nullopt;
    }

    const bool upper = key->caps_sensitive ? (shift != caps) : shift;
    return pick(*key, layout, upper);
}

std::wstring Translator::convert(std::wstring_view text) {
    const auto source = infer_layout(text);
    const auto& map = (source == Layout::Ru) ? map_ru_to_en() : map_en_to_ru();
    std::wstring result;
    result.reserve(text.size());

    for (const wchar_t ch : text) {
        if (const auto it = map.find(ch); it != map.end()) {
            result.push_back(it->second);
        } else {
            result.push_back(ch);
        }
    }

    return result;
}
