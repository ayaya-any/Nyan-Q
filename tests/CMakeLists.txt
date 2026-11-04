include(CTest)

if(NOT BUILD_TESTING)
    return()
endif()

find_package(Qt6 6.12 REQUIRED COMPONENTS Test)

function(clashq_add_test name)
    cmake_parse_arguments(TEST "" "" "SOURCES;LIBS" ${ARGN})
    qt_add_executable(${name} ${TEST_SOURCES})
    target_include_directories(${name} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../src)
    target_link_libraries(${name} PRIVATE Qt6::Test Qt6::Core ${TEST_LIBS})
    if(MSVC)
        target_compile_options(${name} PRIVATE /utf-8 /W3 /permissive-)
    endif()
    add_test(NAME ${name} COMMAND ${name})
    if(WIN32)
        set_tests_properties(${name} PROPERTIES
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
    endif()
endfunction()

clashq_add_test(test_runtime_config
    SOURCES
        test_runtime_config.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/core/RuntimeConfig.cpp
    LIBS Qt6::Core
)

clashq_add_test(test_api_parsers
    SOURCES
        test_api_parsers.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/core/MihomoApi.cpp
    LIBS Qt6::Network
)

clashq_add_test(test_profile_store
    SOURCES
        test_profile_store.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/core/ProfileStore.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../resources/resources.qrc
    LIBS Qt6::Network
)
