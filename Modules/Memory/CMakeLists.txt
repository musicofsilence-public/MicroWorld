cmake_minimum_required(VERSION 3.16)

project(MicroWorldMemory VERSION 0.3.0 LANGUAGES CXX)

option(
    MICROWORLD_MEMORY_BUILD_BENCHMARKS
    "Build MicroWorld Memory host benchmarks"
    ON
)
option(
    MICROWORLD_MEMORY_BUILD_TESTS
    "Build MicroWorld Memory host tests"
    ON
)

# A standalone Memory configuration composes the adjacent Core package while a
# larger profile may provide the target before adding this directory.
if(NOT TARGET MicroWorld::Core)
    set(MICROWORLD_BUILD_TESTS OFF)
    set(MICROWORLD_BUILD_BENCHMARKS OFF)
    set(MICROWORLD_BUILD_EXAMPLES OFF)
    add_subdirectory(
        "${CMAKE_CURRENT_SOURCE_DIR}/../Core"
        "${CMAKE_CURRENT_BINARY_DIR}/Core"
    )
endif()

add_library(microworld_memory STATIC
    src/MemoryResource.cpp
)
add_library(MicroWorld::Memory ALIAS microworld_memory)

target_include_directories(microworld_memory PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
)
target_compile_features(microworld_memory PUBLIC cxx_std_17)
target_link_libraries(microworld_memory PUBLIC MicroWorld::Core)

# Keeps the portable Memory boundary under the same warning gate as Core.
function(microworld_memory_enable_strict_warnings Target)
    if(MSVC)
        target_compile_options(${Target} PRIVATE /W4 /WX /Zc:__cplusplus)
    else()
        target_compile_options(${Target} PRIVATE
            -Wall -Wextra -Wpedantic -Werror
        )
    endif()
endfunction()

microworld_memory_enable_strict_warnings(microworld_memory)

if(MICROWORLD_MEMORY_BUILD_TESTS)
    include(CTest)

    if(BUILD_TESTING)
        set(MICROWORLD_MEMORY_TEST_SOURCES
            "${CMAKE_CURRENT_SOURCE_DIR}/../Core/tests/TestMain.cpp"
            tests/MemoryTests.cpp
            tests/DelegateTests.cpp
        )
        add_executable(
            microworld_memory_tests
            ${MICROWORLD_MEMORY_TEST_SOURCES}
        )
        target_include_directories(
            microworld_memory_tests
            PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../Core/tests"
        )
        target_link_libraries(
            microworld_memory_tests
            PRIVATE MicroWorld::Memory
        )
        microworld_memory_enable_strict_warnings(
            microworld_memory_tests
        )

        if(MSVC)
            # The tests deliberately use over-aligned fixtures to verify
            # resource contracts; their expected tail padding is not a defect.
            target_compile_options(microworld_memory_tests PRIVATE /wd4324)

            if(
                CMAKE_GENERATOR MATCHES "^Visual Studio"
                AND CMAKE_VERSION VERSION_GREATER_EQUAL "3.18"
            )
                set_property(
                    SOURCE ${MICROWORLD_MEMORY_TEST_SOURCES}
                    PROPERTY VS_SETTINGS
                        "ExceptionHandling=false"
                        "RuntimeTypeInfo=false"
                )
            else()
                target_compile_options(
                    microworld_memory_tests
                    PRIVATE /EHs-c- /GR-
                )
            endif()
        else()
            target_compile_options(
                microworld_memory_tests
                PRIVATE -fno-exceptions -fno-rtti
            )
        endif()

        add_test(
            NAME microworld_memory_tests
            COMMAND microworld_memory_tests
        )
    endif()
endif()

if(MICROWORLD_MEMORY_BUILD_BENCHMARKS)
    add_executable(microworld_memory_benchmark
        benchmarks/MemoryBenchmark.cpp
    )
    target_link_libraries(
        microworld_memory_benchmark
        PRIVATE MicroWorld::Memory
    )
    microworld_memory_enable_strict_warnings(
        microworld_memory_benchmark
    )

    # The standard shared-pointer attribution prototype intentionally uses its
    # normal exception contract. Portable Memory production code remains
    # exceptions-free and is verified separately by downstream consumers.
    if(MSVC)
        target_compile_options(
            microworld_memory_benchmark
            PRIVATE /EHsc
        )
    endif()
endif()
