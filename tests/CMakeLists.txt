# Each test is a standalone executable registered with CTest. Add new tests
# here with add_brief_test(<name>).

function(add_brief_test name)
    add_executable(${name} ${name}.c)
    target_link_libraries(${name} PRIVATE brief_core)
    target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME ${name} COMMAND ${name})
endfunction()

add_brief_test(test_common)
add_brief_test(test_lexer)
add_brief_test(test_newline)
add_brief_test(test_parser)
add_brief_test(test_compile)
add_brief_test(test_sema)
add_brief_test(test_flow)
set_tests_properties(test_flow PROPERTIES TIMEOUT 15)
add_brief_test(test_structs)
add_brief_test(test_matching)
add_brief_test(test_symbols)
add_brief_test(test_errors)
add_brief_test(test_conversions)
target_link_libraries(test_conversions PRIVATE m)
set_tests_properties(test_conversions PROPERTIES TIMEOUT 30)
set_tests_properties(test_errors PROPERTIES TIMEOUT 30)
set_tests_properties(test_symbols PROPERTIES TIMEOUT 30)
set_tests_properties(test_matching PROPERTIES TIMEOUT 30)
add_brief_test(test_objects)
add_brief_test(test_ownership)
set_tests_properties(test_structs PROPERTIES TIMEOUT 30)

# Binary-safe POSIX test tools, built with the same warning/sanitizer options
# as the compiler. No interpreter or shell is needed to run program tests.
add_library(brief_test_support STATIC process.c program_expect.c)
target_link_libraries(brief_test_support PUBLIC brief_core)
target_include_directories(brief_test_support PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_objects PRIVATE brief_test_support m)
target_compile_definitions(test_objects PRIVATE BRT_DEBUG)
foreach(tool run_programs test_program_runner test_cli_native test_process test_arithmetic)
    add_executable(${tool} ${tool}.c)
    target_link_libraries(${tool} PRIVATE brief_test_support)
endforeach()
target_link_libraries(test_arithmetic PRIVATE m)
add_executable(fake_cc fake_cc.c)
target_link_libraries(fake_cc PRIVATE brief_options)
add_test(NAME test_program_runner COMMAND test_program_runner)
add_test(NAME test_arithmetic COMMAND test_arithmetic)
set_tests_properties(test_arithmetic PROPERTIES TIMEOUT 30)
add_test(NAME test_process COMMAND test_process $<TARGET_FILE:fake_cc>)
set_tests_properties(test_process PROPERTIES TIMEOUT 15)
set(native_test_options)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang" AND BRIEF_ENABLE_SANITIZERS)
    list(APPEND native_test_options --debug-supported)
endif()
add_test(NAME test_cli_native
    COMMAND test_cli_native $<TARGET_FILE:brief> ${CMAKE_C_COMPILER}
        $<TARGET_FILE:fake_cc> ${native_test_options})
add_test(NAME test_programs
    COMMAND run_programs --brief $<TARGET_FILE:brief> --cc ${CMAKE_C_COMPILER}
        --programs ${CMAKE_CURRENT_SOURCE_DIR}/programs)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang" AND BRIEF_ENABLE_SANITIZERS)
    add_test(NAME test_programs_sanitized
        COMMAND run_programs --brief $<TARGET_FILE:brief> --cc ${CMAKE_C_COMPILER}
            --programs ${CMAKE_CURRENT_SOURCE_DIR}/programs --debug)
endif()

add_test(NAME test_doc_parser
    COMMAND ${CMAKE_COMMAND}
        -DBRIEF=$<TARGET_FILE:brief>
        -DROOT=${CMAKE_SOURCE_DIR}
        -DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/current-doc-parser-fixtures
        -P ${CMAKE_CURRENT_SOURCE_DIR}/test_doc_parser.cmake
)

# Native v0 chapter audits/bridges are retired. New chapter audits return in
# their implementation milestones; this parser manifest reads current docs only.
set_tests_properties(test_doc_parser PROPERTIES LABELS doc-parser TIMEOUT 30)
add_test(NAME test_doc_parser_manifest
    COMMAND ${CMAKE_COMMAND}
        -DROOT=${CMAKE_SOURCE_DIR}
        -DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/doc-parser-manifest-fixtures
        -P ${CMAKE_CURRENT_SOURCE_DIR}/test_doc_parser_manifest.cmake)
set_tests_properties(test_doc_parser_manifest PROPERTIES LABELS doc-parser TIMEOUT 15)

# Current native audits: unchanged whole fences with only entry/observer
# context, separate prose programs and explicit listings (no v0/API mocks).
function(add_current_native_audit chapter)
    set(modes normal)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang" AND BRIEF_ENABLE_SANITIZERS)
        list(APPEND modes sanitized)
    endif()
    foreach(mode IN LISTS modes)
        set(suffix "")
        set(debug OFF)
        if(mode STREQUAL "sanitized")
            set(suffix "_sanitized")
            set(debug ON)
        endif()
        add_test(NAME test_doc_${chapter}_n1${suffix}
            COMMAND ${CMAKE_COMMAND}
                -DBRIEF=$<TARGET_FILE:brief>
                -DRUNNER=$<TARGET_FILE:run_programs>
                -DCC=${CMAKE_C_COMPILER}
                -DROOT=${CMAKE_SOURCE_DIR}
                -DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/current-doc-${chapter}${suffix}
                -DDEBUG=${debug}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/test_doc_${chapter}_n1.cmake)
        set(labels "doc-native;doc-${chapter}")
        if(chapter STREQUAL "base")
            list(APPEND labels native-examples)
        endif()
        set_tests_properties(test_doc_${chapter}_n1${suffix} PROPERTIES
            LABELS "${labels}" TIMEOUT 180)
    endforeach()
endfunction()
add_current_native_audit(functions)
add_current_native_audit(types)
add_current_native_audit(syntax)
add_current_native_audit(modules)
add_current_native_audit(base)
add_test(NAME test_doc_native_manifest
    COMMAND ${CMAKE_COMMAND}
        -DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/doc-native-manifest-fixtures
        -P ${CMAKE_CURRENT_SOURCE_DIR}/test_doc_native_manifest.cmake)
set_tests_properties(test_doc_native_manifest PROPERTIES
    LABELS "doc-native;doc-functions;doc-types;doc-syntax;doc-modules;doc-base;native-examples" TIMEOUT 30)

# Run alone with `ctest -L parse-examples`, or skip with `-LE parse-examples`.
add_test(NAME test_parse_examples
    COMMAND ${CMAKE_COMMAND}
        -DBRIEF=$<TARGET_FILE:brief>
        -DEXAMPLES=${CMAKE_CURRENT_SOURCE_DIR}/parse
        -P ${CMAKE_CURRENT_SOURCE_DIR}/test_parse_examples.cmake
)
set_tests_properties(test_parse_examples PROPERTIES LABELS parse-examples)

add_test(NAME test_failure_spelling
    COMMAND ${CMAKE_COMMAND}
        -DBRIEF=$<TARGET_FILE:brief>
        -DSOURCE_DIR=${PROJECT_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/test_failure_spelling.cmake
)
set_tests_properties(test_failure_spelling PROPERTIES LABELS source-spelling TIMEOUT 30)

add_test(NAME test_cli_ast
    COMMAND ${CMAKE_COMMAND}
        -DBRIEF=$<TARGET_FILE:brief>
        -DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/cli-fixtures
        -P ${CMAKE_CURRENT_SOURCE_DIR}/test_cli_ast.cmake
)

add_test(NAME test_cli
    COMMAND ${CMAKE_COMMAND}
        -DBRIEF=$<TARGET_FILE:brief>
        -DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/cli-fixtures
        -P ${CMAKE_CURRENT_SOURCE_DIR}/test_cli.cmake
)
