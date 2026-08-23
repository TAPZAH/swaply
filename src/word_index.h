#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

class WordIndex {
public:
    void add(std::wstring word);
    void finalize();
    bool load_utf8_file(const std::wstring& path);

    [[nodiscard]] bool contains(std::wstring_view word) const;
    [[nodiscard]] bool has_prefix(std::wstring_view prefix) const;
    [[nodiscard]] std::size_t size() const noexcept { return words_.size(); }

private:
    std::vector<std::wstring> words_;
};
