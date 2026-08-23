#include "word_index.h"

#include <Windows.h>

#include <algorithm>

void WordIndex::add(std::wstring word) {
    if (!word.empty()) {
        words_.push_back(std::move(word));
    }
}

void WordIndex::finalize() {
    if (words_.empty()) {
        return;
    }
    std::sort(words_.begin(), words_.end());
    words_.erase(std::unique(words_.begin(), words_.end()), words_.end());
}

bool WordIndex::load_utf8_file(const std::wstring& path) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }

    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size) || size.QuadPart <= 0 || size.QuadPart > 8 * 1024 * 1024) {
        CloseHandle(file);
        return false;
    }

    std::string text(static_cast<std::size_t>(size.QuadPart), '\0');
    DWORD read = 0;
    const BOOL ok = ReadFile(file, text.data(), static_cast<DWORD>(text.size()), &read, nullptr);
    CloseHandle(file);
    if (!ok) {
        return false;
    }
    text.resize(read);

    std::string current;
    const auto flush = [&]() {
        if (current.empty()) {
            return;
        }
        const int needed = MultiByteToWideChar(CP_UTF8, 0, current.data(), static_cast<int>(current.size()), nullptr, 0);
        if (needed > 0) {
            std::wstring wide(static_cast<std::size_t>(needed), L'\0');
            MultiByteToWideChar(CP_UTF8, 0, current.data(), static_cast<int>(current.size()), wide.data(), needed);
            std::wstring core;
            core.reserve(wide.size());
            for (wchar_t ch : wide) {
                if (ch >= L'A' && ch <= L'Z') {
                    ch = static_cast<wchar_t>(ch - L'A' + L'a');
                } else if (ch >= L'А' && ch <= L'Я') {
                    ch = static_cast<wchar_t>(ch - L'А' + L'а');
                } else if (ch == L'Ё' || ch == L'ё') {
                    ch = L'е';
                }
                const bool letter = (ch >= L'a' && ch <= L'z') || (ch >= L'а' && ch <= L'я');
                if (letter) {
                    core.push_back(ch);
                }
            }
            add(std::move(core));
        }
        current.clear();
    };

    for (const char ch : text) {
        if (ch == '\n' || ch == '\r') {
            flush();
        } else {
            current.push_back(ch);
        }
    }
    flush();
    return true;
}

bool WordIndex::contains(std::wstring_view word) const {
    const std::wstring key(word);
    return std::binary_search(words_.begin(), words_.end(), key);
}

bool WordIndex::has_prefix(std::wstring_view prefix) const {
    if (prefix.empty()) {
        return false;
    }

    const std::wstring key(prefix);
    const auto it = std::lower_bound(words_.begin(), words_.end(), key);
    return it != words_.end() && it->compare(0, key.size(), key) == 0;
}
