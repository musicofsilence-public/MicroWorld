cmake_minimum_required(VERSION 3.20)

set(PICO_BOARD pico CACHE STRING "Pico SDK board")
set(PICO_NO_PICOTOOL ON CACHE BOOL "Avoid building the optional host picotool dependency" FORCE)
set(PICO_SDK_FETCH_FROM_GIT ON CACHE BOOL "" FORCE)
set(
	PICO_SDK_FETCH_FROM_GIT_TAG
	a1438dff1d38bd9c65dbd693f0e5db4b9ae91779
	CACHE STRING
	"Pico SDK 2.2.0 commit"
	FORCE
)
include(pico_sdk_import.cmake)

project(microworld_pico_freertos_consumer LANGUAGES C CXX ASM)

pico_sdk_init()

find_program(MICROWORLD_GIT_EXECUTABLE NAMES git REQUIRED)
if(NOT MICROWORLD_ELF2UF2)
	find_program(MICROWORLD_ELF2UF2 NAMES elf2uf2 REQUIRED)
endif()

function(microworld_verify_git_revision SourceDirectory ExpectedRevision DependencyName)
	execute_process(
		COMMAND "${MICROWORLD_GIT_EXECUTABLE}" rev-parse HEAD
		WORKING_DIRECTORY "${SourceDirectory}"
		RESULT_VARIABLE RevisionResult
		OUTPUT_VARIABLE ActualRevision
		OUTPUT_STRIP_TRAILING_WHITESPACE
		ERROR_VARIABLE RevisionError
	)
	if(NOT RevisionResult EQUAL 0)
		message(FATAL_ERROR "Could not resolve ${DependencyName} revision: ${RevisionError}")
	endif()
	if(NOT ActualRevision STREQUAL ExpectedRevision)
		message(FATAL_ERROR "${DependencyName} resolved to ${ActualRevision}; expected ${ExpectedRevision}")
	endif()
	message(STATUS "${DependencyName} revision: ${ActualRevision}")
endfunction()

microworld_verify_git_revision(
	"${PICO_SDK_PATH}"
	"a1438dff1d38bd9c65dbd693f0e5db4b9ae91779"
	"Pico SDK"
)

set(
	FREERTOS_CONFIG_FILE_DIRECTORY
	"${CMAKE_CURRENT_LIST_DIR}"
	CACHE PATH
	"Directory containing the consumer FreeRTOSConfig.h"
	FORCE
)

include(FetchContent)
FetchContent_Declare(
	freertos_kernel
	GIT_REPOSITORY https://github.com/FreeRTOS/FreeRTOS-Kernel.git
	GIT_TAG 9b777ae5c5b8e9e456065a00294d1e5f5f9facf5
	GIT_SHALLOW FALSE
	SOURCE_SUBDIR portable/ThirdParty/GCC/RP2040
)
FetchContent_MakeAvailable(freertos_kernel)
microworld_verify_git_revision(
	"${freertos_kernel_SOURCE_DIR}"
	"9b777ae5c5b8e9e456065a00294d1e5f5f9facf5"
	"FreeRTOS-Kernel"
)

set(MICROWORLD_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(MICROWORLD_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
# One Modules add provides every portable target plus the PlatformHost and the
# host PlatformPico INTERFACE facade. ../../../.. resolves from
# Modules/tests/Core/consumer/pico-freertos/ to Modules/, the package root.
add_subdirectory(
    "${CMAKE_CURRENT_LIST_DIR}/../../../.."
    "${CMAKE_CURRENT_BINARY_DIR}/microworld"
)

# The native Pico device needs an initialized Pico SDK (hardware_gpio,
# hardware_uart), which the host Modules build deliberately does not provide.
# Build the device locally from the merged Platform/Pico sources so the host
# facade stays SDK-free while the Pico firmware links the real adapter. RadioE32
# folds into the Transport target (MicroWorld::Transport), so the device links it.
set(MICROWORLD_PLATFORM_PICO_DEVICE_SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/../../../../MicroWorld/Platform/Pico/PicoLoraDevice.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../../../../MicroWorld/Platform/Pico/PicoUartByteStream.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../../../../MicroWorld/Platform/Pico/PicoUartPlatform.cpp"
)
add_library(microworld_platform_pico_device STATIC ${MICROWORLD_PLATFORM_PICO_DEVICE_SOURCES})
target_include_directories(
    microworld_platform_pico_device
    PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/../../../../>"
)
target_compile_features(microworld_platform_pico_device PUBLIC cxx_std_17)
target_link_libraries(
    microworld_platform_pico_device
    PUBLIC MicroWorld::Transport
    PRIVATE hardware_gpio hardware_uart
)
set_source_files_properties(
    ${MICROWORLD_PLATFORM_PICO_DEVICE_SOURCES}
    PROPERTIES
        COMPILE_OPTIONS
            "-Wall;-Wextra;-Wpedantic;-Werror;-fno-exceptions;-fno-rtti"
)

set(
	MICROWORLD_PICO_CORE_TEST_SOURCES
	PicoCoreAllocationCounters.cpp
	"${CMAKE_CURRENT_LIST_DIR}/../../LogTests.cpp"
	"${CMAKE_CURRENT_LIST_DIR}/../../TickFunctionTests.cpp"
	"${CMAKE_CURRENT_LIST_DIR}/../../MemoryTests.cpp"
	"${CMAKE_CURRENT_LIST_DIR}/../../DelegateTests.cpp"
	"${CMAKE_CURRENT_LIST_DIR}/../../TimerManagerTests.cpp"
)
set(
	MICROWORLD_CORE_TICK_SOURCE_DIRECTORY
	"${CMAKE_CURRENT_LIST_DIR}/../../../../../examples/01-CoreTick/src"
)

function(microworld_pico_configure_freertos_target Target)
	target_link_libraries(
		${Target}
		PRIVATE
			MicroWorld::Core
			FreeRTOS-Kernel-Static
	)
	target_compile_features(${Target} PRIVATE cxx_std_17)
endfunction()

function(microworld_pico_configure_owned_target Target)
	microworld_pico_configure_freertos_target(${Target})
	target_link_libraries(${Target} PRIVATE pico_stdlib)
endfunction()

function(microworld_pico_add_uf2_output Target)
	set(Uf2Path "${CMAKE_CURRENT_BINARY_DIR}/${Target}.uf2")
	add_custom_command(
		TARGET ${Target}
		POST_BUILD
		COMMAND "${MICROWORLD_ELF2UF2}" "$<TARGET_FILE:${Target}>" "${Uf2Path}"
		BYPRODUCTS "${Uf2Path}"
		VERBATIM
	)
endfunction()

set(
	MICROWORLD_PICO_OWNED_SOURCES
	PicoFreeRtosMain.cpp
	PicoLoraInteropMain.cpp
	PicoCoreTestsMain.cpp
	"${MICROWORLD_CORE_TICK_SOURCE_DIRECTORY}/PicoMain.cpp"
	"${MICROWORLD_CORE_TICK_SOURCE_DIRECTORY}/CoreTickExample.cpp"
	${MICROWORLD_PICO_CORE_TEST_SOURCES}
)
set_source_files_properties(
	${MICROWORLD_PICO_OWNED_SOURCES}
	PROPERTIES
		COMPILE_OPTIONS
			"-Wall;-Wextra;-Wpedantic;-Werror;-fno-exceptions;-fno-rtti"
)
target_compile_options(microworld PRIVATE -fno-exceptions -fno-rtti)

add_executable(microworld_pico_freertos_consumer PicoFreeRtosMain.cpp)
microworld_pico_configure_owned_target(microworld_pico_freertos_consumer)
pico_add_extra_outputs(microworld_pico_freertos_consumer)
microworld_pico_add_uf2_output(microworld_pico_freertos_consumer)

add_executable(
	microworld_pico_core_tick_example
	"${MICROWORLD_CORE_TICK_SOURCE_DIRECTORY}/PicoMain.cpp"
	"${MICROWORLD_CORE_TICK_SOURCE_DIRECTORY}/CoreTickExample.cpp"
)
microworld_pico_configure_owned_target(microworld_pico_core_tick_example)
pico_add_extra_outputs(microworld_pico_core_tick_example)
microworld_pico_add_uf2_output(microworld_pico_core_tick_example)

add_executable(
	microworld_pico_core_tests
	PicoCoreTestsMain.cpp
	${MICROWORLD_PICO_CORE_TEST_SOURCES}
)
target_include_directories(
	microworld_pico_core_tests
	PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../.."
)
microworld_pico_configure_owned_target(microworld_pico_core_tests)
pico_add_extra_outputs(microworld_pico_core_tests)
microworld_pico_add_uf2_output(microworld_pico_core_tests)

add_executable(microworld_pico_lora_interop PicoLoraInteropMain.cpp)
microworld_pico_configure_owned_target(microworld_pico_lora_interop)
target_compile_definitions(microworld_pico_lora_interop PRIVATE PICO_CXX_DISABLE_ALLOCATION_OVERRIDES=1)
target_link_libraries(microworld_pico_lora_interop PRIVATE microworld_platform_pico_device)
pico_add_extra_outputs(microworld_pico_lora_interop)
microworld_pico_add_uf2_output(microworld_pico_lora_interop)

add_executable(microworld_pico_lora_payload_regression PicoLoraInteropMain.cpp)
microworld_pico_configure_owned_target(microworld_pico_lora_payload_regression)
target_compile_definitions(
	microworld_pico_lora_payload_regression
	PRIVATE
		PICO_CXX_DISABLE_ALLOCATION_OVERRIDES=1
		MICROWORLD_LORA_PAYLOAD_REGRESSION
)
target_include_directories(
	microworld_pico_lora_payload_regression
	PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../../../../../examples/17-TwoBoardLora/src"
)
target_link_libraries(microworld_pico_lora_payload_regression PRIVATE microworld_platform_pico_device)
pico_add_extra_outputs(microworld_pico_lora_payload_regression)
microworld_pico_add_uf2_output(microworld_pico_lora_payload_regression)
