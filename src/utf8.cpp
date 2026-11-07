#include "utf8.h"

#include <climits>

std::wstring utf8_to_wide(std::string_view text) {
    std::wstring out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size();) {
        const unsigned char lead = static_cast<unsigned char>(text[i]);
        unsigned code = 0;
        int extra = 0;
        if (lead < 0x80) {
            code = lead;
        } else if ((lead & 0xE0) == 0xC0) {
            code = lead & 0x1F;
            extra = 1;
        } else if ((lead & 0xF0) == 0xE0) {
            code = lead & 0x0F;
            extra = 2;
        } else if ((lead & 0xF8) == 0xF0) {
            code = lead & 0x07;
            extra = 3;
        } else {
            ++i;
            continue;
        }

        if (i + static_cast<std::size_t>(extra) >= text.size()) {
            break;
        }
        bool valid = true;
        for (int k = 0; k < extra; ++k) {
            const unsigned char next = static_cast<unsigned char>(text[i + 1 + static_cast<std::size_t>(k)]);
            if ((next & 0xC0) != 0x80) {
                valid = false;
                break;
            }
            code = (code << 6) | (next & 0x3F);
        }
        if (!valid) {
            ++i;
            continue;
        }
        i += 1 + static_cast<std::size_t>(extra);

        if (code > 0x10FFFF) {
            continue;
        }
#if WCHAR_MAX <= 0xFFFF
        if (code <= 0xFFFF) {
            out.push_back(static_cast<wchar_t>(code));
        } else {
            code -= 0x10000;
            out.push_back(static_cast<wchar_t>(0xD800 + (code >> 10)));
            out.push_back(static_cast<wchar_t>(0xDC00 + (code & 0x3FF)));
        }
#else
        out.push_back(static_cast<wchar_t>(code));
#endif
    }
    return out;
}

std::string wide_to_utf8(std::wstring_view text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        unsigned code = static_cast<unsigned>(text[i]);
#if WCHAR_MAX <= 0xFFFF
        if (code >= 0xD800 && code <= 0xDBFF && i + 1 < text.size()) {
            const unsigned low = static_cast<unsigned>(text[i + 1]);
            if (low >= 0xDC00 && low <= 0xDFFF) {
                code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
                ++i;
            } else {
                code = 0xFFFD;
            }
        } else if (code >= 0xD800 && code <= 0xDFFF) {
            code = 0xFFFD;
        }
#endif
        if (code < 0x80) {
            out.push_back(static_cast<char>(code));
        } else if (code < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (code >> 6)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
        } else if (code < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (code >> 12)));
            out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (code >> 18)));
            out.push_back(static_cast<char>(0x80 | ((code >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
        }
    }
    return out;
}
