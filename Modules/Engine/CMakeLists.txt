cmake_minimum_required(VERSION 3.16)

project(MicroWorldEngine VERSION 0.2.0 LANGUAGES CXX)

option(
    MICROWORLD_ENGINE_BUILD_BENCHMARKS
    "Build MicroWorld Engine host benchmarks"
    OFF
)
option(
    MICROWORLD_ENGINE_BUILD_TESTS
    "Build MicroWorld Engine host tests"
    ON
)

# A standalone Engine configuration composes its inward dependencies. A larger
# profile may instead provide these targets before adding this directory.
if(NOT TARGET MicroWorld::Object)
    set(MICROWORLD_OBJECT_BUILD_TESTS OFF)
    set(MICROWORLD_OBJECT_BUILD_BENCHMARKS OFF)
    add_subdirectory(
        "${CMAKE_CURRENT_SOURCE_DIR}/../Object"
        "${CMAKE_CURRENT_BINARY_DIR}/Object"
    )
endif()

set(MICROWORLD_ENGINE_PRODUCTION_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/World.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Actor.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ActorComponent.cpp"
)

add_library(microworld_engine STATIC ${MICROWORLD_ENGINE_PRODUCTION_SOURCES})
target_include_directories(microworld_engine PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
)
target_compile_features(microworld_engine PUBLIC cxx_std_17)
target_link_libraries(microworld_engine PUBLIC MicroWorld::Object)
add_library(MicroWorld::Engine ALIAS microworld_engine)

# Portable managed-engine code is validated under the same strict, no-runtime-
# type-information boundary as Object.
function(microworld_engine_enable_strict_portable_options Target)
    if(MSVC)
        target_compile_options(${Target} PRIVATE /W4 /WX /Zc:__cplusplus)
    else()
        target_compile_options(${Target} PRIVATE
            -Wall -Wextra -Wpedantic -Werror -fno-exceptions -fno-rtti
        )
    endif()
endfunction()

function(microworld_engine_disable_exceptions_and_rtti SourceFiles)
    if(MSVC)
        if(
            CMAKE_GENERATOR MATCHES "^Visual Studio"
            AND CMAKE_VERSION VERSION_GREATER_EQUAL "3.18"
        )
            # This replaces Visual Studio's default /EHsc setting without a
            # conflicting command-line override on portable Engine sources.
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

microworld_engine_enable_strict_portable_options(microworld_engine)
microworld_engine_disable_exceptions_and_rtti(
    "${MICROWORLD_ENGINE_PRODUCTION_SOURCES}"
)

if(MICROWORLD_ENGINE_BUILD_TESTS)
    include(CTest)

    # The net-composition test links Net into the test executable only; production
    # microworld_engine never depends on Net (the dependency checker enforces this).
    if(NOT TARGET MicroWorld::Net)
        set(MICROWORLD_NET_BUILD_TESTS OFF)
        set(MICROWORLD_NET_BUILD_BENCHMARKS OFF)
        add_subdirectory(
            "${CMAKE_CURRENT_SOURCE_DIR}/../Net"
            "${CMAKE_CURRENT_BINARY_DIR}/Net"
        )
    endif()

    set(MICROWORLD_ENGINE_TEST_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/../Core/tests/TestMain.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/EngineAllocationCounters.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/EngineLifecycleTests.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/EngineRegistrationTests.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/EngineSpawnDestroyTests.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/EngineInlineTypesTests.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/EngineHostTests.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/EngineNetHostTests.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/EngineGarbageCollectionTests.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/EngineTimerManagerTests.cpp"
    )

    if(BUILD_TESTING)
        foreach(MICROWORLD_ENGINE_TEST_SOURCE IN LISTS MICROWORLD_ENGINE_TEST_SOURCES)
            if(NOT EXISTS "${MICROWORLD_ENGINE_TEST_SOURCE}")
                message(
                    FATAL_ERROR
                    "MicroWorld Engine required test source is missing: ${MICROWORLD_ENGINE_TEST_SOURCE}"
                )
            endif()
        endforeach()

        add_executable(microworld_engine_tests ${MICROWORLD_ENGINE_TEST_SOURCES})
        target_include_directories(microworld_engine_tests PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/../Core/tests"
        )
        target_link_libraries(microworld_engine_tests PRIVATE MicroWorld::Engine MicroWorld::Net)
        microworld_engine_enable_strict_portable_options(microworld_engine_tests)
        microworld_engine_disable_exceptions_and_rtti(
            "${MICROWORLD_ENGINE_TEST_SOURCES}"
        )
        add_test(NAME microworld_engine_tests COMMAND microworld_engine_tests)
    endif()
endif()

set(MICROWORLD_ENGINE_BENCHMARK_SOURCE
    "${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/EngineBenchmark.cpp"
)
if(MICROWORLD_ENGINE_BUILD_BENCHMARKS AND EXISTS "${MICROWORLD_ENGINE_BENCHMARK_SOURCE}")
    add_executable(microworld_engine_benchmark ${MICROWORLD_ENGINE_BENCHMARK_SOURCE})
    target_link_libraries(microworld_engine_benchmark PRIVATE MicroWorld::Engine)
    microworld_engine_enable_strict_portable_options(
        microworld_engine_benchmark
    )
    microworld_engine_disable_exceptions_and_rtti(
        "${MICROWORLD_ENGINE_BENCHMARK_SOURCE}"
    )
endif()

# The HostLifecycle example is the canonical managed "hello MicroWorld" demo,
# rewritten on UWorld/AActor/UActorComponent when the Core actor model was
# retired (see MICROWORLD_ROADMAP.md Phase 1).
set(MICROWORLD_ENGINE_EXAMPLE_SOURCE
    "${CMAKE_CURRENT_SOURCE_DIR}/examples/HostLifecycle/Main.cpp"
)
if(EXISTS "${MICROWORLD_ENGINE_EXAMPLE_SOURCE}")
    add_executable(microworld_engine_host_lifecycle ${MICROWORLD_ENGINE_EXAMPLE_SOURCE})
    target_link_libraries(microworld_engine_host_lifecycle PRIVATE MicroWorld::Engine)
    microworld_engine_enable_strict_portable_options(microworld_engine_host_lifecycle)
    microworld_engine_disable_exceptions_and_rtti(
        "${MICROWORLD_ENGINE_EXAMPLE_SOURCE}"
    )
endif()
