idf_component_register(SRCS "Main.cpp")

# Why: keep the engine's strict portable contract on example code. ESP-IDF's
# esp_libc headers use the GCC #include_next extension (pedantic warning) and
# GCC 15's libstdc++ deprecates std::aligned_storage used by C++17 MicroWorld
# headers, so exactly those two diagnostics are downgraded — the same scoped
# relaxations the verified consumer project uses.
target_compile_options(
    ${COMPONENT_LIB}
    PRIVATE
        -std=gnu++17
        -Wall
        -Wextra
        -Wpedantic
        -Werror
        -Wno-error=pedantic
        -Wno-deprecated-declarations
        -fno-exceptions
        -fno-rtti
)
