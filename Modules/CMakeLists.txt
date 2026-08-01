cmake_minimum_required(VERSION 3.16)

# One package, six contract-defined systems. The folder tree below MicroWorld/
# states the architecture directly: Core, Engine (Object folded in), Messaging,
# Transport (Net + RadioE32 folded in), Networking (was Integration), Application,
# and the non-portable Platform/{Host,Esp32,Pico} edges. Each system keeps its
# own STATIC/INTERFACE target and MicroWorld::* alias so the per-module profile
# evidence (CheckProfileMap.py) and the dependency-boundary gate still hold. The
# single include root is this directory: headers live at
# MicroWorld/<System>/<Name>.h and resolve as <MicroWorld/<System>/<Name>.h>.
project(MicroWorld VERSION 0.4.0 LANGUAGES CXX)

option(MICROWORLD_BUILD_TESTS "Build MicroWorld host tests" ON)
option(MICROWORLD_BUILD_BENCHMARKS "Build MicroWorld host benchmarks" OFF)

# Transport may be built without its radio (RadioE32) or IP/protocol (the UDP
# sources) subsets. RP2040 builds omit the IP sources to fit 264 KB RAM; a
# radio-less build omits the E32 framing. Both are link-time facts expressed as
# options, not folders, so the merged Transport system keeps one home.
option(MICROWORLD_TRANSPORT_LORA "Include the RadioE32 transport sources" ON)
option(MICROWORLD_TRANSPORT_IP "Include the IP/protocol transport sources" ON)

# The single include root for every system: <MicroWorld/...> resolves here.
set(MICROWORLD_INCLUDE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")

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

# Portable systems compile under the same strict, no-exception, no-runtime-type-
# information boundary. Every higher system reuses the two helpers below.
function(microworld_enable_strict_portable_options Target)
    if(MSVC)
        target_compile_options(${Target} PRIVATE /W4 /WX /Zc:__cplusplus)
    else()
        target_compile_options(${Target} PRIVATE
            -Wall -Wextra -Wpedantic -Werror -fno-exceptions -fno-rtti
        )
    endif()
endfunction()

function(microworld_disable_exceptions_and_rtti SourceFiles)
    if(MSVC)
        if(
            CMAKE_GENERATOR MATCHES "^Visual Studio"
            AND CMAKE_VERSION VERSION_GREATER_EQUAL "3.18"
        )
            # Replaces Visual Studio's default /EHsc setting without a conflicting
            # command-line override on portable sources.
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

# Produces a deterministic link-map location for the profile-map gate.
function(microworld_enable_profile_map Target MapPath)
    if(MSVC)
        target_link_options(${Target} PRIVATE "/MAP:${MapPath}")
    else()
        target_link_options(${Target} PRIVATE "-Wl,-Map,${MapPath}")
    endif()
endfunction()

# ---------------------------------------------------------------------------
# Core: lifecycle, tick, containers, delegates, smart pointers, timers, IPlaySystem.
# ---------------------------------------------------------------------------
set(MICROWORLD_CORE_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/MicroWorld/Core/Log.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/MicroWorld/Core/MemoryResource.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/MicroWorld/Core/TickFunction.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/MicroWorld/Core/TransportDevice.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/MicroWorld/Core/UartByteStream.cpp"
)

add_library(microworld STATIC ${MICROWORLD_CORE_SOURCES})
add_library(MicroWorld::Core ALIAS microworld)
target_include_directories(microworld PUBLIC
    "$<BUILD_INTERFACE:${MICROWORLD_INCLUDE_ROOT}>"
)
target_compile_features(microworld PUBLIC cxx_std_17)
microworld_enable_strict_warnings(microworld)

# ---------------------------------------------------------------------------
# Engine: UWorld / AActor / UActorComponent + the folded Object store and GC.
# ---------------------------------------------------------------------------
set(MICROWORLD_ENGINE_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/MicroWorld/Engine/Actor.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/MicroWorld/Engine/ActorComponent.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/MicroWorld/Engine/World.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/MicroWorld/Engine/ObjectStore.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/MicroWorld/Engine/GarbageCollector.cpp"
)

add_library(microworld_engine STATIC ${MICROWORLD_ENGINE_SOURCES})
add_library(MicroWorld::Engine ALIAS microworld_engine)
target_include_directories(microworld_engine PUBLIC
    "$<BUILD_INTERFACE:${MICROWORLD_INCLUDE_ROOT}>"
)
target_compile_features(microworld_engine PUBLIC cxx_std_17)
target_link_libraries(microworld_engine PUBLIC MicroWorld::Core)
microworld_enable_strict_portable_options(microworld_engine)
microworld_disable_exceptions_and_rtti("${MICROWORLD_ENGINE_SOURCES}")

# ---------------------------------------------------------------------------
# Messaging: header-only router, channel bindings, reliable channel.
# ---------------------------------------------------------------------------
add_library(microworld_messaging INTERFACE)
target_include_directories(microworld_messaging INTERFACE
    "$<BUILD_INTERFACE:${MICROWORLD_INCLUDE_ROOT}>"
)
target_compile_features(microworld_messaging INTERFACE cxx_std_17)
target_link_libraries(microworld_messaging INTERFACE MicroWorld::Core)
add_library(MicroWorld::Messaging ALIAS microworld_messaging)

# ---------------------------------------------------------------------------
# Transport: byte I/O, frame codec, TTransportHost (the former Net package) plus the
# optional portable RadioE32 framing. The IP and radio subsets are toggled by
# MICROWORLD_TRANSPORT_IP / MICROWORLD_TRANSPORT_LORA so a Pico build can omit
# the IP/protocol sources and a radio-less build can omit the E32 sources.
# ---------------------------------------------------------------------------
set(MICROWORLD_TRANSPORT_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/MicroWorld/Transport/Device.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/MicroWorld/Transport/PacketDropDevice.cpp"
)
if(MICROWORLD_TRANSPORT_LORA)
    list(APPEND MICROWORLD_TRANSPORT_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/MicroWorld/Transport/Lora/E32LoraTransportState.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/MicroWorld/Transport/Lora/E32LoraDevice.cpp"
    )
endif()

add_library(microworld_transport STATIC ${MICROWORLD_TRANSPORT_SOURCES})
add_library(MicroWorld::Transport ALIAS microworld_transport)
target_include_directories(microworld_transport PUBLIC
    "$<BUILD_INTERFACE:${MICROWORLD_INCLUDE_ROOT}>"
)
target_compile_features(microworld_transport PUBLIC cxx_std_17)
target_link_libraries(microworld_transport PUBLIC MicroWorld::Core)
microworld_enable_strict_portable_options(microworld_transport)
microworld_disable_exceptions_and_rtti("${MICROWORLD_TRANSPORT_SOURCES}")

# ---------------------------------------------------------------------------
# Networking (was Integration): TNetworking — Messaging + Transport behind
# IPlaySystem. Header-only; Engine is deliberately absent.
# ---------------------------------------------------------------------------
add_library(microworld_networking INTERFACE)
add_library(MicroWorld::Networking ALIAS microworld_networking)
target_include_directories(microworld_networking INTERFACE
    "$<BUILD_INTERFACE:${MICROWORLD_INCLUDE_ROOT}>"
)
target_compile_features(microworld_networking INTERFACE cxx_std_17)
target_link_libraries(microworld_networking INTERFACE
    MicroWorld::Messaging
    MicroWorld::Transport
)

# ---------------------------------------------------------------------------
# Application: FApplication and the Run frame-loop template.
# ---------------------------------------------------------------------------
set(MICROWORLD_APPLICATION_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/MicroWorld/Application/Application.cpp"
)

add_library(microworld_application STATIC ${MICROWORLD_APPLICATION_SOURCES})
add_library(MicroWorld::Application ALIAS microworld_application)
target_include_directories(microworld_application PUBLIC
    "$<BUILD_INTERFACE:${MICROWORLD_INCLUDE_ROOT}>"
)
target_compile_features(microworld_application PUBLIC cxx_std_17)
target_link_libraries(microworld_application PUBLIC MicroWorld::Engine)
microworld_enable_strict_portable_options(microworld_application)
microworld_disable_exceptions_and_rtti("${MICROWORLD_APPLICATION_SOURCES}")

# ---------------------------------------------------------------------------
# PlatformHost: non-portable host UDP over OS sockets. Outside the dependency
# gate (it reaches OS headers); only the host superbuild builds it.
# ---------------------------------------------------------------------------
set(MICROWORLD_PLATFORM_HOST_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/MicroWorld/Platform/Host/HostWifiDevice.cpp"
)

add_library(microworld_platform_host STATIC ${MICROWORLD_PLATFORM_HOST_SOURCES})
add_library(MicroWorld::PlatformHost ALIAS microworld_platform_host)
target_include_directories(microworld_platform_host PUBLIC
    "$<BUILD_INTERFACE:${MICROWORLD_INCLUDE_ROOT}>"
)
target_compile_features(microworld_platform_host PUBLIC cxx_std_17)
target_link_libraries(microworld_platform_host PUBLIC MicroWorld::Transport)
if(WIN32)
    target_link_libraries(microworld_platform_host PUBLIC ws2_32)
endif()
microworld_enable_strict_portable_options(microworld_platform_host)
microworld_disable_exceptions_and_rtti("${MICROWORLD_PLATFORM_HOST_SOURCES}")

# ---------------------------------------------------------------------------
# PlatformPico: the SDK-free compatibility facade is host-tested here; the real
# device target requires an initialized Pico SDK and stays outside this build.
# ---------------------------------------------------------------------------
set(MICROWORLD_PLATFORM_PICO_BUILD_DEVICE OFF)
option(
    MICROWORLD_PLATFORM_PICO_BUILD_TESTS
    "Build SDK-free PlatformPico compatibility facade tests"
    ON
)

function(microworld_platform_pico_enable_strict_source_options SourceFiles)
    if(MSVC)
        set_source_files_properties(
            ${SourceFiles}
            PROPERTIES COMPILE_OPTIONS "/W4;/WX;/Zc:__cplusplus"
        )
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
            set_property(
                SOURCE ${SourceFiles}
                APPEND
                PROPERTY COMPILE_OPTIONS "/EHs-c-;/GR-"
            )
        endif()
    else()
        set_source_files_properties(
            ${SourceFiles}
            PROPERTIES
                COMPILE_OPTIONS
                    "-Wall;-Wextra;-Wpedantic;-Werror;-fno-exceptions;-fno-rtti"
        )
    endif()
endfunction()

# A header-only facade alias so consumers can name MicroWorld::PlatformPico even
# in the host build where the real device target is unavailable.
add_library(microworld_platform_pico INTERFACE)
add_library(MicroWorld::PlatformPico ALIAS microworld_platform_pico)
target_include_directories(microworld_platform_pico INTERFACE
    "$<BUILD_INTERFACE:${MICROWORLD_INCLUDE_ROOT}>"
)
target_compile_features(microworld_platform_pico INTERFACE cxx_std_17)
target_link_libraries(microworld_platform_pico INTERFACE MicroWorld::Transport)

# ---------------------------------------------------------------------------
# Tests, profile-map probe, benchmarks, examples, and the five checker gates.
# ---------------------------------------------------------------------------
if(MICROWORLD_BUILD_TESTS)
    include(CTest)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)

    set(MICROWORLD_TOOLS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../tools")
    set(MICROWORLD_TESTS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/tests")

    # --- Core tests ---
    add_executable(microworld_tests
        "${MICROWORLD_TESTS_ROOT}/Core/TestMain.cpp"
        "${MICROWORLD_TESTS_ROOT}/Core/CoreAllocationCounters.cpp"
        "${MICROWORLD_TESTS_ROOT}/Core/LogTests.cpp"
        "${MICROWORLD_TESTS_ROOT}/Core/TickFunctionTests.cpp"
        "${MICROWORLD_TESTS_ROOT}/Core/MemoryTests.cpp"
        "${MICROWORLD_TESTS_ROOT}/Core/DelegateTests.cpp"
        "${MICROWORLD_TESTS_ROOT}/Core/TimerManagerTests.cpp"
        "${MICROWORLD_TESTS_ROOT}/Core/DeviceAddressTests.cpp"
    )
    target_link_libraries(microworld_tests PRIVATE MicroWorld::Core)
    microworld_enable_strict_warnings(microworld_tests)
    if(MSVC)
        # The Memory and Delegate tests deliberately use over-aligned fixtures;
        # their expected tail padding is not a defect.
        target_compile_options(microworld_tests PRIVATE /wd4324)
    endif()
    add_test(NAME microworld_tests COMMAND microworld_tests)

    # --- Core no-exceptions/no-rtti profile-map probe ---
    add_executable(microworld_no_exceptions_rtti_probe
        "${MICROWORLD_TESTS_ROOT}/Core/consumer/src/NativeMain.cpp"
    )
    target_link_libraries(microworld_no_exceptions_rtti_probe
        PRIVATE MicroWorld::Core
    )
    microworld_enable_strict_warnings(microworld_no_exceptions_rtti_probe)
    set(MICROWORLD_CORE_PROFILE_MAP
        "${CMAKE_CURRENT_BINARY_DIR}/microworld_core_profile.map"
    )
    microworld_enable_profile_map(
        microworld_no_exceptions_rtti_probe
        "${MICROWORLD_CORE_PROFILE_MAP}"
    )
    if(MSVC)
        target_compile_options(microworld_no_exceptions_rtti_probe PRIVATE /GR-)
    else()
        target_compile_options(microworld_no_exceptions_rtti_probe
            PRIVATE -fno-exceptions -fno-rtti
        )
    endif()

    # --- Engine tests (was Object + Engine) ---
    set(MICROWORLD_ENGINE_TEST_SOURCES
        "${MICROWORLD_TESTS_ROOT}/Core/TestMain.cpp"
        "${MICROWORLD_TESTS_ROOT}/Engine/EngineAllocationCounters.cpp"
        "${MICROWORLD_TESTS_ROOT}/Engine/EngineLifecycleTests.cpp"
        "${MICROWORLD_TESTS_ROOT}/Engine/EngineRegistrationTests.cpp"
        "${MICROWORLD_TESTS_ROOT}/Engine/EngineSpawnDestroyTests.cpp"
        "${MICROWORLD_TESTS_ROOT}/Engine/EngineDeferredSpawnTests.cpp"
        "${MICROWORLD_TESTS_ROOT}/Engine/EngineHostTests.cpp"
        "${MICROWORLD_TESTS_ROOT}/Engine/EngineGarbageCollectionTests.cpp"
        "${MICROWORLD_TESTS_ROOT}/Engine/PlaySystemSetTests.cpp"
        "${MICROWORLD_TESTS_ROOT}/Engine/GarbageCollectorTests.cpp"
        "${MICROWORLD_TESTS_ROOT}/Engine/ObjectStoreTests.cpp"
    )
    add_executable(microworld_engine_tests ${MICROWORLD_ENGINE_TEST_SOURCES})
    target_include_directories(microworld_engine_tests PRIVATE
        "${MICROWORLD_TESTS_ROOT}/Core"
    )
    target_link_libraries(microworld_engine_tests PRIVATE MicroWorld::Engine)
    microworld_enable_strict_portable_options(microworld_engine_tests)
    microworld_disable_exceptions_and_rtti("${MICROWORLD_ENGINE_TEST_SOURCES}")
    add_test(NAME microworld_engine_tests COMMAND microworld_engine_tests)

    # --- Messaging tests ---
    set(MICROWORLD_MESSAGING_TEST_SOURCES
        "${MICROWORLD_TESTS_ROOT}/Core/TestMain.cpp"
        "${MICROWORLD_TESTS_ROOT}/Engine/EngineAllocationCounters.cpp"
        "${MICROWORLD_TESTS_ROOT}/Messaging/NameIdTests.cpp"
        "${MICROWORLD_TESTS_ROOT}/Messaging/MessageTypesTests.cpp"
        "${MICROWORLD_TESTS_ROOT}/Messaging/MessagingSystemTests.cpp"
        "${MICROWORLD_TESTS_ROOT}/Messaging/MessagingWireTests.cpp"
        "${MICROWORLD_TESTS_ROOT}/Messaging/MessageCodecTests.cpp"
        "${MICROWORLD_TESTS_ROOT}/Messaging/MessageRouterTests.cpp"
        "${MICROWORLD_TESTS_ROOT}/Messaging/MessageChannelTests.cpp"
        "${MICROWORLD_TESTS_ROOT}/Messaging/ReliableChannelTests.cpp"
    )
    add_executable(microworld_messaging_tests ${MICROWORLD_MESSAGING_TEST_SOURCES})
    target_include_directories(microworld_messaging_tests PRIVATE
        "${MICROWORLD_TESTS_ROOT}/Core"
        "${MICROWORLD_TESTS_ROOT}/Engine"
    )
    target_link_libraries(microworld_messaging_tests PRIVATE
        MicroWorld::Messaging
        MicroWorld::Engine
        MicroWorld::Transport
    )
    microworld_enable_strict_portable_options(microworld_messaging_tests)
    microworld_disable_exceptions_and_rtti("${MICROWORLD_MESSAGING_TEST_SOURCES}")
    add_test(NAME microworld_messaging_tests COMMAND microworld_messaging_tests)

    # --- Transport tests (was Net + RadioE32) ---
    set(microworld_transport_TEST_SOURCES
        "${MICROWORLD_TESTS_ROOT}/Core/TestMain.cpp"
        "${MICROWORLD_TESTS_ROOT}/Transport/TransportAllocationCounters.cpp"
        "${MICROWORLD_TESTS_ROOT}/Transport/ByteWriterTests.cpp"
        "${MICROWORLD_TESTS_ROOT}/Transport/ByteReaderTests.cpp"
        "${MICROWORLD_TESTS_ROOT}/Transport/E32LoraTests.cpp"
        "${MICROWORLD_TESTS_ROOT}/Transport/FrameCodecTests.cpp"
        "${MICROWORLD_TESTS_ROOT}/Transport/HostLoopbackTests.cpp"
        "${MICROWORLD_TESTS_ROOT}/Transport/LoopbackNetworkTests.cpp"
        "${MICROWORLD_TESTS_ROOT}/Transport/PacketDropDeviceTests.cpp"
        "${MICROWORLD_TESTS_ROOT}/Transport/TransportManagerTests.cpp"
        "${MICROWORLD_TESTS_ROOT}/Transport/TransportProtocolTests.cpp"
        "${MICROWORLD_TESTS_ROOT}/Transport/TransportHostTests.cpp"
        "${MICROWORLD_TESTS_ROOT}/Transport/TransportAllocationTests.cpp"
        "${MICROWORLD_TESTS_ROOT}/Transport/E32LoraDeviceTests.cpp"
    )
    add_executable(microworld_transport_tests ${microworld_transport_TEST_SOURCES})
    target_include_directories(microworld_transport_tests PRIVATE
        "${MICROWORLD_TESTS_ROOT}/Core"
    )
    target_link_libraries(microworld_transport_tests PRIVATE MicroWorld::Transport)
    microworld_enable_strict_portable_options(microworld_transport_tests)
    microworld_disable_exceptions_and_rtti("${microworld_transport_TEST_SOURCES}")
    add_test(NAME microworld_transport_tests COMMAND microworld_transport_tests)

    # --- Networking tests (was Integration) ---
    set(microworld_networking_TEST_SOURCES
        "${MICROWORLD_TESTS_ROOT}/Core/TestMain.cpp"
        "${MICROWORLD_TESTS_ROOT}/Networking/EngineHostTests.cpp"
        "${MICROWORLD_TESTS_ROOT}/Networking/NetworkingTests.cpp"
    )
    add_executable(microworld_networking_tests ${microworld_networking_TEST_SOURCES})
    target_include_directories(microworld_networking_tests PRIVATE
        "${MICROWORLD_TESTS_ROOT}/Core"
    )
    target_link_libraries(microworld_networking_tests PRIVATE
        MicroWorld::Networking
        MicroWorld::Engine
    )
    microworld_enable_strict_portable_options(microworld_networking_tests)
    microworld_disable_exceptions_and_rtti("${microworld_networking_TEST_SOURCES}")
    add_test(NAME microworld_networking_tests COMMAND microworld_networking_tests)

    # --- Application tests ---
    set(MICROWORLD_APPLICATION_TEST_SOURCES
        "${MICROWORLD_TESTS_ROOT}/Core/TestMain.cpp"
        "${MICROWORLD_TESTS_ROOT}/Application/ApplicationTests.cpp"
        "${MICROWORLD_TESTS_ROOT}/Application/ApplicationRunTests.cpp"
    )
    add_executable(microworld_application_tests ${MICROWORLD_APPLICATION_TEST_SOURCES})
    target_include_directories(microworld_application_tests PRIVATE
        "${MICROWORLD_TESTS_ROOT}/Core"
    )
    target_link_libraries(microworld_application_tests PRIVATE MicroWorld::Application)
    microworld_enable_strict_portable_options(microworld_application_tests)
    microworld_disable_exceptions_and_rtti("${MICROWORLD_APPLICATION_TEST_SOURCES}")
    add_test(NAME microworld_application_tests COMMAND microworld_application_tests)

    # --- PlatformHost tests ---
    set(MICROWORLD_PLATFORM_HOST_TEST_SOURCES
        "${MICROWORLD_TESTS_ROOT}/Core/TestMain.cpp"
        "${MICROWORLD_TESTS_ROOT}/Platform/Host/HostTimeSourceTests.cpp"
        "${MICROWORLD_TESTS_ROOT}/Platform/Host/HostWifiDeviceTests.cpp"
        "${MICROWORLD_TESTS_ROOT}/Platform/Host/HostTransportEndToEndTests.cpp"
    )
    add_executable(microworld_platform_host_tests ${MICROWORLD_PLATFORM_HOST_TEST_SOURCES})
    target_include_directories(microworld_platform_host_tests PRIVATE
        "${MICROWORLD_TESTS_ROOT}/Core"
    )
    target_link_libraries(microworld_platform_host_tests PRIVATE MicroWorld::PlatformHost)
    microworld_enable_strict_portable_options(microworld_platform_host_tests)
    microworld_disable_exceptions_and_rtti("${MICROWORLD_PLATFORM_HOST_TEST_SOURCES}")
    add_test(NAME microworld_platform_host_tests COMMAND microworld_platform_host_tests)

    # --- PlatformPico SDK-free compatibility tests ---
    if(MICROWORLD_PLATFORM_PICO_BUILD_TESTS)
        set(MICROWORLD_PLATFORM_PICO_TEST_SOURCES
            "${MICROWORLD_TESTS_ROOT}/Core/TestMain.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/MicroWorld/Platform/Pico/PicoLoraDevice.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/MicroWorld/Platform/Pico/PicoUartByteStream.cpp"
            "${MICROWORLD_TESTS_ROOT}/Platform/Pico/PicoLoraDeviceTests.cpp"
        )
        add_executable(
            microworld_platform_pico_compatibility_tests
            ${MICROWORLD_PLATFORM_PICO_TEST_SOURCES}
        )
        target_include_directories(
            microworld_platform_pico_compatibility_tests
            PRIVATE
                "${MICROWORLD_INCLUDE_ROOT}"
                "${MICROWORLD_TESTS_ROOT}/Core"
        )
        target_compile_features(
            microworld_platform_pico_compatibility_tests
            PRIVATE cxx_std_17
        )
        # RadioE32 folded into the Transport target (MicroWorld::Transport alias), so
        # the facade's embedded FE32LoraDevice links through it rather than a
        # separate RadioE32 archive.
        target_link_libraries(
            microworld_platform_pico_compatibility_tests
            PRIVATE MicroWorld::Transport
        )
        microworld_platform_pico_enable_strict_source_options(
            "${MICROWORLD_PLATFORM_PICO_TEST_SOURCES}"
        )
        add_test(
            NAME microworld_platform_pico_compatibility_tests
            COMMAND microworld_platform_pico_compatibility_tests
        )
    endif()

    # --- Benchmarks ---
    if(MICROWORLD_BUILD_BENCHMARKS)
        set(MICROWORLD_ENGINE_BENCHMARK_SOURCE
            "${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/Engine/GarbageCollectorBenchmark.cpp"
        )
        if(EXISTS "${MICROWORLD_ENGINE_BENCHMARK_SOURCE}")
            add_executable(microworld_engine_benchmark ${MICROWORLD_ENGINE_BENCHMARK_SOURCE})
            target_link_libraries(microworld_engine_benchmark PRIVATE MicroWorld::Engine)
            microworld_enable_strict_portable_options(microworld_engine_benchmark)
            microworld_disable_exceptions_and_rtti("${MICROWORLD_ENGINE_BENCHMARK_SOURCE}")
        endif()
    endif()

    # --- Example programs ---
    set(MICROWORLD_ENGINE_EXAMPLE_SOURCE
        "${CMAKE_CURRENT_SOURCE_DIR}/../examples/HostLifecycle/Main.cpp"
    )
    if(EXISTS "${MICROWORLD_ENGINE_EXAMPLE_SOURCE}")
        add_executable(microworld_engine_host_lifecycle ${MICROWORLD_ENGINE_EXAMPLE_SOURCE})
        target_link_libraries(microworld_engine_host_lifecycle PRIVATE MicroWorld::Engine)
        microworld_enable_strict_portable_options(microworld_engine_host_lifecycle)
        microworld_disable_exceptions_and_rtti("${MICROWORLD_ENGINE_EXAMPLE_SOURCE}")
    endif()

    set(MICROWORLD_PLATFORM_HOST_EXAMPLE_SOURCE
        "${CMAKE_CURRENT_SOURCE_DIR}/../examples/TwoNodeDemo/Main.cpp"
    )
    if(EXISTS "${MICROWORLD_PLATFORM_HOST_EXAMPLE_SOURCE}")
        add_executable(microworld_platform_host_two_node_demo ${MICROWORLD_PLATFORM_HOST_EXAMPLE_SOURCE})
        target_link_libraries(microworld_platform_host_two_node_demo PRIVATE
            MicroWorld::PlatformHost
            MicroWorld::Engine
        )
        microworld_enable_strict_portable_options(microworld_platform_host_two_node_demo)
        microworld_disable_exceptions_and_rtti("${MICROWORLD_PLATFORM_HOST_EXAMPLE_SOURCE}")
    endif()

    # --- The five checker gates (run alongside their self-tests under ctest) ---
    add_test(
        NAME microworld_dependency_boundaries_self_test
        COMMAND ${Python3_EXECUTABLE} ${MICROWORLD_TOOLS_ROOT}/CheckDependencyBoundaries.py --self-test
    )
    add_test(
        NAME microworld_dependency_boundaries
        COMMAND ${Python3_EXECUTABLE} ${MICROWORLD_TOOLS_ROOT}/CheckDependencyBoundaries.py
            --package Core=${CMAKE_CURRENT_SOURCE_DIR}/MicroWorld/Core
            --package Engine=${CMAKE_CURRENT_SOURCE_DIR}/MicroWorld/Engine
            --package Messaging=${CMAKE_CURRENT_SOURCE_DIR}/MicroWorld/Messaging
            --package Transport=${CMAKE_CURRENT_SOURCE_DIR}/MicroWorld/Transport
            --package Networking=${CMAKE_CURRENT_SOURCE_DIR}/MicroWorld/Networking
            --package Application=${CMAKE_CURRENT_SOURCE_DIR}/MicroWorld/Application
    )
    add_test(
        NAME microworld_profile_map_self_test
        COMMAND ${Python3_EXECUTABLE} ${MICROWORLD_TOOLS_ROOT}/CheckProfileMap.py --self-test
    )
    add_test(
        NAME microworld_core_profile_map
        COMMAND ${Python3_EXECUTABLE} ${MICROWORLD_TOOLS_ROOT}/CheckProfileMap.py
            --map ${MICROWORLD_CORE_PROFILE_MAP}
            --profile Core
    )
    add_test(
        NAME microworld_folder_guides_self_test
        COMMAND ${Python3_EXECUTABLE} ${MICROWORLD_TOOLS_ROOT}/CheckFolderAgents.py --self-test
    )
    add_test(
        NAME microworld_folder_guides
        COMMAND ${Python3_EXECUTABLE} ${MICROWORLD_TOOLS_ROOT}/CheckFolderAgents.py
            --root ${CMAKE_CURRENT_SOURCE_DIR}
    )
    add_test(
        NAME microworld_documentation_style_self_test
        COMMAND ${Python3_EXECUTABLE} ${MICROWORLD_TOOLS_ROOT}/CheckDocumentationStyle.py --self-test
    )
    # The repository root, not Modules: examples are maintained source too, and
    # the checker's default exclusions keep every generated tree out.
    add_test(
        NAME microworld_documentation_style
        COMMAND ${Python3_EXECUTABLE} ${MICROWORLD_TOOLS_ROOT}/CheckDocumentationStyle.py
            --root ${CMAKE_CURRENT_SOURCE_DIR}/..
    )
endif()

# Expose the strict-options helpers and the profile-map helper to standalone
# consumers (the pico-freertos consumer and the host consumer harness) that add
# this directory into their own build.
set(MICROWORLD_ENABLE_STRICT_WARNINGS_FUNC microworld_enable_strict_warnings CACHE INTERNAL "")
set(MICROWORLD_ENABLE_PROFILE_MAP_FUNC microworld_enable_profile_map CACHE INTERNAL "")
