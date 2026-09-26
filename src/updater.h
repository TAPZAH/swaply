#pragma once

#include <Windows.h>

#include <string>

struct UpdateInfo {
    bool available = false;
    bool check_failed = false;
    std::wstring version;
    std::wstring url;
};

class Updater {
public:
    // Synchronous HTTPS request to the GitHub releases API. Reports whether a
    // newer release exists and where its setup executable can be downloaded.
    [[nodiscard]] static UpdateInfo check();

    // Downloads the setup executable to a temporary file and launches it.
    // Returns true when the installer was started.
    static bool download_and_run(const std::wstring& url);
};
