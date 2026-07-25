cmake_minimum_required(VERSION 3.16)

project(MicroWorldObject VERSION 0.3.0 LANGUAGES CXX)

option(
    MICROWORLD_OBJECT_BUILD_BENCHMARKS
    "Build MicroWorld Object host benchmarks"
    ON
)
option(
    MICROWORLD_OBJECT_BUILD_TESTS
    "Build MicroWorld Object host tests"
    ON
)

# A standalone Object configuration composes its inward dependencies. A larger
# profile may instead provide these targets before adding this directory.
if(NOT TARGET MicroWorld::Core)
    set(MICROWORLD_BUILD_TESTS OFF)
    set(MICROWORLD_BUILD_BENCHMARKS OFF)
    set(MICROWORLD_BUILD_EXAMPLES OFF)
    add_subdirectory(
        "${CMAKE_CURRENT_SOURCE_DIR}/../Core"
        "${CMAKE_CURRENT_BINARY_DIR}/Core"
    )
endif()

set(MICROWORLD_OBJECT_PRODUCTION_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ObjectStore.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/GarbageCollector.cpp"
)

add_library(microworld_object STATIC ${MICROWORLD_OBJECT_PRODUCTION_SOURCES})
target_include_directories(microworld_object PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
)
target_compile_features(microworld_object PUBLIC cxx_std_17)
target_link_libraries(microworld_object PUBLIC MicroWorld::Core)
add_library(MicroWorld::Object ALIAS microworld_object)

# Portable managed-object code is validated under the same strict, no-runtime-
# type-information boundary as Memory, including its retained benchmark.
function(microworld_object_enable_strict_portable_options Target)
    if(MSVC)
        target_compile_options(${Target} PRIVATE /W4 /WX /Zc:__cplusplus)
    else()
        target_compile_options(${Target} PRIVATE
            -Wall -Wextra -Wpedantic -Werror -fno-exceptions -fno-rtti
        )
    endif()
endfunction()

function(microworld_object_disable_exceptions_and_rtti SourceFiles)
    if(MSVC)
        if(
            CMAKE_GENERATOR MATCHES "^Visual Studio"
            AND CMAKE_VERSION VERSION_GREATER_EQUAL "3.18"
        )
            # This replaces Visual Studio's default /EHsc setting without a
            # conflicting command-line override on portable Object sources.
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

microworld_object_enable_strict_portable_options(microworld_object)
microworld_object_disable_exceptions_and_rtti(
    "${MICROWORLD_OBJECT_PRODUCTION_SOURCES}"
)

if(MICROWORLD_OBJECT_BUILD_TESTS)
    include(CTest)

    set(MICROWORLD_OBJECT_TEST_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/../Core/tests/TestMain.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/ObjectStoreTests.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/GarbageCollectorTests.cpp"
    )

    if(BUILD_TESTING)
        set(MICROWORLD_OBJECT_ALL_TEST_SOURCES_PRESENT TRUE)
        foreach(MICROWORLD_OBJECT_TEST_SOURCE IN LISTS MICROWORLD_OBJECT_TEST_SOURCES)
            if(NOT EXISTS "${MICROWORLD_OBJECT_TEST_SOURCE}")
                set(MICROWORLD_OBJECT_ALL_TEST_SOURCES_PRESENT FALSE)
            endif()
        endforeach()

        if(MICROWORLD_OBJECT_ALL_TEST_SOURCES_PRESENT)
            add_executable(microworld_object_tests ${MICROWORLD_OBJECT_TEST_SOURCES})
            target_include_directories(microworld_object_tests PRIVATE
                "${CMAKE_CURRENT_SOURCE_DIR}/../Core/tests"
            )
            target_link_libraries(microworld_object_tests PRIVATE MicroWorld::Object)
            microworld_object_enable_strict_portable_options(microworld_object_tests)
            microworld_object_disable_exceptions_and_rtti(
                "${MICROWORLD_OBJECT_TEST_SOURCES}"
            )
            add_test(NAME microworld_object_tests COMMAND microworld_object_tests)
        endif()
    endif()
endif()

set(MICROWORLD_OBJECT_BENCHMARK_SOURCE
    "${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/GarbageCollectorBenchmark.cpp"
)
if(MICROWORLD_OBJECT_BUILD_BENCHMARKS AND EXISTS "${MICROWORLD_OBJECT_BENCHMARK_SOURCE}")
    add_executable(microworld_object_benchmark ${MICROWORLD_OBJECT_BENCHMARK_SOURCE})
    target_link_libraries(microworld_object_benchmark PRIVATE MicroWorld::Object)
    microworld_object_enable_strict_portable_options(
        microworld_object_benchmark
    )
    microworld_object_disable_exceptions_and_rtti(
        "${MICROWORLD_OBJECT_BENCHMARK_SOURCE}"
    )
endif()
