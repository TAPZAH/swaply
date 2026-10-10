cmake_minimum_required(VERSION 3.16)

project(Swaply VERSION 0.988.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(Swaply
    src/main.cpp
    src/config.cpp
    src/hotkey.cpp
    src/hook_manager.cpp
    src/text_tracker.cpp
    src/translator.cpp
    src/layout_detector.cpp
    src/word_index.cpp
    src/focus_guard.cpp
    src/input_sim.cpp
    src/updater.cpp
    src/tray_icon.cpp
    src/settings_dialog.cpp
    src/swaply.rc
)

set_target_properties(Swaply PROPERTIES
    WIN32_EXECUTABLE TRUE
)

target_include_directories(Swaply PRIVATE src)

target_compile_definitions(Swaply PRIVATE
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    WINVER=0x0A00
    _WIN32_WINNT=0x0A00
)

target_link_libraries(Swaply PRIVATE
    user32
    shell32
    advapi32
    comctl32
    ole32
    oleaut32
    uiautomationcore
    winhttp
)

add_custom_command(TARGET Swaply POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/assets/default_config.json"
        "$<TARGET_FILE_DIR:Swaply>/default_config.json"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/assets/dict"
        "$<TARGET_FILE_DIR:Swaply>/dict"
)

if(MSVC)
    target_compile_options(Swaply PRIVATE /W4 /permissive- /utf-8)
    target_link_options(Swaply PRIVATE
        "/MANIFESTDEPENDENCY:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'"
    )
    set(CMAKE_RC_FLAGS "${CMAKE_RC_FLAGS} /I${CMAKE_SOURCE_DIR} /I${CMAKE_SOURCE_DIR}/src")
else()
    target_compile_options(Swaply PRIVATE -Wall -Wextra -Wpedantic)
    set(CMAKE_RC_FLAGS "${CMAKE_RC_FLAGS} -I${CMAKE_SOURCE_DIR} -I${CMAKE_SOURCE_DIR}/src")
endif()

if(MINGW)
    target_link_options(Swaply PRIVATE -municode)
endif()

add_executable(swaply_tests
    tests/test_core.cpp
    src/translator.cpp
    src/layout_detector.cpp
    src/word_index.cpp
    src/hotkey.cpp
    src/text_tracker.cpp
    src/focus_guard.cpp
)

target_include_directories(swaply_tests PRIVATE src)
target_compile_definitions(swaply_tests PRIVATE
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    WINVER=0x0A00
    _WIN32_WINNT=0x0A00
)
target_link_libraries(swaply_tests PRIVATE user32)

add_custom_command(TARGET swaply_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/assets/dict"
        "$<TARGET_FILE_DIR:swaply_tests>/dict"
)

if(MSVC)
    target_compile_options(swaply_tests PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(swaply_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

enable_testing()
add_test(NAME Swaply_core COMMAND swaply_tests)
