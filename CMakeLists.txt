cmake_minimum_required(VERSION 3.16)

# Root superbuild: adds every portable/host module so the whole engine builds and
# tests under one configuration. Each module's own CMakeLists keeps its project()
# and sibling-guard logic intact, so configuring a module directory standalone
# works exactly as before. The guards below mirror the same NOT TARGET checks the
# modules use internally, so a module that an earlier sibling already pulled in
# (e.g. Engine pulls Net for its tests, PlatformHost pulls Net and Engine) is not
# added twice. PlatformEsp32 is PlatformIO/ESP-IDF only and has no CMakeLists,
# so it is intentionally not added here.
project(MicroWorld VERSION 0.3.0 LANGUAGES CXX)

enable_testing()

if(NOT TARGET MicroWorld::Core)
    add_subdirectory(Modules/Core)
endif()
if(NOT TARGET MicroWorld::Object)
    add_subdirectory(Modules/Object)
endif()
# Net and Engine are added before Messaging so the root superbuild owns their
# test targets rather than Messaging's test support pulling them in with tests
# disabled.
if(NOT TARGET MicroWorld::Net)
    add_subdirectory(Modules/Net)
endif()
if(NOT TARGET MicroWorld::RadioE32)
    add_subdirectory(Modules/RadioE32)
endif()
# PlatformPico's production target requires an initialized Pico SDK and remains
# outside this host superbuild. Its SDK-free compatibility facade is still
# host-tested here while RadioE32 owns portable framing tests.
set(MICROWORLD_PLATFORM_PICO_BUILD_DRIVER OFF)
set(MICROWORLD_PLATFORM_PICO_BUILD_TESTS ON)
add_subdirectory(Modules/PlatformPico)
if(NOT TARGET MicroWorld::Engine)
    add_subdirectory(Modules/Engine)
endif()
if(NOT TARGET MicroWorld::Messaging)
    add_subdirectory(Modules/Messaging)
endif()
if(NOT TARGET MicroWorld::Application)
    add_subdirectory(Modules/Application)
endif()
if(NOT TARGET MicroWorld::Integration)
    add_subdirectory(Modules/Integration)
endif()
if(NOT TARGET MicroWorld::PlatformHost)
    add_subdirectory(Modules/PlatformHost)
endif()

# Keeps the shared ESP32/Pico regression-payload contract independently
# verifiable on the host without making an example a module dependency.
set(MICROWORLD_LORA_PAYLOAD_REGRESSION_TEST_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/Modules/Core/tests/TestMain.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/examples/17-TwoBoardLora/LoraPayloadRegressionTests.cpp"
)
add_executable(
    microworld_lora_payload_regression_tests
    ${MICROWORLD_LORA_PAYLOAD_REGRESSION_TEST_SOURCES}
)
target_include_directories(
    microworld_lora_payload_regression_tests
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/Modules/Core/tests"
        "${CMAKE_CURRENT_SOURCE_DIR}/examples/17-TwoBoardLora/src"
)
target_link_libraries(
    microworld_lora_payload_regression_tests
    PRIVATE MicroWorld::Net
)
target_compile_features(microworld_lora_payload_regression_tests PRIVATE cxx_std_17)
microworld_net_enable_strict_portable_options(microworld_lora_payload_regression_tests)
microworld_net_disable_exceptions_and_rtti(
    "${MICROWORLD_LORA_PAYLOAD_REGRESSION_TEST_SOURCES}"
)
add_test(
    NAME microworld_lora_payload_regression_tests
    COMMAND microworld_lora_payload_regression_tests
)

# Format gate: the repo clang-format policy is otherwise uncovered by any build
# or checker, so dry-run it over every tracked C++ source as a ctest step. The
# root owns it (not any single module) because the style file lives here and the
# sweep spans every module.
find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_test(
    NAME microworld_format_check
    COMMAND
        Python3::Interpreter
        ${CMAKE_CURRENT_SOURCE_DIR}/tools/CheckFormatting.py
        --root ${CMAKE_CURRENT_SOURCE_DIR}
)
