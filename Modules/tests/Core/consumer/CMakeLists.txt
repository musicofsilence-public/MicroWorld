cmake_minimum_required(VERSION 3.16)

option(
    MICROWORLD_STANDALONE_CONSUMER
    "Build the host consumer without PlatformIO/ESP-IDF"
    OFF
)
option(
    MICROWORLD_STANDALONE_MEMORY_CONSUMER
    "Build the host Core memory-API consumer without PlatformIO/ESP-IDF"
    OFF
)
option(
    MICROWORLD_STANDALONE_OBJECT_CONSUMER
    "Build the host Core+Object consumer without PlatformIO/ESP-IDF"
    OFF
)
option(
    MICROWORLD_STANDALONE_ENGINE_CONSUMER
    "Build the host Core+Object+Engine consumer without PlatformIO/ESP-IDF"
    OFF
)
option(
    MICROWORLD_STANDALONE_NET_CONSUMER
    "Build the host Core+Net consumer without PlatformIO/ESP-IDF"
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
    # The single Modules package defines every per-system target and alias
    # (Core, Engine, Messaging, Transport/Net, Networking, Application) in one
    # add, so each consumer below links its alias directly — no per-package
    # add_subdirectory remains. ../.. resolves from Modules/tests/Core/consumer
    # to Modules/, the consolidated package root.
    add_subdirectory(
        "${CMAKE_CURRENT_LIST_DIR}/../../.."
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

    if(MICROWORLD_STANDALONE_MEMORY_CONSUMER)
        add_executable(
            microworld_memory_consumer
            src/MemoryNativeMain.cpp
        )
        target_link_libraries(
            microworld_memory_consumer
            PRIVATE MicroWorld::Core
        )
        microworld_consumer_enable_strict_portable_flags(
            microworld_memory_consumer
        )
        microworld_enable_profile_map(
            microworld_memory_consumer
            "${CMAKE_CURRENT_BINARY_DIR}/microworld_memory_profile.map"
        )
    endif()

    if(MICROWORLD_STANDALONE_OBJECT_CONSUMER)
        add_executable(
            microworld_object_consumer
            src/ObjectNativeMain.cpp
        )
        target_link_libraries(
            microworld_object_consumer
            PRIVATE MicroWorld::Engine
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
        add_executable(
            microworld_transport_consumer
            src/NetNativeMain.cpp
        )
        target_link_libraries(
            microworld_transport_consumer
            PRIVATE MicroWorld::Transport
        )
        microworld_consumer_enable_strict_portable_flags(
            microworld_transport_consumer
        )
        microworld_enable_profile_map(
            microworld_transport_consumer
            "${CMAKE_CURRENT_BINARY_DIR}/microworld_transport_profile.map"
        )
    endif()
else()
    include($ENV{IDF_PATH}/tools/cmake/project.cmake)
    project(microworld_consumer)
endif()
