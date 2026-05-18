cmake_minimum_required(VERSION 3.21)

project(macos-search
    VERSION 0.1.0
    DESCRIPTION "Standalone cached folder/file search for macOS"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT APPLE)
    message(FATAL_ERROR "macos-search currently targets macOS only.")
endif()

if(EXISTS "/opt/homebrew/opt/qt")
    list(APPEND CMAKE_PREFIX_PATH "/opt/homebrew/opt/qt")
endif()
if(EXISTS "/usr/local/opt/qt")
    list(APPEND CMAKE_PREFIX_PATH "/usr/local/opt/qt")
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Concurrent Test)

enable_testing()

# ---------------------------------------------------------------------------
# Shared sources (used by both app and tests)
# ---------------------------------------------------------------------------
set(MACOS_SEARCH_LIB_SOURCES
    src/PathCacheManager.cpp
    src/PathCacheManager.h
    src/FolderSearchWorker.cpp
    src/FolderSearchWorker.h
    src/SearchField.cpp
    src/SearchField.h
    src/ExcludeSettings.cpp
    src/ExcludeSettings.h
    src/ExcludeSettingsDialog.cpp
    src/ExcludeSettingsDialog.h
    src/MaudeConfig.cpp
    src/MaudeConfig.h
    src/ThemeManager.cpp
    src/ThemeManager.h
    src/SwiftUIStyle.cpp
    src/SwiftUIStyle.h
    src/IconRegistry.cpp
    src/IconRegistry.h
    src/FolderBrowserDialog.cpp
    src/FolderBrowserDialog.h
    src/Autostart.cpp
    src/Autostart.h
    src/FirstRunDialog.cpp
    src/FirstRunDialog.h
    src/GlobalHotkey.cpp
    src/GlobalHotkey.h
    src/PathSelector/PathSelector.cpp
    src/PathSelector/PathSelector.h
    src/PathSelector/PathSelectorState.cpp
    src/PathSelector/PathSelectorState.h
    src/PathSelector/PathSelectorUI.cpp
    src/PathSelector/PathSelectorUI.h
    src/PathSelector/PathSelectorKeyHandler.cpp
    src/PathSelector/PathSelectorKeyHandler.h
    src/PathSelector/PathSelectorPopup.cpp
    src/PathSelector/PathSelectorPopup.h
    src/PathSelector/FileSystemAdapter.cpp
    src/PathSelector/FileSystemAdapter.h
)

set(MACOS_SEARCH_APP_ICON "${CMAKE_CURRENT_SOURCE_DIR}/assets/macos-search.icns")

set(MACOS_SEARCH_SOURCES
    src/main.cpp
    src/MainWindow.cpp
    src/MainWindow.h
    src/SearchResultModel.cpp
    src/SearchResultModel.h
    ${MACOS_SEARCH_LIB_SOURCES}
    assets/icons.qrc
    ${MACOS_SEARCH_APP_ICON}
)

qt_add_executable(macos-search MACOSX_BUNDLE ${MACOS_SEARCH_SOURCES})

set_source_files_properties(${MACOS_SEARCH_APP_ICON} PROPERTIES
    MACOSX_PACKAGE_LOCATION "Resources"
)

target_include_directories(macos-search PRIVATE src)

target_link_libraries(macos-search PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Concurrent
    "-framework Carbon"
)

set_target_properties(macos-search PROPERTIES
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_BUNDLE_NAME "macos-search"
    MACOSX_BUNDLE_GUI_IDENTIFIER "de.v-und-s.macos-search"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    MACOSX_BUNDLE_ICON_FILE "macos-search.icns"
)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
set(MACOS_SEARCH_TEST_SOURCES
    tests/test_main.cpp
    tests/PathCacheManagerTest.cpp
    tests/PathCacheManagerTest.h
    tests/SearchFieldTest.cpp
    tests/SearchFieldTest.h
    tests/ExcludeSettingsTest.cpp
    tests/ExcludeSettingsTest.h
    tests/PathSelectorStateTest.cpp
    tests/PathSelectorStateTest.h
    tests/FolderBrowserDialogTest.cpp
    tests/FolderBrowserDialogTest.h
    tests/UserInteractionTest.cpp
    tests/UserInteractionTest.h
    tests/UsabilityTest.cpp
    tests/UsabilityTest.h
    tests/CacheStrategyTest.cpp
    tests/CacheStrategyTest.h
    tests/AutostartTest.cpp
    tests/AutostartTest.h
    tests/GlobalHotkeyTest.cpp
    tests/GlobalHotkeyTest.h
    ${MACOS_SEARCH_LIB_SOURCES}
    assets/icons.qrc
)

qt_add_executable(macos-search_tests ${MACOS_SEARCH_TEST_SOURCES})

target_include_directories(macos-search_tests PRIVATE src)

target_link_libraries(macos-search_tests PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Concurrent
    Qt6::Test
    "-framework Carbon"
)

set_target_properties(macos-search_tests PROPERTIES
    MACOSX_BUNDLE FALSE
)

add_test(NAME macos-search_tests COMMAND macos-search_tests)
