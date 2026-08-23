# Open Source Layout Switcher (Abra C++ Port)

## 🎯 Project Goal
Создать легковесный, быстрый и минималистичный аналог Punto Switcher / xneur для Windows на Modern C++ (17/20). 
Вдохновлено проектом Abra (Python), но переписано на нативный WinAPI для минимального потребления памяти (< 5MB) и мгновенного отклика.

## 🛠 Tech Stack
- **Язык:** C++17 / C++20
- **Сборка:** CMake
- **UI/Система:** Win32 API (без Qt/wxWidgets для минимизации размера)
- **Конфигурация:** JSON (через `nlohmann/json` header-only)
- **Иконка:** Встроенный ресурс `.ico`

## 📁 Directory Structure
```text
/src
  main.cpp          # Entry point, message loop
  hook_manager.cpp  # WH_KEYBOARD_LL implementation
  text_tracker.cpp  # Buffer for current word & backspace logic
  translator.cpp    # Char mapping (QWERTY <-> ЙЦУКЕН)
  tray_icon.cpp     # Shell_NotifyIcon wrapper
  input_sim.cpp     # SendInput wrapper (Unicode & Backspace)
/assets
  icon.ico
  default_config.json
CMakeLists.txt