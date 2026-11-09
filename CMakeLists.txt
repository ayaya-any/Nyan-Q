cmake_minimum_required(VERSION 3.21)

project(Nyan-Q VERSION 0.1.4 LANGUAGES C CXX)

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

# ---------------------------------------------------------------------------
# JavaScript configuration override engine
#
# QuickJS-ng is fetched at configure time with FetchContent (pinned archive and
# SHA-256). Set -DFETCHCONTENT_SOURCE_DIR_QUICKJS=<dir> for an offline build.
# libyaml backs the order-preserving Mihomo config DOM and is fetched by
# scripts/fetch-deps.ps1 because the repository does not track vendored source.
# ---------------------------------------------------------------------------
include(FetchContent)
set(NYANQ_LIBYAML_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libyaml")
if(NOT EXISTS "${NYANQ_LIBYAML_DIR}/include/yaml.h")
    message(FATAL_ERROR
        "libyaml sources are missing. Run scripts/fetch-deps.ps1 before configuring.")
endif()

FetchContent_Declare(quickjs
    URL https://github.com/quickjs-ng/quickjs/releases/download/v0.18.0/quickjs-amalgam.zip
    URL_HASH SHA256=6C05066A6B62B483E2BA0F83D7A16688C6958182B214BFD693529434CEF84A9E
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(quickjs)
if(NOT EXISTS "${quickjs_SOURCE_DIR}/quickjs-amalgam.c")
    message(FATAL_ERROR
        "QuickJS FetchContent did not provide quickjs-amalgam.c (source: ${quickjs_SOURCE_DIR}).")
endif()

add_library(nyanq_quickjs STATIC "${quickjs_SOURCE_DIR}/quickjs-amalgam.c")
target_include_directories(nyanq_quickjs PUBLIC "${quickjs_SOURCE_DIR}")
target_compile_definitions(nyanq_quickjs PRIVATE _CRT_SECURE_NO_WARNINGS)
set_target_properties(nyanq_quickjs PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)
if(MSVC)
    target_compile_options(nyanq_quickjs PRIVATE /std:c11 /experimental:c11atomics /W0)
else()
    target_link_libraries(nyanq_quickjs PUBLIC m ${CMAKE_DL_LIBS})
endif()

set(NYANQ_LIBYAML_CONFIG_DIR "${CMAKE_CURRENT_BINARY_DIR}/libyaml-config")
file(MAKE_DIRECTORY "${NYANQ_LIBYAML_CONFIG_DIR}")
file(WRITE "${NYANQ_LIBYAML_CONFIG_DIR}/config.h"
"#define YAML_VERSION_MAJOR 0
#define YAML_VERSION_MINOR 2
#define YAML_VERSION_PATCH 5
#define YAML_VERSION_STRING \"0.2.5\"
")
add_library(nyanq_yaml STATIC
    "${NYANQ_LIBYAML_DIR}/src/api.c"
    "${NYANQ_LIBYAML_DIR}/src/dumper.c"
    "${NYANQ_LIBYAML_DIR}/src/emitter.c"
    "${NYANQ_LIBYAML_DIR}/src/loader.c"
    "${NYANQ_LIBYAML_DIR}/src/parser.c"
    "${NYANQ_LIBYAML_DIR}/src/reader.c"
    "${NYANQ_LIBYAML_DIR}/src/scanner.c"
    "${NYANQ_LIBYAML_DIR}/src/writer.c"
)
target_include_directories(nyanq_yaml PUBLIC "${NYANQ_LIBYAML_DIR}/include")
target_include_directories(nyanq_yaml PRIVATE "${NYANQ_LIBYAML_CONFIG_DIR}")
target_compile_definitions(nyanq_yaml
    PUBLIC YAML_DECLARE_STATIC
    PRIVATE HAVE_CONFIG_H _CRT_SECURE_NO_WARNINGS)

# Built-in language plugins used by the editors. Adding another language is a
# provider implementation plus one registration in LanguageRegistry.
add_library(nyanq_lang STATIC
    src/lang/LanguageRegistry.cpp
    src/lang/YamlLanguageProvider.cpp
    src/lang/JavaScriptLanguageProvider.cpp
)
target_include_directories(nyanq_lang PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(nyanq_lang PUBLIC Qt6::Core Qt6::Gui nyanq_yaml nyanq_quickjs)
if(MSVC)
    target_compile_options(nyanq_lang PRIVATE /utf-8 /W3 /permissive- /Zc:__cplusplus)
endif()

# Script storage, execution, orchestration and final config validation. This
# library is shared by the application and the automated tests.
add_library(nyanq_scripting STATIC
    src/scripts/YamlDocument.cpp
    src/scripts/ScriptTypes.cpp
    src/scripts/ScriptEngine.cpp
    src/scripts/ScriptPipeline.cpp
    src/scripts/ScriptRepository.cpp
    src/scripts/ScriptBindingManager.cpp
    src/scripts/ScriptDownloader.cpp
    src/scripts/ScriptLogManager.cpp
    src/scripts/ScriptManager.cpp
    src/scripts/MihomoConfigValidator.cpp
    src/scripts/ConfigComposer.cpp
    src/core/RuntimeConfig.cpp
)
target_include_directories(nyanq_scripting PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(nyanq_scripting
    PUBLIC Qt6::Core Qt6::Network nyanq_yaml nyanq_quickjs)
if(MSVC)
    target_compile_options(nyanq_scripting PRIVATE /utf-8 /W3 /permissive- /Zc:__cplusplus)
endif()

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
    src/core/MihomoProcess.cpp
    src/core/MihomoApi.cpp
    src/core/NetworkProbe.cpp
    src/core/SystemProxy.cpp
    src/core/ClashController.cpp

    src/models/RuleListModel.cpp
    src/models/ConnectionListModel.cpp

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
    src/ui/widgets/ScriptCard.cpp

    src/ui/editor/CodeEditor.cpp
    src/ui/editor/ConfigEditorView.cpp
    src/ui/editor/ScriptEditorView.cpp

    src/ui/pages/DashboardPage.cpp
    src/ui/pages/ProxiesPage.cpp
    src/ui/pages/ProfilesPage.cpp
    src/ui/pages/ConnectionsPage.cpp
    src/ui/pages/RulesPage.cpp
    src/ui/pages/LogsPage.cpp
    src/ui/pages/ScriptsPage.cpp
    src/ui/pages/SettingsPage.cpp

    src/ui/dialogs/MaterialDialog.cpp
    src/ui/dialogs/ProfileDialog.cpp
    src/ui/dialogs/ConfirmDialog.cpp
    src/ui/dialogs/ScriptDialogs.cpp

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
    nyanq_lang
    nyanq_scripting
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
    LICENSE
    DESTINATION .)
install(DIRECTORY licenses DESTINATION .)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/docs")
    install(DIRECTORY docs DESTINATION .)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/examples")
    install(DIRECTORY examples DESTINATION .)
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
