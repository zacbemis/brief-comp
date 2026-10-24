# End-to-end milestone-0 checks, with no Python dependency.
file(MAKE_DIRECTORY "${TEST_DIR}")
set(valid "${TEST_DIR}/valid.brf")
file(WRITE "${valid}" "main() { print(\"Hello\") }\n")
execute_process(COMMAND "${BRIEF}" dump-tokens "${valid}"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors)
if(NOT status EQUAL 0 OR NOT errors STREQUAL "")
    message(FATAL_ERROR "valid lexing failed (${status}): ${errors}")
endif()
set(expected
    "1:1 TOK_IDENT \"main\"\n1:5 TOK_LPAREN \"(\"\n1:6 TOK_RPAREN \")\"\n1:8 TOK_LBRACE \"{\"\n1:10 TOK_IDENT \"print\"\n1:15 TOK_LPAREN \"(\"\n1:16 TOK_STR_LIT \"\\\"Hello\\\"\"\n1:23 TOK_RPAREN \")\"\n1:25 TOK_RBRACE \"}\"\n1:26 TOK_NEWLINE \"\\n\"\n2:1 TOK_EOF \"\"\n")
if(NOT output STREQUAL expected)
    message(FATAL_ERROR "unexpected token dump:\n${output}\nexpected:\n${expected}")
endif()

set(invalid "${TEST_DIR}/invalid.brf")
file(WRITE "${invalid}" "main() {\n  \"\\q\"\n  @\n  /* unfinished\n")
execute_process(COMMAND "${BRIEF}" dump-tokens "${invalid}"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors)
if(NOT status EQUAL 1 OR NOT output STREQUAL "")
    message(FATAL_ERROR "invalid lexing should fail without a token dump (${status}): ${errors}")
endif()
set(expected
    "error: Unknown escape in string literal.\n  --> ${invalid}:2:4\n  |\n2 |   \"\\q\"\n  |    ^~\nerror: Unexpected character.\n  --> ${invalid}:3:3\n  |\n3 |   @\n  |   ^\nerror: Unterminated block comment.\n  --> ${invalid}:4:3\n  |\n4 |   /* unfinished\n  |   ^~~~~~~~~~~~~\n")
if(NOT errors STREQUAL expected)
    message(FATAL_ERROR "unexpected diagnostics:\n${errors}\nexpected:\n${expected}")
endif()

set(utf8 "${TEST_DIR}/utf8.brf")
file(WRITE "${utf8}" "let s = \"café \\q\"\n")
execute_process(COMMAND "${BRIEF}" dump-tokens "${utf8}"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors)
string(REPEAT " " 14 indent)
set(expected
    "error: Unknown escape in string literal.\n  --> ${utf8}:1:16\n  |\n1 | let s = \"café \\q\"\n  | ${indent}^~\n")
if(NOT status EQUAL 1 OR NOT output STREQUAL "" OR NOT errors STREQUAL expected)
    message(FATAL_ERROR "incorrect UTF-8 escape diagnostic (${status}):\n${errors}\nexpected:\n${expected}")
endif()

# An unclosed string must not swallow errors or valid strings on later lines.
# Check both line-ending styles and reject backslash-newline continuation.
foreach(line_ending IN ITEMS "\n" "\r\n")
    foreach(prefix IN ITEMS "\"abc" "\"abc\\")
        set(recovery "${TEST_DIR}/recovery.brf")
        file(WRITE "${recovery}" "${prefix}${line_ending}@ \"ok\"${line_ending}")
        execute_process(COMMAND "${BRIEF}" dump-tokens "${recovery}"
            RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors)
        string(LENGTH "${prefix}" prefix_length)
        math(EXPR tail_length "${prefix_length} - 1")
        string(REPEAT "~" ${tail_length} tail)
        set(expected
            "error: Unterminated string.\n  --> ${recovery}:1:1\n  |\n1 | ${prefix}\n  | ^${tail}\nerror: Unexpected character.\n  --> ${recovery}:2:1\n  |\n2 | @ \"ok\"\n  | ^\n")
        if(NOT status EQUAL 1 OR NOT output STREQUAL "" OR NOT errors STREQUAL expected)
            message(FATAL_ERROR "incorrect string recovery (${status}):\n${errors}\nexpected:\n${expected}")
        endif()
    endforeach()
endforeach()

# Unsupported source exponents are lexical errors, with the same single
# source-backed diagnostic in token, AST and emission commands. Strings used
# by numeric conversions keep their independent decimal/exponent grammar.
set(exponent "${TEST_DIR}/exponent.brf")
file(WRITE "${exponent}" "main() {\r\n  let value = -1.25E+2\r\n  print(\"kept\")\r\n}\r\n")
string(REPEAT " " 15 indent)
set(expected "error: Exponent notation is not supported in source number literals.\n  --> ${exponent}:2:16\n  |\n2 |   let value = -1.25E+2\n  | ${indent}^~~~~~~\n")
foreach(command IN ITEMS dump-tokens dump-ast emit-c)
    execute_process(COMMAND "${BRIEF}" "${command}" "${exponent}"
        RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors)
    if(NOT status EQUAL 1 OR NOT output STREQUAL "" OR NOT errors STREQUAL expected)
        message(FATAL_ERROR "incorrect ${command} exponent diagnostic (${status}):\n${errors}\nexpected:\n${expected}")
    endif()
endforeach()

execute_process(COMMAND "${BRIEF}" dump-tokens "${TEST_DIR}/missing.brf"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors)
if(NOT status EQUAL 1 OR NOT errors MATCHES "error: cannot read")
    message(FATAL_ERROR "missing file was not reported (${status}): ${errors}")
endif()
execute_process(COMMAND "${BRIEF}"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors)
if(NOT status EQUAL 2 OR NOT errors MATCHES "usage: brief dump-tokens")
    message(FATAL_ERROR "usage was not reported (${status}): ${errors}")
endif()
