cmake_minimum_required(VERSION 3.16)
project(BriefCompiler VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Core compiler library. Sources are grouped by subsystem and added as they
# come online so tests can link against them without a main().
add_library(brief_core STATIC
    src/lexer/lexer.c
    src/lexer/token.c
)

target_include_directories(brief_core PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)

# Main compiler executable. Only built once a main.c exists.
if(EXISTS "${CMAKE_SOURCE_DIR}/src/main.c")
    add_executable(brief src/main.c)
    target_link_libraries(brief PRIVATE brief_core)
endif()

enable_testing()
add_subdirectory(tests)
