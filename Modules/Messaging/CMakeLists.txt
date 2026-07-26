cmake_minimum_required(VERSION 3.16)

project(MicroWorldMessaging VERSION 0.3.0 LANGUAGES CXX)

option(
    MICROWORLD_MESSAGING_BUILD_TESTS
    "Build MicroWorld Messaging host tests"
    ON
)

# A standalone Messaging configuration composes its sole production dependency.
# Larger profiles may instead provide Core before adding this directory.
if(NOT TARGET MicroWorld::Core)
    set(MICROWORLD_BUILD_TESTS OFF)
    add_subdirectory(
        "${CMAKE_CURRENT_SOURCE_DIR}/../Core"
        "${CMAKE_CURRENT_BINARY_DIR}/Core"
    )
endif()

# Messaging is header-only: callers select all capacities through template
# parameters, so no production translation unit or archive is required. The
# INTERFACE target carries only the Core include and link boundary.
add_library(microworld_messaging INTERFACE)
target_include_directories(microworld_messaging INTERFACE
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
)
target_compile_features(microworld_messaging INTERFACE cxx_std_17)
target_link_libraries(microworld_messaging INTERFACE MicroWorld::Core)
add_library(MicroWorld::Messaging ALIAS microworld_messaging)

# Keeps the Messaging test executable under the portable no-exception,
# no-runtime-type-information boundary. The public INTERFACE target inherits
# its consumers' strict options and does not compile sources itself.
function(microworld_messaging_enable_strict_portable_options Target)
    if(MSVC)
        target_compile_options(${Target} PRIVATE /W4 /WX /Zc:__cplusplus)
    else()
        target_compile_options(${Target} PRIVATE
            -Wall -Wextra -Wpedantic -Werror -fno-exceptions -fno-rtti
        )
    endif()
endfunction()

function(microworld_messaging_disable_exceptions_and_rtti SourceFiles)
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

if(MICROWORLD_MESSAGING_BUILD_TESTS)
    include(CTest)

    # The moved channel tests retain Engine allocation counters and deterministic
    # Engine/Net host-loopback fixtures. These test-only targets do not expand
    # Messaging's public Core-only dependency boundary.
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

    set(MICROWORLD_MESSAGING_TEST_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/../Core/tests/TestMain.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/../Engine/tests/EngineAllocationCounters.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/MessageCodecTests.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/MessageRouterTests.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/MessageChannelTests.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/ReliableChannelTests.cpp"
    )

    if(BUILD_TESTING)
        foreach(MICROWORLD_MESSAGING_TEST_SOURCE IN LISTS MICROWORLD_MESSAGING_TEST_SOURCES)
            if(NOT EXISTS "${MICROWORLD_MESSAGING_TEST_SOURCE}")
                message(
                    FATAL_ERROR
                    "MicroWorld Messaging required test source is missing: ${MICROWORLD_MESSAGING_TEST_SOURCE}"
                )
            endif()
        endforeach()

        add_executable(microworld_messaging_tests ${MICROWORLD_MESSAGING_TEST_SOURCES})
        target_include_directories(microworld_messaging_tests PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/../Core/tests"
            "${CMAKE_CURRENT_SOURCE_DIR}/../Engine/tests"
        )
        target_link_libraries(microworld_messaging_tests PRIVATE
            MicroWorld::Messaging
            MicroWorld::Engine
            MicroWorld::Net
        )
        microworld_messaging_enable_strict_portable_options(microworld_messaging_tests)
        microworld_messaging_disable_exceptions_and_rtti(
            "${MICROWORLD_MESSAGING_TEST_SOURCES}"
        )
        add_test(NAME microworld_messaging_tests COMMAND microworld_messaging_tests)
    endif()
endif()
