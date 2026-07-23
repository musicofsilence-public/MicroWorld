cmake_minimum_required(VERSION 3.16)

project(MicroWorldNet VERSION 0.3.0 LANGUAGES CXX)

option(
    MICROWORLD_NET_BUILD_BENCHMARKS
    "Build MicroWorld Net host benchmarks"
    OFF
)
option(
    MICROWORLD_NET_BUILD_TESTS
    "Build MicroWorld Net host tests"
    ON
)

# A standalone Net configuration composes the adjacent Memory package while a
# larger profile may provide the target before adding this directory. Net never
# depends on Object or Engine; its inward edge is Core <- Memory <- Net only.
if(NOT TARGET MicroWorld::Memory)
    set(MICROWORLD_MEMORY_BUILD_TESTS OFF)
    set(MICROWORLD_MEMORY_BUILD_BENCHMARKS OFF)
    add_subdirectory(
        "${CMAKE_CURRENT_SOURCE_DIR}/../Memory"
        "${CMAKE_CURRENT_BINARY_DIR}/Memory"
    )
endif()

set(MICROWORLD_NET_PRODUCTION_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/NetDriver.cpp"
)

add_library(microworld_net STATIC ${MICROWORLD_NET_PRODUCTION_SOURCES})
add_library(MicroWorld::Net ALIAS microworld_net)

target_include_directories(microworld_net PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
)
target_compile_features(microworld_net PUBLIC cxx_std_17)
target_link_libraries(microworld_net PUBLIC MicroWorld::Memory)

# Keeps the portable Net boundary under the same strict, no-runtime-type-
# information gate as Memory so byte I/O never introduces a hidden dependency.
function(microworld_net_enable_strict_portable_options Target)
    if(MSVC)
        target_compile_options(${Target} PRIVATE /W4 /WX /Zc:__cplusplus)
    else()
        target_compile_options(${Target} PRIVATE
            -Wall -Wextra -Wpedantic -Werror -fno-exceptions -fno-rtti
        )
    endif()
endfunction()

function(microworld_net_disable_exceptions_and_rtti SourceFiles)
    if(MSVC)
        if(
            CMAKE_GENERATOR MATCHES "^Visual Studio"
            AND CMAKE_VERSION VERSION_GREATER_EQUAL "3.18"
        )
            # This replaces Visual Studio's default /EHsc setting without a
            # conflicting command-line override on portable Net sources.
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

microworld_net_enable_strict_portable_options(microworld_net)
microworld_net_disable_exceptions_and_rtti(
    "${MICROWORLD_NET_PRODUCTION_SOURCES}"
)

if(MICROWORLD_NET_BUILD_TESTS)
    include(CTest)

    set(MICROWORLD_NET_TEST_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/../Core/tests/TestMain.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/NetAllocationCounters.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/ByteWriterTests.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/ByteReaderTests.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/FrameCodecTests.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/HostLoopbackTests.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/NetManagerTests.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/NetProtocolTests.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/NetHostTests.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/NetAllocationTests.cpp"
    )

    if(BUILD_TESTING)
        foreach(MICROWORLD_NET_TEST_SOURCE IN LISTS MICROWORLD_NET_TEST_SOURCES)
            if(NOT EXISTS "${MICROWORLD_NET_TEST_SOURCE}")
                message(
                    FATAL_ERROR
                    "MicroWorld Net required test source is missing: ${MICROWORLD_NET_TEST_SOURCE}"
                )
            endif()
        endforeach()

        add_executable(microworld_net_tests ${MICROWORLD_NET_TEST_SOURCES})
        target_include_directories(microworld_net_tests PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/../Core/tests"
        )
        target_link_libraries(microworld_net_tests PRIVATE MicroWorld::Net)
        microworld_net_enable_strict_portable_options(microworld_net_tests)
        microworld_net_disable_exceptions_and_rtti(
            "${MICROWORLD_NET_TEST_SOURCES}"
        )
        add_test(NAME microworld_net_tests COMMAND microworld_net_tests)
    endif()
endif()

set(MICROWORLD_NET_BENCHMARK_SOURCE
    "${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/NetBenchmark.cpp"
)
if(MICROWORLD_NET_BUILD_BENCHMARKS AND EXISTS "${MICROWORLD_NET_BENCHMARK_SOURCE}")
    add_executable(microworld_net_benchmark ${MICROWORLD_NET_BENCHMARK_SOURCE})
    target_link_libraries(microworld_net_benchmark PRIVATE MicroWorld::Net)
    microworld_net_enable_strict_portable_options(microworld_net_benchmark)
    microworld_net_disable_exceptions_and_rtti(
        "${MICROWORLD_NET_BENCHMARK_SOURCE}"
    )
endif()
