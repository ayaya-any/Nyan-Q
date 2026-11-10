include(CTest)

if(NOT BUILD_TESTING)
    return()
endif()

find_package(Qt6 6.12 REQUIRED COMPONENTS Test)

function(nyanq_add_test name)
    cmake_parse_arguments(TEST "" "" "SOURCES;LIBS" ${ARGN})
    qt_add_executable(${name} ${TEST_SOURCES})
    target_include_directories(${name} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../src)
    target_link_libraries(${name} PRIVATE Qt6::Test Qt6::Core Qt6::Network ${TEST_LIBS})
    if(MSVC)
        target_compile_options(${name} PRIVATE /utf-8 /W3 /permissive-)
    endif()
    add_test(NAME ${name} COMMAND ${name})
    if(WIN32)
        set_tests_properties(${name} PROPERTIES
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
    endif()
endfunction()

nyanq_add_test(test_runtime_config
    SOURCES
        test_runtime_config.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/core/RuntimeConfig.cpp
    LIBS Qt6::Core
)

nyanq_add_test(test_api_parsers
    SOURCES
        test_api_parsers.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/core/MihomoApi.cpp
    LIBS Qt6::Network
)

nyanq_add_test(test_profile_store
    SOURCES
        test_profile_store.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/core/ProfileStore.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../resources/resources.qrc
    LIBS Qt6::Network
)

nyanq_add_test(test_yaml_document
    SOURCES
        test_yaml_document.cpp
        yaml_sample.cpp
    LIBS nyanq_scripting
)

nyanq_add_test(test_language_services
    SOURCES
        test_language_services.cpp
    LIBS nyanq_lang
)

nyanq_add_test(test_code_editor
    SOURCES
        test_code_editor.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/ui/editor/CodeEditor.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/app/Theme.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/app/Fonts.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../resources/resources.qrc
    LIBS nyanq_lang Qt6::Widgets Qt6::Gui
)

nyanq_add_test(test_message_dialog
    SOURCES
        test_message_dialog.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/ui/dialogs/MessageDialog.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/ui/dialogs/MaterialDialog.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/ui/widgets/MaterialWidgets.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/app/Theme.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/app/Fonts.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../resources/resources.qrc
    LIBS Qt6::Widgets Qt6::Gui
)

nyanq_add_test(test_script_engine
    SOURCES
        test_script_engine.cpp
        yaml_sample.cpp
    LIBS nyanq_scripting
)

nyanq_add_test(test_script_repository
    SOURCES
        test_script_repository.cpp
    LIBS nyanq_scripting
)

nyanq_add_test(test_script_binding_manager
    SOURCES
        test_script_binding_manager.cpp
    LIBS nyanq_scripting
)

nyanq_add_test(test_script_pipeline
    SOURCES
        test_script_pipeline.cpp
        yaml_sample.cpp
    LIBS nyanq_scripting
)

nyanq_add_test(test_script_downloader
    SOURCES
        test_script_downloader.cpp
        http_test_server.cpp
    LIBS nyanq_scripting
)

nyanq_add_test(test_script_manager
    SOURCES
        test_script_manager.cpp
        http_test_server.cpp
        yaml_sample.cpp
    LIBS nyanq_scripting
)

nyanq_add_test(test_mihomo_config_validator
    SOURCES
        test_mihomo_config_validator.cpp
    LIBS nyanq_scripting
)

nyanq_add_test(test_mihomo_load
    SOURCES
        test_mihomo_load.cpp
        yaml_sample.cpp
    LIBS nyanq_scripting
)

nyanq_add_test(test_config_composer
    SOURCES
        test_config_composer.cpp
        yaml_sample.cpp
    LIBS nyanq_scripting
)
