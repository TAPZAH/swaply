#include "updater.h"

#include "version.h"

#include <shellapi.h>
#include <winhttp.h>

#include <array>
#include <cstddef>
#include <string>
#include <string_view>

namespace {

// Only the official repository is ever contacted, over HTTPS. Binaries are not
// code-signed, so integrity relies on TLS plus this fixed host.
constexpr wchar_t kApiHost[] = L"api.github.com";
constexpr wchar_t kApiPath[] = L"/repos/TAPZAH/swaply/releases?per_page=1";
constexpr char kDownloadPrefix[] = "https://github.com/TAPZAH/swaply/";
constexpr std::size_t kMaxResponseBytes = 8 * 1024 * 1024;

class WinHttpHandle {
public:
    explicit WinHttpHandle(HINTERNET handle) : handle_(handle) {}
    ~WinHttpHandle() {
        if (handle_ != nullptr) {
            WinHttpCloseHandle(handle_);
        }
    }

    WinHttpHandle(const WinHttpHandle&) = delete;
    WinHttpHandle& operator=(const WinHttpHandle&) = delete;

    [[nodiscard]] HINTERNET get() const noexcept { return handle_; }
    [[nodiscard]] explicit operator bool() const noexcept { return handle_ != nullptr; }

private:
    HINTERNET handle_ = nullptr;
};

[[nodiscard]] std::wstring utf8_to_wide(std::string_view text) {
    if (text.empty()) {
        return {};
    }
    const int needed = MultiByteToWideChar(
        CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (needed <= 0) {
        return {};
    }
    std::wstring wide(static_cast<std::size_t>(needed), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), needed);
    return wide;
}

[[nodiscard]] bool http_get(const std::wstring& host, const std::wstring& path, std::string& out) {
    WinHttpHandle session(WinHttpOpen(
        L"Swaply-updater", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session) {
        return false;
    }
    WinHttpSetTimeouts(session.get(), 5000, 5000, 5000, 5000);

    WinHttpHandle connect(WinHttpConnect(session.get(), host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0));
    if (!connect) {
        return false;
    }

    WinHttpHandle request(WinHttpOpenRequest(
        connect.get(),
        L"GET",
        path.c_str(),
        nullptr,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        WINHTTP_FLAG_SECURE));
    if (!request) {
        return false;
    }

    DWORD redirect = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
    WinHttpSetOption(request.get(), WINHTTP_OPTION_REDIRECT_POLICY, &redirect, sizeof(redirect));

    const wchar_t headers[] =
        L"User-Agent: Swaply-updater\r\n"
        L"Accept: application/vnd.github+json\r\n";
    if (!WinHttpSendRequest(
            request.get(), headers, static_cast<DWORD>(-1), WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(request.get(), nullptr)) {
        return false;
    }

    DWORD status = 0;
    DWORD status_size = sizeof(status);
    if (!WinHttpQueryHeaders(
            request.get(),
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX,
            &status,
            &status_size,
            WINHTTP_NO_HEADER_INDEX) ||
        status != 200) {
        return false;
    }

    out.clear();
    for (;;) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request.get(), &available)) {
            return false;
        }
        if (available == 0) {
            break;
        }
        if (out.size() + available > kMaxResponseBytes) {
            return false;
        }

        std::string chunk(available, '\0');
        DWORD read = 0;
        if (!WinHttpReadData(request.get(), chunk.data(), available, &read)) {
            return false;
        }
        out.append(chunk.data(), read);
    }
    return true;
}

[[nodiscard]] std::string json_string_value(
    const std::string& body,
    std::string_view key,
    std::size_t from) {
    const std::string needle = "\"" + std::string(key) + "\":\"";
    const auto position = body.find(needle, from);
    if (position == std::string::npos) {
        return {};
    }
    const auto begin = position + needle.size();
    const auto end = body.find('"', begin);
    if (end == std::string::npos) {
        return {};
    }
    return body.substr(begin, end - begin);
}

[[nodiscard]] std::string find_setup_url(const std::string& body) {
    const std::string key = "\"browser_download_url\":\"";
    constexpr std::string_view suffix = "-setup.exe";
    std::size_t from = 0;
    while (true) {
        const auto position = body.find(key, from);
        if (position == std::string::npos) {
            return {};
        }
        const auto begin = position + key.size();
        const auto end = body.find('"', begin);
        if (end == std::string::npos) {
            return {};
        }
        std::string url = body.substr(begin, end - begin);
        if (url.size() > suffix.size() &&
            url.compare(url.size() - suffix.size(), suffix.size(), suffix) == 0) {
            return url;
        }
        from = end + 1;
    }
}

[[nodiscard]] std::array<int, 3> parse_version(std::wstring_view text) {
    std::array<int, 3> parts{0, 0, 0};
    int index = 0;
    int value = 0;
    bool in_number = false;
    for (const wchar_t ch : text) {
        if (ch >= L'0' && ch <= L'9') {
            value = value * 10 + static_cast<int>(ch - L'0');
            if (value > 1000000) {
                value = 1000000;
            }
            in_number = true;
            continue;
        }
        if (in_number) {
            if (index < 3) {
                parts[static_cast<std::size_t>(index)] = value;
            }
            ++index;
            value = 0;
            in_number = false;
            if (index >= 3) {
                break;
            }
        }
    }
    if (in_number && index < 3) {
        parts[static_cast<std::size_t>(index)] = value;
    }
    return parts;
}

}  // namespace

UpdateInfo Updater::check() {
    UpdateInfo info;

    std::string body;
    if (!http_get(kApiHost, kApiPath, body)) {
        info.check_failed = true;
        return info;
    }

    const std::string tag = json_string_value(body, "tag_name", 0);
    if (tag.empty()) {
        info.check_failed = true;
        return info;
    }

    const std::wstring version = utf8_to_wide(tag);
    if (parse_version(version) <= parse_version(SWAPLY_VERSION_STRW)) {
        return info;
    }

    const std::string url = find_setup_url(body);
    if (url.empty() || url.rfind(kDownloadPrefix, 0) != 0) {
        info.check_failed = true;
        return info;
    }

    info.available = true;
    info.version = version;
    info.url = utf8_to_wide(url);
    return info;
}

bool Updater::download_and_run(const std::wstring& url) {
    if (url.rfind(L"https://github.com/TAPZAH/swaply/", 0) != 0) {
        return false;
    }

    constexpr std::wstring_view scheme = L"https://";
    const auto slash = url.find(L'/', scheme.size());
    if (slash == std::wstring::npos) {
        return false;
    }
    const std::wstring host = url.substr(scheme.size(), slash - scheme.size());
    const std::wstring path = url.substr(slash);

    std::string data;
    if (!http_get(host, path, data) || data.empty()) {
        return false;
    }

    wchar_t temp_dir[MAX_PATH]{};
    if (GetTempPathW(MAX_PATH, temp_dir) == 0) {
        return false;
    }
    const std::wstring file = std::wstring(temp_dir) + L"Swaply-update-setup.exe";

    HANDLE handle = CreateFileW(
        file.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        return false;
    }

    DWORD written = 0;
    const bool written_ok =
        data.size() <= MAXDWORD &&
        WriteFile(handle, data.data(), static_cast<DWORD>(data.size()), &written, nullptr) != FALSE &&
        written == data.size();
    CloseHandle(handle);
    if (!written_ok) {
        DeleteFileW(file.c_str());
        return false;
    }

    const HINSTANCE result = ShellExecuteW(
        nullptr, L"open", file.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(result) <= 32) {
        DeleteFileW(file.c_str());
        return false;
    }
    return true;
}
