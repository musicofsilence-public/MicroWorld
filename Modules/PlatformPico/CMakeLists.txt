cmake_minimum_required(VERSION 3.20)

project(MicroWorldPlatformPico VERSION 0.3.0 LANGUAGES CXX)

option(
    MICROWORLD_PLATFORM_PICO_BUILD_DRIVER
    "Build the native Pico SDK E32 driver"
    ON
)
option(
    MICROWORLD_PLATFORM_PICO_BUILD_TESTS
    "Build SDK-free PlatformPico state tests"
    OFF
)

if(NOT TARGET MicroWorld::Net)
    set(MICROWORLD_NET_BUILD_TESTS OFF)
    set(MICROWORLD_NET_BUILD_BENCHMARKS OFF)
    add_subdirectory(
        "${CMAKE_CURRENT_SOURCE_DIR}/../Net"
        "${CMAKE_CURRENT_BINARY_DIR}/Net"
    )
endif()

set(
    MICROWORLD_PLATFORM_PICO_STATE_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/E32LoraTransportState.cpp"
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

if(MICROWORLD_PLATFORM_PICO_BUILD_DRIVER)
    if(NOT TARGET hardware_gpio OR NOT TARGET hardware_uart)
        message(
            FATAL_ERROR
            "PlatformPico driver requires pico_sdk_init() before add_subdirectory"
        )
    endif()

    set(
        MICROWORLD_PLATFORM_PICO_DRIVER_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/src/PicoE32LoraDriver.cpp"
        ${MICROWORLD_PLATFORM_PICO_STATE_SOURCES}
    )
    add_library(
        microworld_platform_pico
        STATIC
        ${MICROWORLD_PLATFORM_PICO_DRIVER_SOURCES}
    )
    add_library(MicroWorld::PlatformPico ALIAS microworld_platform_pico)
    target_include_directories(
        microworld_platform_pico
        PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    )
    target_compile_features(microworld_platform_pico PUBLIC cxx_std_17)
    target_link_libraries(
        microworld_platform_pico
        PUBLIC MicroWorld::Net
        PRIVATE hardware_gpio hardware_uart
    )
    microworld_platform_pico_enable_strict_source_options(
        "${MICROWORLD_PLATFORM_PICO_DRIVER_SOURCES}"
    )
endif()

if(MICROWORLD_PLATFORM_PICO_BUILD_TESTS)
    include(CTest)
    if(BUILD_TESTING)
        set(
            MICROWORLD_PLATFORM_PICO_TEST_SOURCES
            "${CMAKE_CURRENT_SOURCE_DIR}/../Core/tests/TestMain.cpp"
            ${MICROWORLD_PLATFORM_PICO_STATE_SOURCES}
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/E32LoraTransportStateTests.cpp"
        )
        add_executable(
            microworld_platform_pico_state_tests
            ${MICROWORLD_PLATFORM_PICO_TEST_SOURCES}
        )
        target_include_directories(
            microworld_platform_pico_state_tests
            PRIVATE
                "${CMAKE_CURRENT_SOURCE_DIR}/include"
                "${CMAKE_CURRENT_SOURCE_DIR}/../Core/tests"
        )
        target_compile_features(
            microworld_platform_pico_state_tests
            PRIVATE cxx_std_17
        )
        target_link_libraries(
            microworld_platform_pico_state_tests
            PRIVATE MicroWorld::Net
        )
        microworld_platform_pico_enable_strict_source_options(
            "${MICROWORLD_PLATFORM_PICO_TEST_SOURCES}"
        )
        add_test(
            NAME microworld_platform_pico_state_tests
            COMMAND microworld_platform_pico_state_tests
        )
    endif()
endif()
