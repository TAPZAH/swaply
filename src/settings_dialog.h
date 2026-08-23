#pragma once

#include "config.h"

#include <Windows.h>

[[nodiscard]] bool show_settings_dialog(HWND parent, AppConfig& config);
