cmake_minimum_required(VERSION 3.16)

project(MicroWorldRadioE32 VERSION 0.3.0 LANGUAGES CXX)

option(
    MICROWORLD_RADIO_E32_BUILD_TESTS
    "Build MicroWorld RadioE32 host tests"
    ON
)

# A standalone RadioE32 configuration composes its portable dependencies while
# a larger profile may provide them before adding this directory.
if(NOT TARGET MicroWorld::Core)
    set(MICROWORLD_BUILD_TESTS OFF)
    add_subdirectory(
        "${CMAKE_CURRENT_SOURCE_DIR}/../Core"
        "${CMAKE_CURRENT_BINARY_DIR}/Core"
    )
endif()

if(NOT TARGET MicroWorld::Net)
    set(MICROWORLD_NET_BUILD_TESTS OFF)
    set(MICROWORLD_NET_BUILD_BENCHMARKS OFF)
    add_subdirectory(
        "${CMAKE_CURRENT_SOURCE_DIR}/../Net"
        "${CMAKE_CURRENT_BINARY_DIR}/Net"
    )
endif()

set(MICROWORLD_RADIO_E32_PRODUCTION_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/E32LoraTransportState.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/RadioE32Driver.cpp"
)

add_library(
    microworld_radio_e32
    STATIC
    ${MICROWORLD_RADIO_E32_PRODUCTION_SOURCES}
)
add_library(MicroWorld::RadioE32 ALIAS microworld_radio_e32)

target_include_directories(microworld_radio_e32 PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
)
target_compile_features(microworld_radio_e32 PUBLIC cxx_std_17)
target_link_libraries(microworld_radio_e32 PUBLIC
    MicroWorld::Core
    MicroWorld::Net
)

# Keeps portable radio framing under the repository's strict, no-exception,
# no-runtime-type-information boundary.
function(microworld_radio_e32_enable_strict_portable_options Target)
    if(MSVC)
        target_compile_options(${Target} PRIVATE /W4 /WX /Zc:__cplusplus)
    else()
        target_compile_options(${Target} PRIVATE
            -Wall -Wextra -Wpedantic -Werror -fno-exceptions -fno-rtti
        )
    endif()
endfunction()

function(microworld_radio_e32_disable_exceptions_and_rtti SourceFiles)
    if(MSVC)
        if(
            CMAKE_GENERATOR MATCHES "^Visual Studio"
            AND CMAKE_VERSION VERSION_GREATER_EQUAL "3.18"
        )
            # This replaces Visual Studio's default /EHsc setting without a
            # conflicting command-line override on portable RadioE32 sources.
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

microworld_radio_e32_enable_strict_portable_options(microworld_radio_e32)
microworld_radio_e32_disable_exceptions_and_rtti(
    "${MICROWORLD_RADIO_E32_PRODUCTION_SOURCES}"
)

if(MICROWORLD_RADIO_E32_BUILD_TESTS)
    include(CTest)

    set(MICROWORLD_RADIO_E32_TEST_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/../Core/tests/TestMain.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/RadioE32DriverTests.cpp"
    )

    if(BUILD_TESTING)
        foreach(MICROWORLD_RADIO_E32_TEST_SOURCE IN LISTS MICROWORLD_RADIO_E32_TEST_SOURCES)
            if(NOT EXISTS "${MICROWORLD_RADIO_E32_TEST_SOURCE}")
                message(
                    FATAL_ERROR
                    "MicroWorld RadioE32 required test source is missing: ${MICROWORLD_RADIO_E32_TEST_SOURCE}"
                )
            endif()
        endforeach()

        add_executable(
            microworld_radio_e32_tests
            ${MICROWORLD_RADIO_E32_TEST_SOURCES}
        )
        target_include_directories(microworld_radio_e32_tests PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/../Core/tests"
        )
        target_link_libraries(
            microworld_radio_e32_tests
            PRIVATE
            MicroWorld::RadioE32
        )
        microworld_radio_e32_enable_strict_portable_options(
            microworld_radio_e32_tests
        )
        microworld_radio_e32_disable_exceptions_and_rtti(
            "${MICROWORLD_RADIO_E32_TEST_SOURCES}"
        )
        add_test(
            NAME microworld_radio_e32_tests
            COMMAND microworld_radio_e32_tests
        )
    endif()
endif()
