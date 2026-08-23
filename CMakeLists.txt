cmake_minimum_required(VERSION 3.16)

project(wxneur LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(wxneur
    src/main.cpp
    src/config.cpp
    src/hotkey.cpp
    src/hook_manager.cpp
    src/text_tracker.cpp
    src/translator.cpp
    src/layout_detector.cpp
    src/focus_guard.cpp
    src/input_sim.cpp
    src/tray_icon.cpp
    src/settings_dialog.cpp
    src/wxneur.rc
)

set_target_properties(wxneur PROPERTIES
    WIN32_EXECUTABLE TRUE
)

target_include_directories(wxneur PRIVATE src)

target_compile_definitions(wxneur PRIVATE
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    WINVER=0x0A00
    _WIN32_WINNT=0x0A00
)

target_link_libraries(wxneur PRIVATE
    user32
    shell32
    advapi32
    comctl32
)

add_custom_command(TARGET wxneur POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/assets/default_config.json"
        "$<TARGET_FILE_DIR:wxneur>/default_config.json"
)

if(MSVC)
    target_compile_options(wxneur PRIVATE /W4 /permissive- /utf-8)
    target_link_options(wxneur PRIVATE
        "/MANIFESTDEPENDENCY:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'"
    )
    set(CMAKE_RC_FLAGS "${CMAKE_RC_FLAGS} /I${CMAKE_SOURCE_DIR} /I${CMAKE_SOURCE_DIR}/src")
else()
    target_compile_options(wxneur PRIVATE -Wall -Wextra -Wpedantic)
    set(CMAKE_RC_FLAGS "${CMAKE_RC_FLAGS} -I${CMAKE_SOURCE_DIR} -I${CMAKE_SOURCE_DIR}/src")
endif()

if(MINGW)
    target_link_options(wxneur PRIVATE -municode)
endif()

add_executable(wxneur_tests
    tests/test_core.cpp
    src/translator.cpp
    src/layout_detector.cpp
    src/hotkey.cpp
)

target_include_directories(wxneur_tests PRIVATE src)
target_compile_definitions(wxneur_tests PRIVATE
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    WINVER=0x0A00
    _WIN32_WINNT=0x0A00
)
target_link_libraries(wxneur_tests PRIVATE user32)

if(MSVC)
    target_compile_options(wxneur_tests PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(wxneur_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

enable_testing()
add_test(NAME wxneur_core COMMAND wxneur_tests)
