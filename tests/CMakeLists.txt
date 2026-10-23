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
        -DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/doc-parser-fixtures
        -P ${CMAKE_CURRENT_SOURCE_DIR}/test_doc_parser.cmake
)

function(add_doc_acceptance_test name script label debug)
    add_test(NAME ${name}
        COMMAND ${CMAKE_COMMAND}
            -DBRIEF=$<TARGET_FILE:brief>
            -DRUNNER=$<TARGET_FILE:run_programs>
            -DCC=${CMAKE_C_COMPILER}
            -DROOT=${CMAKE_SOURCE_DIR}
            -DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/${name}-fixtures
            -DDEBUG=${debug}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/${script})
    set_tests_properties(${name} PROPERTIES LABELS "doc-acceptance;${label}" TIMEOUT 120)
endfunction()
add_doc_acceptance_test(test_doc_errors test_doc_errors.cmake doc-errors OFF)
add_doc_acceptance_test(test_doc_pipelines test_doc_pipelines.cmake doc-pipelines OFF)
add_doc_acceptance_test(test_doc_functions test_doc_functions.cmake doc-functions OFF)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang" AND BRIEF_ENABLE_SANITIZERS)
    add_doc_acceptance_test(test_doc_errors_sanitized test_doc_errors.cmake doc-errors ON)
    add_doc_acceptance_test(test_doc_pipelines_sanitized test_doc_pipelines.cmake doc-pipelines ON)
    add_doc_acceptance_test(test_doc_functions_sanitized test_doc_functions.cmake doc-functions ON)
endif()

add_test(NAME test_doc_examples_support
    COMMAND ${CMAKE_COMMAND}
        -DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/doc-example-support-fixtures
        -P ${CMAKE_CURRENT_SOURCE_DIR}/test_doc_examples_support.cmake)
set_tests_properties(test_doc_examples_support PROPERTIES LABELS doc-acceptance TIMEOUT 15)

# Run alone with `ctest -L parse-examples`, or skip with `-LE parse-examples`.
add_test(NAME test_parse_examples
    COMMAND ${CMAKE_COMMAND}
        -DBRIEF=$<TARGET_FILE:brief>
        -DEXAMPLES=${CMAKE_CURRENT_SOURCE_DIR}/parse
        -P ${CMAKE_CURRENT_SOURCE_DIR}/test_parse_examples.cmake
)
set_tests_properties(test_parse_examples PROPERTIES LABELS parse-examples)

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
