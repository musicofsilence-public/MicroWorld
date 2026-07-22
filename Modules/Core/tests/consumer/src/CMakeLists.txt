get_filename_component(
    MICROWORLD_CONSUMER_ENVIRONMENT
    "${BUILD_DIR}"
    NAME
)

# PlatformIO isolates each environment in a same-named build directory, which
# gives ESP-IDF one entry point even though it ignores build_src_filter.
if(MICROWORLD_CONSUMER_ENVIRONMENT STREQUAL "esp32-s3")
    set(MICROWORLD_CONSUMER_SOURCE Esp32Main.cpp)
elseif(MICROWORLD_CONSUMER_ENVIRONMENT STREQUAL "esp32-s3-memory")
    set(MICROWORLD_CONSUMER_SOURCE MemoryEsp32Main.cpp)
elseif(MICROWORLD_CONSUMER_ENVIRONMENT STREQUAL "esp32-s3-object")
    set(MICROWORLD_CONSUMER_SOURCE ObjectEsp32Main.cpp)
elseif(MICROWORLD_CONSUMER_ENVIRONMENT STREQUAL "esp32-s3-engine")
    set(MICROWORLD_CONSUMER_SOURCE EngineEsp32Main.cpp)
elseif(MICROWORLD_CONSUMER_ENVIRONMENT STREQUAL "esp32-s3-net")
    set(MICROWORLD_CONSUMER_SOURCE NetEsp32Main.cpp)
elseif(MICROWORLD_CONSUMER_ENVIRONMENT STREQUAL "esp32-s3-platform")
    set(MICROWORLD_CONSUMER_SOURCE PlatformEsp32Main.cpp)
elseif(MICROWORLD_CONSUMER_ENVIRONMENT STREQUAL "esp32-s3-benchmark")
    set(MICROWORLD_CONSUMER_SOURCE Esp32BenchmarkMain.cpp)
else()
    message(FATAL_ERROR
        "Unsupported MicroWorld ESP32 consumer environment: "
        "${MICROWORLD_CONSUMER_ENVIRONMENT}")
endif()

idf_component_register(SRCS "${MICROWORLD_CONSUMER_SOURCE}")

if(
    MICROWORLD_CONSUMER_ENVIRONMENT STREQUAL "esp32-s3-memory"
    OR MICROWORLD_CONSUMER_ENVIRONMENT STREQUAL "esp32-s3-object"
    OR MICROWORLD_CONSUMER_ENVIRONMENT STREQUAL "esp32-s3-engine"
    OR MICROWORLD_CONSUMER_ENVIRONMENT STREQUAL "esp32-s3-net"
    OR MICROWORLD_CONSUMER_ENVIRONMENT STREQUAL "esp32-s3-platform"
    OR MICROWORLD_CONSUMER_ENVIRONMENT STREQUAL "esp32-s3-benchmark"
)
    # PlatformIO forwards project build flags to ESP-IDF C sources as well as
    # this C++ component. Keep C++-only portability gates on the target that
    # instantiates Memory's representative public templates. GCC 15.2's
    # libstdc++ diagnoses std::aligned_storage in the C++17 StaticVector and
    # Delegate headers as deprecated; suppress only that declaration warning
    # until the separately owned production headers adopt replacement storage.
    target_compile_options(
        ${COMPONENT_LIB}
        PRIVATE
            -std=gnu++17
            -Wall
            -Wextra
            -Wpedantic
            -Werror
            -Wno-deprecated-declarations
            -fno-exceptions
            -fno-rtti
    )
endif()

if(
    MICROWORLD_CONSUMER_ENVIRONMENT STREQUAL "esp32-s3-platform"
    OR MICROWORLD_CONSUMER_ENVIRONMENT STREQUAL "esp32-s3-benchmark"
)
    # The platform env was the first consumer to #include ESP-IDF headers from C++
    # (esp_timer.h, esp_log.h, lwip/sockets.h); the benchmark env does the same
    # (esp_timer.h, esp_heap_caps.h, esp_log.h, freertos/task.h). ESP-IDF's
    # esp_libc platform headers (stdio.h, assert.h) use the GCC `#include_next`
    # extension, which -Wpedantic flags as an error in code the project does not
    # own. Keep the strict set above but downgrade pedantic to a non-fatal warning
    # for these envs only, so the extension in ESP-IDF system headers does not
    # block the compile while project code still warns on its own pedantic
    # violations.
    target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-error=pedantic)
endif()
