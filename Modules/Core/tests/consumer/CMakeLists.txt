cmake_minimum_required(VERSION 3.16)

option(
    MICROWORLD_STANDALONE_CONSUMER
    "Build the host consumer without PlatformIO/ESP-IDF"
    OFF
)
option(
    MICROWORLD_STANDALONE_MEMORY_CONSUMER
    "Build the host Core+Memory consumer without PlatformIO/ESP-IDF"
    OFF
)
option(
    MICROWORLD_STANDALONE_OBJECT_CONSUMER
    "Build the host Core+Memory+Object consumer without PlatformIO/ESP-IDF"
    OFF
)
option(
    MICROWORLD_STANDALONE_ENGINE_CONSUMER
    "Build the host Core+Memory+Object+Engine consumer without PlatformIO/ESP-IDF"
    OFF
)
option(
    MICROWORLD_STANDALONE_NET_CONSUMER
    "Build the host Core+Memory+Net consumer without PlatformIO/ESP-IDF"
    OFF
)

if(
    MICROWORLD_STANDALONE_CONSUMER
    OR MICROWORLD_STANDALONE_MEMORY_CONSUMER
    OR MICROWORLD_STANDALONE_OBJECT_CONSUMER
    OR MICROWORLD_STANDALONE_ENGINE_CONSUMER
    OR MICROWORLD_STANDALONE_NET_CONSUMER
)
    project(microworld_standalone_consumer LANGUAGES CXX)

    if(MSVC)
        # Replace CMake's default MSVC exception/RTTI flags at profile scope so
        # every linked portable package and consumer compiles under one
        # warning-clean contract without contradictory command-line switches.
        string(REPLACE "/EHsc" "/EHs-c-" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
        string(REPLACE "/GR" "/GR-" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
    endif()

    set(MICROWORLD_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(MICROWORLD_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
    set(MICROWORLD_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    add_subdirectory(
        "${CMAKE_CURRENT_LIST_DIR}/../.."
        "${CMAKE_CURRENT_BINARY_DIR}/microworld"
    )

    function(microworld_consumer_enable_strict_portable_flags Target)
        if(MSVC)
            target_compile_options(
                ${Target}
                PRIVATE /W4 /WX /Zc:__cplusplus
            )
        else()
            target_compile_options(
                ${Target}
                PRIVATE
                    -Wall
                    -Wextra
                    -Wpedantic
                    -Werror
                    -fno-exceptions
                    -fno-rtti
            )
        endif()
    endfunction()

    if(MICROWORLD_STANDALONE_CONSUMER)
        add_executable(
            microworld_core_consumer
            src/NativeMain.cpp
        )
        target_link_libraries(
            microworld_core_consumer
            PRIVATE MicroWorld::Core
        )
        microworld_consumer_enable_strict_portable_flags(
            microworld_core_consumer
        )
    endif()

    if(MICROWORLD_STANDALONE_MEMORY_CONSUMER OR MICROWORLD_STANDALONE_OBJECT_CONSUMER)
        set(
            MICROWORLD_MEMORY_BUILD_TESTS
            OFF
            CACHE BOOL "" FORCE
        )
        set(
            MICROWORLD_MEMORY_BUILD_BENCHMARKS
            OFF
            CACHE BOOL "" FORCE
        )
        add_subdirectory(
            "${CMAKE_CURRENT_LIST_DIR}/../../../Memory"
            "${CMAKE_CURRENT_BINARY_DIR}/Memory"
        )
        if(MICROWORLD_STANDALONE_MEMORY_CONSUMER)
            add_executable(
                microworld_memory_consumer
                src/MemoryNativeMain.cpp
            )
            target_link_libraries(
                microworld_memory_consumer
                PRIVATE MicroWorld::Memory
            )
            microworld_consumer_enable_strict_portable_flags(
                microworld_memory_consumer
            )
            microworld_enable_profile_map(
                microworld_memory_consumer
                "${CMAKE_CURRENT_BINARY_DIR}/microworld_memory_profile.map"
            )
        endif()
    endif()

    if(MICROWORLD_STANDALONE_OBJECT_CONSUMER)
        set(
            MICROWORLD_OBJECT_BUILD_TESTS
            OFF
            CACHE BOOL "" FORCE
        )
        set(
            MICROWORLD_OBJECT_BUILD_BENCHMARKS
            OFF
            CACHE BOOL "" FORCE
        )
        add_subdirectory(
            "${CMAKE_CURRENT_LIST_DIR}/../../../Object"
            "${CMAKE_CURRENT_BINARY_DIR}/Object"
        )
        add_executable(
            microworld_object_consumer
            src/ObjectNativeMain.cpp
        )
        target_link_libraries(
            microworld_object_consumer
            PRIVATE MicroWorld::Object
        )
        microworld_consumer_enable_strict_portable_flags(
            microworld_object_consumer
        )
        microworld_enable_profile_map(
            microworld_object_consumer
            "${CMAKE_CURRENT_BINARY_DIR}/microworld_object_profile.map"
        )
    endif()

    if(MICROWORLD_STANDALONE_ENGINE_CONSUMER)
        set(
            MICROWORLD_OBJECT_BUILD_TESTS
            OFF
            CACHE BOOL "" FORCE
        )
        set(
            MICROWORLD_OBJECT_BUILD_BENCHMARKS
            OFF
            CACHE BOOL "" FORCE
        )
        add_subdirectory(
            "${CMAKE_CURRENT_LIST_DIR}/../../../Object"
            "${CMAKE_CURRENT_BINARY_DIR}/Object"
        )
        set(
            MICROWORLD_ENGINE_BUILD_TESTS
            OFF
            CACHE BOOL "" FORCE
        )
        set(
            MICROWORLD_ENGINE_BUILD_BENCHMARKS
            OFF
            CACHE BOOL "" FORCE
        )
        add_subdirectory(
            "${CMAKE_CURRENT_LIST_DIR}/../../../Engine"
            "${CMAKE_CURRENT_BINARY_DIR}/Engine"
        )
        add_executable(
            microworld_engine_consumer
            src/EngineNativeMain.cpp
        )
        target_link_libraries(
            microworld_engine_consumer
            PRIVATE MicroWorld::Engine
        )
        microworld_consumer_enable_strict_portable_flags(
            microworld_engine_consumer
        )
        microworld_enable_profile_map(
            microworld_engine_consumer
            "${CMAKE_CURRENT_BINARY_DIR}/microworld_engine_profile.map"
        )
    endif()

    if(MICROWORLD_STANDALONE_NET_CONSUMER)
        set(
            MICROWORLD_MEMORY_BUILD_TESTS
            OFF
            CACHE BOOL "" FORCE
        )
        set(
            MICROWORLD_MEMORY_BUILD_BENCHMARKS
            OFF
            CACHE BOOL "" FORCE
        )
        add_subdirectory(
            "${CMAKE_CURRENT_LIST_DIR}/../../../Memory"
            "${CMAKE_CURRENT_BINARY_DIR}/Memory"
        )
        set(
            MICROWORLD_NET_BUILD_TESTS
            OFF
            CACHE BOOL "" FORCE
        )
        set(
            MICROWORLD_NET_BUILD_BENCHMARKS
            OFF
            CACHE BOOL "" FORCE
        )
        add_subdirectory(
            "${CMAKE_CURRENT_LIST_DIR}/../../../Net"
            "${CMAKE_CURRENT_BINARY_DIR}/Net"
        )
        add_executable(
            microworld_net_consumer
            src/NetNativeMain.cpp
        )
        target_link_libraries(
            microworld_net_consumer
            PRIVATE MicroWorld::Net
        )
        microworld_consumer_enable_strict_portable_flags(
            microworld_net_consumer
        )
        microworld_enable_profile_map(
            microworld_net_consumer
            "${CMAKE_CURRENT_BINARY_DIR}/microworld_net_profile.map"
        )
    endif()
else()
    include($ENV{IDF_PATH}/tools/cmake/project.cmake)
    project(microworld_consumer)
endif()
