#pragma once

#include "config.h"

class FocusGuard {
public:
    [[nodiscard]] static bool should_ignore(const AppConfig& config);
};
