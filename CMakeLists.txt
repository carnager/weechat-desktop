cmake_minimum_required(VERSION 3.21)
project(weechat-desktop VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.9 REQUIRED COMPONENTS Core Gui Quick QuickControls2 WebSockets Network Widgets Svg)
find_package(Qt6 6.9 QUIET COMPONENTS DBus Test)

qt_standard_project_setup(REQUIRES 6.9)

# Everything except main() lives in a static library so the tests can link against it.
add_library(weechat_core STATIC
    src/RelayConnection.cpp src/RelayConnection.h
    src/Session.cpp src/Session.h
    src/Buffer.cpp src/Buffer.h
    src/BufferListModel.cpp src/BufferListModel.h
    src/LineModel.cpp src/LineModel.h
    src/NickModel.cpp src/NickModel.h
    src/Formatter.cpp src/Formatter.h
    src/EmojiStore.cpp src/EmojiStore.h
    src/Settings.cpp src/Settings.h
    src/LinkPreviews.cpp src/LinkPreviews.h
    src/Notifier.cpp src/Notifier.h
    src/Completer.cpp src/Completer.h
    src/NowPlaying.cpp src/NowPlaying.h
    src/Protocol.cpp src/Protocol.h
)
target_include_directories(weechat_core PUBLIC src)
target_link_libraries(weechat_core PUBLIC
    Qt6::Core Qt6::Gui Qt6::Quick Qt6::WebSockets Qt6::Network Qt6::Widgets)
if (TARGET Qt6::DBus)
    target_link_libraries(weechat_core PUBLIC Qt6::DBus)
    target_compile_definitions(weechat_core PUBLIC HAVE_DBUS=1)
endif()

qt_add_resources(weechat_core "data"
    PREFIX "/"
    FILES resources/emoji.json resources/icon.svg)

qt_add_executable(weechat-desktop src/main.cpp)
target_link_libraries(weechat-desktop PRIVATE weechat_core Qt6::QuickControls2 Qt6::Svg)
target_compile_definitions(weechat-desktop PRIVATE WD_VERSION="${PROJECT_VERSION}")

# Development hooks (screenshots, simulated input, frame recording); off in Release builds.
if (CMAKE_BUILD_TYPE STREQUAL "Release")
    set(WD_DEVTOOLS_DEFAULT OFF)
else()
    set(WD_DEVTOOLS_DEFAULT ON)
endif()
option(WD_DEVTOOLS "Build the development hooks (see src/DevTools.h)" ${WD_DEVTOOLS_DEFAULT})
if (WD_DEVTOOLS)
    target_sources(weechat-desktop PRIVATE src/DevTools.cpp src/DevTools.h)
    target_compile_definitions(weechat-desktop PRIVATE WD_DEVTOOLS=1)
endif()
set(QML_SOURCES
    qml/Main.qml
    qml/Theme.qml
    qml/Sidebar.qml
    qml/BufferRow.qml
    qml/ChatView.qml
    qml/ChatHeader.qml
    qml/MessageDelegate.qml
    qml/LinkPreview.qml
    qml/InputBar.qml
    qml/CompletionPopup.qml
    qml/EmojiPicker.qml
    qml/NickList.qml
    qml/QuickSwitcher.qml
    qml/ConnectView.qml
    qml/SettingsDialog.qml
    qml/ShortcutsDialog.qml
    qml/WhoisWindow.qml
    qml/CertificateDialog.qml
    qml/MentionsPanel.qml
    qml/LinksPanel.qml
    qml/NickMenu.qml
    qml/IconButton.qml
    qml/Icon.qml
    qml/Badge.qml
    qml/Avatar.qml
    qml/StatusBanner.qml
    qml/AppTextField.qml
    qml/Toast.qml
    qml/AppButton.qml
    qml/AppSwitch.qml
    qml/Segmented.qml
    qml/AppScrollBar.qml
)
# Serve the files from the module root (not qml/), so the module's qmldir — and with it the
# Theme singleton — is what files see, rather than an implicit import of their own directory.
foreach(file IN LISTS QML_SOURCES)
    get_filename_component(alias ${file} NAME)
    set_source_files_properties(${file} PROPERTIES QT_RESOURCE_ALIAS ${alias})
endforeach()
set_source_files_properties(qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(weechat-desktop
    URI WeechatDesktop
    VERSION 1.0
    QML_FILES ${QML_SOURCES}
)

if (TARGET Qt6::Test)
    enable_testing()
    qt_add_executable(weechat-tests tests/tst_core.cpp)
    target_link_libraries(weechat-tests PRIVATE weechat_core Qt6::Test)
    add_test(NAME core COMMAND weechat-tests)
endif()

# Headless UI checks against the mock relay (needs the dev hooks and Python's websockets).
find_package(Python3 COMPONENTS Interpreter)
if (WD_DEVTOOLS AND Python3_Interpreter_FOUND)
    enable_testing()
    add_test(NAME ui COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/ui_checks.py
             --app $<TARGET_FILE:weechat-desktop> --mock ${CMAKE_SOURCE_DIR}/tools/mock_relay.py)
    set_tests_properties(ui PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 400)
endif()

include(GNUInstallDirs)
install(TARGETS weechat-desktop RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES resources/weechat-desktop.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES resources/icon.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps
        RENAME weechat-desktop.svg)
