cmake_minimum_required(VERSION 3.16)

project(MicroWorldApplication VERSION 0.3.0 LANGUAGES CXX)

option(
    MICROWORLD_APPLICATION_BUILD_TESTS
    "Build MicroWorld Application host tests"
    ON
)

# A standalone Application configuration composes its inward dependencies. A
# larger profile may instead provide these targets before adding this directory.
# Application is the first package that owns an engine: it links Engine, which
# transitively brings Object and Core.
if(NOT TARGET MicroWorld::Engine)
    set(MICROWORLD_ENGINE_BUILD_TESTS OFF)
    set(MICROWORLD_ENGINE_BUILD_BENCHMARKS OFF)
    add_subdirectory(
        "${CMAKE_CURRENT_SOURCE_DIR}/../Engine"
        "${CMAKE_CURRENT_BINARY_DIR}/Engine"
    )
endif()

set(MICROWORLD_APPLICATION_PRODUCTION_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Application.cpp"
)

add_library(microworld_application STATIC ${MICROWORLD_APPLICATION_PRODUCTION_SOURCES})
target_include_directories(microworld_application PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
)
target_compile_features(microworld_application PUBLIC cxx_std_17)
target_link_libraries(microworld_application PUBLIC MicroWorld::Engine)
add_library(MicroWorld::Application ALIAS microworld_application)

# Portable application code is validated under the same strict, no-runtime-type-
# information boundary as Engine.
function(microworld_application_enable_strict_portable_options Target)
    if(MSVC)
        target_compile_options(${Target} PRIVATE /W4 /WX /Zc:__cplusplus)
    else()
        target_compile_options(${Target} PRIVATE
            -Wall -Wextra -Wpedantic -Werror -fno-exceptions -fno-rtti
        )
    endif()
endfunction()

function(microworld_application_disable_exceptions_and_rtti SourceFiles)
    if(MSVC)
        if(
            CMAKE_GENERATOR MATCHES "^Visual Studio"
            AND CMAKE_VERSION VERSION_GREATER_EQUAL "3.18"
        )
            set_property(
                SOURCE ${SourceFiles}
                PROPERTY VS_SETTINGS
                    "ExceptionHandling=false"
                    "RuntimeTypeInfo=false"
            )
        else()
            set_source_files_properties(
                ${SourceFiles}
                PROPERTIES COMPILE_OPTIONS "/EHs-c-;/GR-"
            )
        endif()
    endif()
endfunction()

microworld_application_enable_strict_portable_options(microworld_application)
microworld_application_disable_exceptions_and_rtti(
    "${MICROWORLD_APPLICATION_PRODUCTION_SOURCES}"
)

if(MICROWORLD_APPLICATION_BUILD_TESTS)
    include(CTest)

    set(MICROWORLD_APPLICATION_TEST_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/../Core/tests/TestMain.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/ApplicationTests.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/ApplicationRunnerTests.cpp"
    )

    if(BUILD_TESTING)
        foreach(MICROWORLD_APPLICATION_TEST_SOURCE IN LISTS MICROWORLD_APPLICATION_TEST_SOURCES)
            if(NOT EXISTS "${MICROWORLD_APPLICATION_TEST_SOURCE}")
                message(
                    FATAL_ERROR
                    "MicroWorld Application required test source is missing: ${MICROWORLD_APPLICATION_TEST_SOURCE}"
                )
            endif()
        endforeach()

        add_executable(microworld_application_tests ${MICROWORLD_APPLICATION_TEST_SOURCES})
        target_include_directories(microworld_application_tests PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/../Core/tests"
        )
        target_link_libraries(microworld_application_tests PRIVATE MicroWorld::Application)
        microworld_application_enable_strict_portable_options(microworld_application_tests)
        microworld_application_disable_exceptions_and_rtti(
            "${MICROWORLD_APPLICATION_TEST_SOURCES}"
        )
        add_test(NAME microworld_application_tests COMMAND microworld_application_tests)
    endif()
endif()
