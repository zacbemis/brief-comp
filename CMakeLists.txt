cmake_minimum_required(VERSION 3.16)
project(BriefCompiler VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_C_EXTENSIONS OFF)

option(BRIEF_ENABLE_SANITIZERS "Enable ASan and UBSan in Debug builds" ON)
add_library(brief_options INTERFACE)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(brief_options INTERFACE -Wall -Wextra -Wpedantic)
    if(BRIEF_ENABLE_SANITIZERS)
        target_compile_options(brief_options INTERFACE
            $<$<CONFIG:Debug>:-fsanitize=address,undefined>
            $<$<CONFIG:Debug>:-fno-sanitize-recover=all>
            $<$<CONFIG:Debug>:-fno-omit-frame-pointer>
        )
        target_link_options(brief_options INTERFACE
            $<$<CONFIG:Debug>:-fsanitize=address,undefined>
        )
    endif()
endif()

# Core compiler library. Sources are grouped by subsystem and added as they
# come online so tests can link against them without a main().
add_library(brief_core STATIC
    src/common/arena.c
    src/common/source.c
    src/common/diagnostic.c
    src/common/intern.c
    src/lexer/lexer.c
    src/lexer/token.c
)

target_include_directories(brief_core PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(brief_core PUBLIC brief_options)

# The milestone-0 driver exposes lexing, not compilation yet.
add_executable(brief src/main.c)
target_link_libraries(brief PRIVATE brief_core)

enable_testing()
add_subdirectory(tests)
