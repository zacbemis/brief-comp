cmake_minimum_required(VERSION 3.16)

# Runs the example files in tests/parse (see tests/parse/README.md).
#
# pass/*.brf must parse: dump-ast exits 0, prints a tree, and writes nothing to
# stderr. fail/*.brf must be rejected: dump-ast exits 1 and prints no tree. The
# first diagnostic must be on the line named by the first `// parse-error:`
# header; recovery-* files must report exactly the listed lines, in order.
# Every failure is collected before the test fails, so one run shows them all.

set(failures "")
set(count 0)

function(fail file reason)
    set(failures "${failures}\n  ${file}: ${reason}" PARENT_SCOPE)
endfunction()

file(GLOB pass_files "${EXAMPLES}/pass/*.brf")
file(GLOB fail_files "${EXAMPLES}/fail/*.brf")
list(SORT pass_files)
list(SORT fail_files)
if(NOT pass_files OR NOT fail_files)
    message(FATAL_ERROR "no example files found under ${EXAMPLES}")
endif()

foreach(path IN LISTS pass_files)
    get_filename_component(name "${path}" NAME)
    execute_process(COMMAND "${BRIEF}" dump-ast "${path}"
        RESULT_VARIABLE status OUTPUT_VARIABLE tree ERROR_VARIABLE errors)
    if(NOT status EQUAL 0 OR NOT errors STREQUAL "" OR NOT tree MATCHES "^\\(program")
        string(REGEX MATCH "error: [^\n]*\n  --> [^\n]*" first "${errors}")
        fail("pass/${name}" "did not parse (status ${status}): ${first}")
    endif()
    math(EXPR count "${count} + 1")
endforeach()

foreach(path IN LISTS fail_files)
    get_filename_component(name "${path}" NAME)
    file(READ "${path}" text)
    # Semicolons would split CMake lists; no header or location contains one.
    string(REPLACE ";" "<semicolon>" text "${text}")
    string(REGEX MATCHALL "\n" newlines "\n${text}")
    list(LENGTH newlines end_of_file_line)

    set(expected "")
    string(REGEX MATCHALL "(^|\n)// parse-error: [^\n]*" headers "${text}")
    foreach(header IN LISTS headers)
        if(header MATCHES "parse-error: line ([0-9]+)$")
            list(APPEND expected "${CMAKE_MATCH_1}")
        elseif(header MATCHES "parse-error: end of file$")
            list(APPEND expected "${end_of_file_line}")
        else()
            fail("fail/${name}" "malformed header: ${header}")
        endif()
    endforeach()
    if(NOT expected)
        fail("fail/${name}" "no // parse-error: header")
        continue()
    endif()

    execute_process(COMMAND "${BRIEF}" dump-ast "${path}"
        RESULT_VARIABLE status OUTPUT_VARIABLE tree ERROR_VARIABLE errors)
    string(REPLACE ";" "<semicolon>" errors "${errors}")
    if(errors MATCHES "AddressSanitizer|runtime error:")
        fail("fail/${name}" "sanitizer report")
    endif()
    if(NOT status EQUAL 1 OR NOT tree STREQUAL "")
        fail("fail/${name}" "was not rejected (status ${status})")
        math(EXPR count "${count} + 1")
        continue()
    endif()

    # The line of each `error:`; the `-->` line that follows it ends ":LINE:COL".
    set(actual "")
    string(REGEX MATCHALL "(^|\n)error: [^\n]*\n  --> [^\n]*" located "${errors}")
    foreach(entry IN LISTS located)
        string(REGEX REPLACE ".*:([0-9]+):[0-9]+$" "\\1" line "${entry}")
        list(APPEND actual "${line}")
    endforeach()

    if(name MATCHES "^recovery-")
        if(NOT actual STREQUAL expected)
            fail("fail/${name}" "expected errors on lines ${expected}, got ${actual}")
        endif()
    else()
        list(GET expected 0 want)
        if(actual)
            list(GET actual 0 got)
        else()
            set(got "none")
        endif()
        if(NOT got STREQUAL want)
            fail("fail/${name}" "expected the first error on line ${want}, got ${got}")
        endif()
    endif()
    math(EXPR count "${count} + 1")
endforeach()

if(failures)
    message(FATAL_ERROR "parse example failures:${failures}")
endif()
message(STATUS "Checked ${count} parse example files")
