# Each test is a standalone executable registered with CTest. Add new tests
# here with add_brief_test(<name>).

function(add_brief_test name)
    add_executable(${name} ${name}.c)
    target_link_libraries(${name} PRIVATE brief_core)
    # White-box tests may include pass-private src/<pass>/internal.h headers.
    target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
        ${PROJECT_SOURCE_DIR}/src)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

foreach(area storage diagnostics)
    add_brief_test(test_common_${area})
endforeach()
foreach(area tokens syntax literals source programs)
    add_brief_test(test_lexer_${area})
endforeach()
add_brief_test(test_newline)
foreach(area declarations types names expressions hints symbols patterns recovery)
    add_brief_test(test_parser_${area})
endforeach()
add_brief_test(test_compile)
foreach(area declarations inference)
    add_brief_test(test_sema_${area})
endforeach()
add_brief_test(test_flow)
set_tests_properties(test_flow PROPERTIES TIMEOUT 15)
foreach(area types methods lookup permissions)
    add_brief_test(test_structs_${area})
    set_tests_properties(test_structs_${area} PROPERTIES TIMEOUT 30)
endforeach()
add_brief_test(test_matching)
foreach(area sets positions payloads kinds data outcomes handling)
    add_brief_test(test_symbols_${area})
    set_tests_properties(test_symbols_${area} PROPERTIES TIMEOUT 30)
endforeach()
foreach(area handling inference)
    add_brief_test(test_errors_${area})
    set_tests_properties(test_errors_${area} PROPERTIES TIMEOUT 30)
endforeach()
add_brief_test(test_returns_exact)
add_brief_test(test_returns_none)
add_brief_test(test_returns_outcomes)
add_brief_test(test_outcome_bindings)
add_brief_test(test_outcome_bindings_paths)
add_brief_test(test_outcome_bindings_loops)
add_brief_test(test_shift_counts)
add_brief_test(test_definite_assignment)
add_brief_test(test_definite_assignment_paths)
add_brief_test(test_let_else)
add_brief_test(test_inclusive_ranges)
set_tests_properties(test_inclusive_ranges PROPERTIES TIMEOUT 30)
set_tests_properties(test_let_else PROPERTIES TIMEOUT 30)
set_tests_properties(test_definite_assignment_paths PROPERTIES TIMEOUT 30)
set_tests_properties(test_definite_assignment PROPERTIES TIMEOUT 30)
set_tests_properties(test_shift_counts PROPERTIES TIMEOUT 30)
set_tests_properties(test_outcome_bindings_loops PROPERTIES TIMEOUT 30)
set_tests_properties(test_outcome_bindings_paths PROPERTIES TIMEOUT 30)
set_tests_properties(test_outcome_bindings PROPERTIES TIMEOUT 30)
set_tests_properties(test_returns_outcomes PROPERTIES TIMEOUT 30)
add_brief_test(test_conversions)
target_link_libraries(test_conversions PRIVATE m)
set_tests_properties(test_conversions PROPERTIES TIMEOUT 30)
set_tests_properties(test_matching PROPERTIES TIMEOUT 30)
add_brief_test(test_objects)
add_brief_test(test_ownership)

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
# Program fixtures run in shards so CTest can check them in parallel.
set(BRIEF_PROGRAM_SHARDS 8)
math(EXPR last_program_shard "${BRIEF_PROGRAM_SHARDS} - 1")
foreach(shard RANGE ${last_program_shard})
    add_test(NAME test_programs_${shard}
        COMMAND run_programs --brief $<TARGET_FILE:brief> --cc ${CMAKE_C_COMPILER}
            --programs ${CMAKE_CURRENT_SOURCE_DIR}/programs
            --shard ${shard}/${BRIEF_PROGRAM_SHARDS})
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang" AND BRIEF_ENABLE_SANITIZERS)
        add_test(NAME test_programs_sanitized_${shard}
            COMMAND run_programs --brief $<TARGET_FILE:brief>
                --cc ${CMAKE_C_COMPILER}
                --programs ${CMAKE_CURRENT_SOURCE_DIR}/programs --debug
                --shard ${shard}/${BRIEF_PROGRAM_SHARDS})
    endif()
endforeach()

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
    set(milestone n1)
    if(ARGC GREATER 1)
        set(milestone "${ARGV1}")
    endif()
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
        add_test(NAME test_doc_${chapter}_${milestone}${suffix}
            COMMAND ${CMAKE_COMMAND}
                -DBRIEF=$<TARGET_FILE:brief>
                -DRUNNER=$<TARGET_FILE:run_programs>
                -DCC=${CMAKE_C_COMPILER}
                -DROOT=${CMAKE_SOURCE_DIR}
                -DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/current-doc-${chapter}${suffix}
                -DDEBUG=${debug}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/test_doc_${chapter}_${milestone}.cmake)
        set(labels "doc-native;doc-${chapter}")
        if(chapter STREQUAL "base")
            list(APPEND labels native-examples)
        endif()
        set_tests_properties(test_doc_${chapter}_${milestone}${suffix} PROPERTIES
            LABELS "${labels}" TIMEOUT 180)
    endforeach()
endfunction()
add_current_native_audit(functions)
add_current_native_audit(types)
add_current_native_audit(syntax)
add_current_native_audit(modules)
add_current_native_audit(base)
add_current_native_audit(symbols n2)
add_current_native_audit(errors n2)
add_current_native_audit(pipelines n3)
add_current_native_audit(memory n3)
add_test(NAME test_doc_native_manifest
    COMMAND ${CMAKE_COMMAND}
        -DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/doc-native-manifest-fixtures
        -P ${CMAKE_CURRENT_SOURCE_DIR}/test_doc_native_manifest.cmake)
set_tests_properties(test_doc_native_manifest PROPERTIES
    LABELS "doc-native;doc-functions;doc-types;doc-syntax;doc-modules;doc-base;doc-symbols;doc-errors;doc-pipelines;doc-memory;native-examples" TIMEOUT 30)

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
