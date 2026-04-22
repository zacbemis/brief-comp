# Each test is a standalone executable registered with CTest. Add new tests
# here with add_brief_test(<name>).

function(add_brief_test name)
    add_executable(${name} ${name}.c)
    target_link_libraries(${name} PRIVATE brief_core)
    target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME ${name} COMMAND ${name})
endfunction()

add_brief_test(test_lexer)
