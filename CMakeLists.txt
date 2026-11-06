cmake_minimum_required(VERSION 3.21)

project(Nyan-Q VERSION 0.1.4 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC OFF)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Qt6 6.12 REQUIRED COMPONENTS Core Gui Widgets Network)
qt_standard_project_setup(REQUIRES 6.12)

set(NYANQ_SOURCES
    src/main.cpp

    src/app/AppPaths.cpp
    src/app/Elevation.cpp
    src/app/Fonts.cpp
    src/app/MenuStyler.cpp
    src/app/RoundedPopupFilter.cpp
    src/app/ScrollBarStyler.cpp
    src/app/Theme.cpp

    src/core/Formatters.cpp
    src/core/SettingsStore.cpp
    src/core/ProfileStore.cpp
    src/core/RuntimeConfig.cpp
    src/core/MihomoProcess.cpp
    src/core/MihomoApi.cpp
    src/core/NetworkProbe.cpp
    src/core/SystemProxy.cpp
    src/core/ClashController.cpp

    src/models/RuleListModel.cpp
    src/models/ConnectionListModel.cpp

    src/lsp/LspClient.cpp
    src/lsp/LspServerRegistry.cpp

    src/ui/MainWindow.cpp
    src/ui/Sidebar.cpp
    src/ui/TitleBar.cpp
    src/ui/PageHeader.cpp

    src/ui/widgets/MaterialWidgets.cpp
    src/ui/widgets/TrafficChart.cpp
    src/ui/widgets/PowerOrb.cpp
    src/ui/widgets/ProxyNodeCard.cpp
    src/ui/widgets/ProfileCard.cpp
    src/ui/widgets/SiteLatencyTile.cpp
    src/ui/widgets/SearchField.cpp
    src/ui/widgets/FlowLayout.cpp

    src/ui/editor/CodeEditor.cpp
    src/ui/editor/ConfigEditorView.cpp

    src/ui/pages/DashboardPage.cpp
    src/ui/pages/ProxiesPage.cpp
    src/ui/pages/ProfilesPage.cpp
    src/ui/pages/ConnectionsPage.cpp
    src/ui/pages/RulesPage.cpp
    src/ui/pages/LogsPage.cpp
    src/ui/pages/SettingsPage.cpp

    src/ui/dialogs/MaterialDialog.cpp
    src/ui/dialogs/ProfileDialog.cpp
    src/ui/dialogs/ConfirmDialog.cpp

    resources/resources.qrc
)

file(GLOB_RECURSE NYANQ_HEADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h")

qt_add_executable(NyanQ WIN32 ${NYANQ_SOURCES} ${NYANQ_HEADERS})
set_target_properties(NyanQ PROPERTIES OUTPUT_NAME "Nyan-Q")

target_include_directories(NyanQ PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui
)

target_link_libraries(NyanQ PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
)

target_compile_definitions(NyanQ PRIVATE
    NYANQ_VERSION="${PROJECT_VERSION}"
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

if(MSVC)
    target_compile_options(NyanQ PRIVATE /utf-8 /W3 /permissive- /Zc:__cplusplus)
endif()

if(WIN32)
    target_link_libraries(NyanQ PRIVATE dwmapi wininet shell32 advapi32 psapi)
    configure_file(resources/app.rc.in ${CMAKE_CURRENT_BINARY_DIR}/app.rc @ONLY)
    target_sources(NyanQ PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/app.rc)
endif()

# Ship the Mihomo core beside the executable. It is an external process and
# never linked into the Qt application.
set(MIHOMO_BINARY "${CMAKE_CURRENT_SOURCE_DIR}/third_party/mihomo/mihomo.exe")
set(WINTUN_BINARY "${CMAKE_CURRENT_SOURCE_DIR}/third_party/mihomo/wintun.dll")
if(EXISTS "${MIHOMO_BINARY}")
    add_custom_command(TARGET NyanQ POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${MIHOMO_BINARY}" "$<TARGET_FILE_DIR:NyanQ>/mihomo.exe"
        COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_FILE_DIR:NyanQ>/lsp"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/resources/clash-config.schema.json"
                "$<TARGET_FILE_DIR:NyanQ>/lsp/clash-config.schema.json"
        COMMENT "Bundling Mihomo core"
        VERBATIM)
    if(EXISTS "${WINTUN_BINARY}")
        add_custom_command(TARGET NyanQ POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${WINTUN_BINARY}" "$<TARGET_FILE_DIR:NyanQ>/wintun.dll"
            COMMENT "Bundling Wintun TUN driver"
            VERBATIM)
    endif()
else()
    message(WARNING "Mihomo core not found at ${MIHOMO_BINARY}; runtime will require a core path in Settings.")
endif()

install(TARGETS NyanQ RUNTIME DESTINATION .)
if(EXISTS "${MIHOMO_BINARY}")
    install(FILES "${MIHOMO_BINARY}" DESTINATION .)
endif()
if(EXISTS "${WINTUN_BINARY}")
    install(FILES "${WINTUN_BINARY}" DESTINATION .)
endif()
install(FILES
    README.md
    THIRD_PARTY_NOTICES.md
    lsp-servers.example.json
    LICENSE
    DESTINATION .)
install(DIRECTORY licenses DESTINATION .)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/docs")
    install(DIRECTORY docs DESTINATION .)
endif()

# Convenience target used by scripts/package.ps1.
find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS "${QT6_INSTALL_PREFIX}/bin" "${Qt6_DIR}/../../../bin")
if(WINDEPLOYQT_EXECUTABLE)
    add_custom_target(deploy
        COMMAND "${WINDEPLOYQT_EXECUTABLE}" --release --no-translations --no-opengl-sw
                --compiler-runtime --dir "$<TARGET_FILE_DIR:NyanQ>" "$<TARGET_FILE:NyanQ>"
        DEPENDS NyanQ
        COMMENT "Deploying Qt runtime beside Nyan-Q.exe"
        VERBATIM)
endif()

include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
