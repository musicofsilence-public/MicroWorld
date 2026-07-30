cmake_minimum_required(VERSION 3.20)

project(microworld_core_tick_example_tests LANGUAGES CXX)

if(MSVC)
	string(REPLACE "/EHsc" "/EHs-c-" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
	string(REPLACE "/GR" "/GR-" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
endif()

set(MICROWORLD_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(MICROWORLD_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
set(MICROWORLD_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory(
	"${CMAKE_CURRENT_LIST_DIR}/../../../Modules"
	"${CMAKE_CURRENT_BINARY_DIR}/microworld"
)

add_executable(
	microworld_core_tick_example_tests
	"${CMAKE_CURRENT_LIST_DIR}/../../../Modules/tests/Core/TestMain.cpp"
	"${CMAKE_CURRENT_LIST_DIR}/../src/CoreTickExample.cpp"
	CoreTickExampleTests.cpp
)
target_include_directories(
	microworld_core_tick_example_tests
	PRIVATE
		"${CMAKE_CURRENT_LIST_DIR}/../../../Modules/tests/Core"
		"${CMAKE_CURRENT_LIST_DIR}/../src"
)
target_link_libraries(
	microworld_core_tick_example_tests
	PRIVATE MicroWorld::Core
)
target_compile_features(microworld_core_tick_example_tests PRIVATE cxx_std_17)
if(MSVC)
	target_compile_options(
		microworld_core_tick_example_tests
		PRIVATE /W4 /WX /Zc:__cplusplus
	)
else()
	target_compile_options(
		microworld_core_tick_example_tests
		PRIVATE
			-Wall
			-Wextra
			-Wpedantic
			-Werror
			-fno-exceptions
			-fno-rtti
	)
endif()

enable_testing()
add_test(NAME microworld_core_tick_example_tests COMMAND microworld_core_tick_example_tests)
