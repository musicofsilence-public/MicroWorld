cmake_minimum_required(VERSION 3.16)

# Root superbuild: adds the single Modules package so the whole engine — all six
# contract-defined systems plus the non-portable Platform edges — builds and
# tests under one configuration. Modules/CMakeLists.txt owns every per-system
# target, its MicroWorld::* alias, its tests, the profile-map probe, the
# benchmarks, the example programs, and the five checker gates. Standalone
# per-module configuration is gone: the superbuild is now the only path.
project(MicroWorld VERSION 0.4.0 LANGUAGES CXX)

enable_testing()

add_subdirectory(Modules)

# Keeps the shared ESP32/Pico regression-payload contract independently
# verifiable on the host without making an example a module dependency.
set(MICROWORLD_LORA_PAYLOAD_REGRESSION_TEST_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/Modules/tests/Core/TestMain.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/examples/17-TwoBoardLora/LoraPayloadRegressionTests.cpp"
)
add_executable(
    microworld_lora_payload_regression_tests
    ${MICROWORLD_LORA_PAYLOAD_REGRESSION_TEST_SOURCES}
)
target_include_directories(
    microworld_lora_payload_regression_tests
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/Modules/tests/Core"
        "${CMAKE_CURRENT_SOURCE_DIR}/examples/17-TwoBoardLora/src"
)
target_link_libraries(
    microworld_lora_payload_regression_tests
    PRIVATE MicroWorld::Transport
)
target_compile_features(microworld_lora_payload_regression_tests PRIVATE cxx_std_17)
microworld_enable_strict_portable_options(microworld_lora_payload_regression_tests)
microworld_disable_exceptions_and_rtti(
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
