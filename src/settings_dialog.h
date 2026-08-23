#pragma once

#include "config.h"

#include <Windows.h>

[[nodiscard]] bool show_settings_dialog(HWND parent, AppConfig& config);
void show_about_dialog(HWND parent);
