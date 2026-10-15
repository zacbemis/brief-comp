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

add_test(NAME test_cli
    COMMAND ${CMAKE_COMMAND}
        -DBRIEF=$<TARGET_FILE:brief>
        -DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/cli-fixtures
        -P ${CMAKE_CURRENT_SOURCE_DIR}/test_cli.cmake
)
