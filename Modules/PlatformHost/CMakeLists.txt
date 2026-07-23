cmake_minimum_required(VERSION 3.16)

project(MicroWorldPlatformHost VERSION 0.3.0 LANGUAGES CXX)

option(
    MICROWORLD_PLATFORM_HOST_BUILD_TESTS
    "Build MicroWorld PlatformHost host tests"
    ON
)

# PlatformHost is the first NON-PORTABLE package: it may include OS socket
# headers and is therefore deliberately not governed by
# CheckDependencyBoundaries.py. It composes the adjacent Net package for
# INetDriver/FNetAddress/ENetResult and reaches Core transitively for Time.h.
if(NOT TARGET MicroWorld::Net)
    set(MICROWORLD_NET_BUILD_TESTS OFF)
    set(MICROWORLD_NET_BUILD_BENCHMARKS OFF)
    add_subdirectory(
        "${CMAKE_CURRENT_SOURCE_DIR}/../Net"
        "${CMAKE_CURRENT_BINARY_DIR}/Net"
    )
endif()

# The Phase 6.1 two-node demo composes the engine's TEngineHost (which owns
# UWorld/AActor/GC) on top of a PlatformHost UDP driver, so the example links
# MicroWorld::Engine directly. Engine never depends on Net (the frame seam is
# caller-bound via TNetHostFrame), so adding it here introduces no cycle.
if(NOT TARGET MicroWorld::Engine)
    set(MICROWORLD_ENGINE_BUILD_BENCHMARKS OFF)
    add_subdirectory(
        "${CMAKE_CURRENT_SOURCE_DIR}/../Engine"
        "${CMAKE_CURRENT_BINARY_DIR}/Engine"
    )
endif()

set(MICROWORLD_PLATFORM_HOST_PRODUCTION_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/HostUdpDriver.cpp"
)

add_library(microworld_platform_host STATIC ${MICROWORLD_PLATFORM_HOST_PRODUCTION_SOURCES})
add_library(MicroWorld::PlatformHost ALIAS microworld_platform_host)

target_include_directories(microworld_platform_host PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
)
target_compile_features(microworld_platform_host PUBLIC cxx_std_17)
target_link_libraries(microworld_platform_host PUBLIC MicroWorld::Net)
if(WIN32)
    target_link_libraries(microworld_platform_host PUBLIC ws2_32)
endif()

# Keeps the platform adapter under the same strict, no-runtime-type-information
# gate as the portable packages so the host socket path introduces no hidden
# dependency or undefined behavior surface.
function(microworld_platform_host_enable_strict_portable_options Target)
    if(MSVC)
        target_compile_options(${Target} PRIVATE /W4 /WX /Zc:__cplusplus)
    else()
        target_compile_options(${Target} PRIVATE
            -Wall -Wextra -Wpedantic -Werror -fno-exceptions -fno-rtti
        )
    endif()
endfunction()

function(microworld_platform_host_disable_exceptions_and_rtti SourceFiles)
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

microworld_platform_host_enable_strict_portable_options(microworld_platform_host)
microworld_platform_host_disable_exceptions_and_rtti(
    "${MICROWORLD_PLATFORM_HOST_PRODUCTION_SOURCES}"
)

if(MICROWORLD_PLATFORM_HOST_BUILD_TESTS)
    include(CTest)

    set(MICROWORLD_PLATFORM_HOST_TEST_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/../Core/tests/TestMain.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/HostTimeSourceTests.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/HostUdpDriverTests.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/HostNetEndToEndTests.cpp"
    )

    if(BUILD_TESTING)
        foreach(MICROWORLD_PLATFORM_HOST_TEST_SOURCE IN LISTS MICROWORLD_PLATFORM_HOST_TEST_SOURCES)
            if(NOT EXISTS "${MICROWORLD_PLATFORM_HOST_TEST_SOURCE}")
                message(
                    FATAL_ERROR
                    "MicroWorld PlatformHost required test source is missing: ${MICROWORLD_PLATFORM_HOST_TEST_SOURCE}"
                )
            endif()
        endforeach()

        add_executable(microworld_platform_host_tests ${MICROWORLD_PLATFORM_HOST_TEST_SOURCES})
        target_include_directories(microworld_platform_host_tests PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/../Core/tests"
        )
        target_link_libraries(microworld_platform_host_tests PRIVATE MicroWorld::PlatformHost)
        microworld_platform_host_enable_strict_portable_options(microworld_platform_host_tests)
        microworld_platform_host_disable_exceptions_and_rtti(
            "${MICROWORLD_PLATFORM_HOST_TEST_SOURCES}"
        )
        add_test(NAME microworld_platform_host_tests COMMAND microworld_platform_host_tests)
    endif()
endif()

# The Phase 6.1 two-node acceptance demo: one process hosts a dedicated-server
# TEngineHost and a bare TNetHost client over real localhost UDP. A host-UDP
# consumer MUST live in this non-portable package because only platform-host may
# reach OS socket headers.
set(MICROWORLD_PLATFORM_HOST_EXAMPLE_SOURCE
    "${CMAKE_CURRENT_SOURCE_DIR}/examples/TwoNodeDemo/Main.cpp"
)
if(EXISTS "${MICROWORLD_PLATFORM_HOST_EXAMPLE_SOURCE}")
    add_executable(microworld_platform_host_two_node_demo ${MICROWORLD_PLATFORM_HOST_EXAMPLE_SOURCE})
    target_link_libraries(microworld_platform_host_two_node_demo PRIVATE
        MicroWorld::PlatformHost
        MicroWorld::Engine
    )
    microworld_platform_host_enable_strict_portable_options(microworld_platform_host_two_node_demo)
    microworld_platform_host_disable_exceptions_and_rtti(
        "${MICROWORLD_PLATFORM_HOST_EXAMPLE_SOURCE}"
    )
endif()
