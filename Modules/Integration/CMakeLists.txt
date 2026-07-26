cmake_minimum_required(VERSION 3.16)

project(MicroWorldIntegration VERSION 0.3.0 LANGUAGES CXX)

option(
    MICROWORLD_INTEGRATION_BUILD_TESTS
    "Build MicroWorld Integration host tests"
    ON
)

# A standalone Integration configuration composes its inward dependencies. A
# larger profile may instead provide these targets before adding this directory.
# Integration is the only package allowed to see both Engine and Net: it owns the
# one object (TNetSystem) that turns net drivers into a working networked engine.
if(NOT TARGET MicroWorld::Messaging)
    set(MICROWORLD_MESSAGING_BUILD_TESTS OFF)
    add_subdirectory(
        "${CMAKE_CURRENT_SOURCE_DIR}/../Messaging"
        "${CMAKE_CURRENT_BINARY_DIR}/Messaging"
    )
endif()
if(NOT TARGET MicroWorld::Engine)
    set(MICROWORLD_ENGINE_BUILD_TESTS OFF)
    set(MICROWORLD_ENGINE_BUILD_BENCHMARKS OFF)
    add_subdirectory(
        "${CMAKE_CURRENT_SOURCE_DIR}/../Engine"
        "${CMAKE_CURRENT_BINARY_DIR}/Engine"
    )
endif()
if(NOT TARGET MicroWorld::Net)
    set(MICROWORLD_NET_BUILD_TESTS OFF)
    set(MICROWORLD_NET_BUILD_BENCHMARKS OFF)
    add_subdirectory(
        "${CMAKE_CURRENT_SOURCE_DIR}/../Net"
        "${CMAKE_CURRENT_BINARY_DIR}/Net"
    )
endif()

# Integration is header-only: TNetSystem is a template instantiated by the
# caller, so there is no production translation unit. The INTERFACE library
# carries the include path and the Messaging+Engine+Net link edges for consumers.
add_library(microworld_integration INTERFACE)
target_include_directories(microworld_integration INTERFACE
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
)
target_compile_features(microworld_integration INTERFACE cxx_std_17)
target_link_libraries(microworld_integration INTERFACE
    MicroWorld::Messaging
    MicroWorld::Engine
    MicroWorld::Net
)
add_library(MicroWorld::Integration ALIAS microworld_integration)

# Keeps the portable Integration boundary under the same strict, no-runtime-type-
# information gate as Engine and Net.
function(microworld_integration_enable_strict_portable_options Target)
    if(MSVC)
        target_compile_options(${Target} PRIVATE /W4 /WX /Zc:__cplusplus)
    else()
        target_compile_options(${Target} PRIVATE
            -Wall -Wextra -Wpedantic -Werror -fno-exceptions -fno-rtti
        )
    endif()
endfunction()

function(microworld_integration_disable_exceptions_and_rtti SourceFiles)
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

if(MICROWORLD_INTEGRATION_BUILD_TESTS)
    include(CTest)

    set(MICROWORLD_INTEGRATION_TEST_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/../Core/tests/TestMain.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/EngineNetHostTests.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/NetSystemTests.cpp"
    )

    if(BUILD_TESTING)
        foreach(MICROWORLD_INTEGRATION_TEST_SOURCE IN LISTS MICROWORLD_INTEGRATION_TEST_SOURCES)
            if(NOT EXISTS "${MICROWORLD_INTEGRATION_TEST_SOURCE}")
                message(
                    FATAL_ERROR
                    "MicroWorld Integration required test source is missing: ${MICROWORLD_INTEGRATION_TEST_SOURCE}"
                )
            endif()
        endforeach()

        add_executable(microworld_integration_tests ${MICROWORLD_INTEGRATION_TEST_SOURCES})
        target_include_directories(microworld_integration_tests PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/../Core/tests"
        )
        target_link_libraries(microworld_integration_tests PRIVATE MicroWorld::Integration)
        microworld_integration_enable_strict_portable_options(microworld_integration_tests)
        microworld_integration_disable_exceptions_and_rtti(
            "${MICROWORLD_INTEGRATION_TEST_SOURCES}"
        )
        add_test(NAME microworld_integration_tests COMMAND microworld_integration_tests)
    endif()
endif()
