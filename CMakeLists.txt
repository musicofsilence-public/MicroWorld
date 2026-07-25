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
# Net is added before Engine so the root superbuild owns Net (with tests ON)
# rather than Engine pulling it under Engine's binary dir with tests OFF.
if(NOT TARGET MicroWorld::Net)
    add_subdirectory(Modules/Net)
endif()
if(NOT TARGET MicroWorld::Engine)
    add_subdirectory(Modules/Engine)
endif()
if(NOT TARGET MicroWorld::PlatformHost)
    add_subdirectory(Modules/PlatformHost)
endif()

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
