cmake_minimum_required(VERSION 3.16)

project(MicroWorld VERSION 0.3.0 LANGUAGES CXX)

option(MICROWORLD_BUILD_TESTS "Build MicroWorld host tests" ON)

# Core owns the engine's primitive layer: lifecycle, tick, application, and the
# explicit memory resources, bounded containers, and fixed delegates that every
# higher module reuses. The World/Actor/Component and Network sources were
# retired in favor of the managed Engine layer (see MICROWORLD_ROADMAP.md Phase
# 1); Memory was folded in here because it was one translation unit plus
# header-only templates with no boundary of its own to protect.
set(MICROWORLD_CORE_SOURCES
    src/Application.cpp
    src/Log.cpp
    src/MemoryResource.cpp
    src/TickFunction.cpp
)

# The physical target name preserves the released 0.1 CMake contract while the
# namespaced alias gives later modules an explicit inward dependency.
add_library(microworld STATIC ${MICROWORLD_CORE_SOURCES})
add_library(MicroWorld::Core ALIAS microworld)
target_include_directories(microworld PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
)
target_compile_features(microworld PUBLIC cxx_std_17)

# Keeps production, tests, probes, benchmarks, and examples under one warning gate.
function(microworld_enable_strict_warnings Target)
    if(MSVC)
        target_compile_options(${Target} PRIVATE /W4 /WX /Zc:__cplusplus)
    else()
        target_compile_options(${Target} PRIVATE
            -Wall -Wextra -Wpedantic -Werror
        )
    endif()
endfunction()

# Produces a deterministic link-map location for the Core profile gate.
function(microworld_enable_profile_map Target MapPath)
    if(MSVC)
        target_link_options(${Target} PRIVATE "/MAP:${MapPath}")
    else()
        target_link_options(${Target} PRIVATE "-Wl,-Map,${MapPath}")
    endif()
endfunction()

microworld_enable_strict_warnings(microworld)

if(MICROWORLD_BUILD_TESTS)
    include(CTest)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)

    add_executable(microworld_tests
        tests/TestMain.cpp
        tests/CoreAllocationCounters.cpp
        tests/LogTests.cpp
        tests/TickFunctionTests.cpp
        tests/ApplicationTests.cpp
        tests/ApplicationRunnerTests.cpp
        tests/MemoryTests.cpp
        tests/DelegateTests.cpp
        tests/TimerManagerTests.cpp
    )
    target_link_libraries(microworld_tests PRIVATE MicroWorld::Core)
    microworld_enable_strict_warnings(microworld_tests)
    if(MSVC)
        # The folded-in Memory and Delegate tests deliberately use over-aligned
        # fixtures to verify resource and delegate contracts; their expected tail
        # padding is not a defect.
        target_compile_options(microworld_tests PRIVATE /wd4324)
    endif()
    add_test(NAME microworld_tests COMMAND microworld_tests)

    add_executable(microworld_no_exceptions_rtti_probe
        tests/consumer/src/NativeMain.cpp
    )
    target_link_libraries(microworld_no_exceptions_rtti_probe
        PRIVATE MicroWorld::Core
    )
    microworld_enable_strict_warnings(
        microworld_no_exceptions_rtti_probe
    )
    set(MICROWORLD_CORE_PROFILE_MAP
        "${CMAKE_CURRENT_BINARY_DIR}/microworld_core_profile.map"
    )
    microworld_enable_profile_map(
        microworld_no_exceptions_rtti_probe
        "${MICROWORLD_CORE_PROFILE_MAP}"
    )
    if(MSVC)
        target_compile_options(microworld_no_exceptions_rtti_probe
            PRIVATE /GR-
        )
    else()
        target_compile_options(microworld_no_exceptions_rtti_probe
            PRIVATE -fno-exceptions -fno-rtti
        )
    endif()

    add_test(
        NAME microworld_dependency_boundaries_self_test
        COMMAND
            ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/../../tools/CheckDependencyBoundaries.py
            --self-test
    )
    add_test(
        NAME microworld_dependency_boundaries
        COMMAND
            ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/../../tools/CheckDependencyBoundaries.py
            --package Core=${CMAKE_CURRENT_SOURCE_DIR}
    )
    add_test(
        NAME microworld_profile_map_self_test
        COMMAND
            ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/../../tools/CheckProfileMap.py
            --self-test
    )
    add_test(
        NAME microworld_core_profile_map
        COMMAND
            ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/../../tools/CheckProfileMap.py
            --map ${MICROWORLD_CORE_PROFILE_MAP}
            --profile Core
    )
endif()

# The dispatch benchmark and HostLifecycle example were retired with the Core
# actor model. Dispatch is now covered by microworld-engine; the HostLifecycle
# example was rewritten on the managed types and moved to microworld-engine
# (see MICROWORLD_ROADMAP.md Phase 1).
