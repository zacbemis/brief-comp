cmake_minimum_required(VERSION 3.16)

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
    "1:1 TOK_LOWER_NAME \"main\"\n1:5 TOK_LPAREN \"(\"\n1:6 TOK_RPAREN \")\"\n1:8 TOK_LBRACE \"{\"\n1:10 TOK_LOWER_NAME \"print\"\n1:15 TOK_LPAREN \"(\"\n1:16 TOK_STR_LIT \"\\\"Hello\\\"\"\n1:23 TOK_RPAREN \")\"\n1:25 TOK_RBRACE \"}\"\n1:26 TOK_NEWLINE \"\\n\"\n2:1 TOK_EOF \"\"\n")
if(NOT output STREQUAL expected)
    message(FATAL_ERROR "unexpected token dump:\n${output}\nexpected:\n${expected}")
endif()

# Name classification is visible in the stable token dump, including '_'.
set(names "${TEST_DIR}/names.brf")
file(WRITE "${names}" "User user _ User2 user_2\n")
execute_process(COMMAND "${BRIEF}" dump-tokens "${names}"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors)
set(expected "1:1 TOK_UPPER_NAME \"User\"\n1:6 TOK_LOWER_NAME \"user\"\n1:11 TOK_WILDCARD \"_\"\n1:13 TOK_UPPER_NAME \"User2\"\n1:19 TOK_LOWER_NAME \"user_2\"\n1:25 TOK_NEWLINE \"\\n\"\n2:1 TOK_EOF \"\"\n")
if(NOT status EQUAL 0 OR NOT errors STREQUAL "" OR NOT output STREQUAL expected)
    message(FATAL_ERROR "incorrect classified token dump (${status}):\n${errors}\n${output}\nexpected:\n${expected}")
endif()

# Invalid underscore names have one lexical diagnostic in each frontend mode,
# with byte columns, tab-expanded excerpts and no guessed fix.
set(underscores "${TEST_DIR}/underscores.brf")
file(WRITE "${underscores}" "main() {\r\n\t_hidden\r\n}\r\n")
set(expected "error: Names must start with a letter; '_' alone is a wildcard.\n  --> ${underscores}:2:2\n  |\n2 |     _hidden\n  |     ^~~~~~~\n")
foreach(command IN ITEMS dump-tokens dump-ast emit-c)
    execute_process(COMMAND "${BRIEF}" "${command}" "${underscores}"
        RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors)
    if(NOT status EQUAL 1 OR NOT output STREQUAL "" OR NOT errors STREQUAL expected)
        message(FATAL_ERROR "incorrect ${command} underscore diagnostic (${status}):\n${errors}\nexpected:\n${expected}")
    endif()
endforeach()

# not is a keyword; postfix panic and inequality retain their own tokens.
set(logical "${TEST_DIR}/logical.brf")
file(WRITE "${logical}" "not flag! != false\n")
execute_process(COMMAND "${BRIEF}" dump-tokens "${logical}"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors)
set(expected "1:1 TOK_NOT \"not\"\n1:5 TOK_LOWER_NAME \"flag\"\n1:9 TOK_BANG \"!\"\n1:11 TOK_BANG_EQUAL \"!=\"\n1:14 TOK_BOOL_LIT \"false\"\n1:19 TOK_NEWLINE \"\\n\"\n2:1 TOK_EOF \"\"\n")
if(NOT status EQUAL 0 OR NOT errors STREQUAL "" OR NOT output STREQUAL expected)
    message(FATAL_ERROR "incorrect not/panic tokens (${status}):\n${errors}\n${output}")
endif()

# dbl is the binary64 keyword; dub is an ordinary name.
set(binary64 "${TEST_DIR}/binary64.brf")
file(WRITE "${binary64}" "dbl dub dblx dubx\n")
execute_process(COMMAND "${BRIEF}" dump-tokens "${binary64}"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors)
set(expected "1:1 TOK_DBL \"dbl\"\n1:5 TOK_LOWER_NAME \"dub\"\n1:9 TOK_LOWER_NAME \"dblx\"\n1:14 TOK_LOWER_NAME \"dubx\"\n1:18 TOK_NEWLINE \"\\n\"\n2:1 TOK_EOF \"\"\n")
if(NOT status EQUAL 0 OR NOT errors STREQUAL "" OR NOT output STREQUAL expected)
    message(FATAL_ERROR "incorrect dbl tokens (${status}):\n${errors}\n${output}")
endif()
file(WRITE "${binary64}" "main() { let n = dbl(1) }\r\n")
execute_process(COMMAND "${BRIEF}" emit-c "${binary64}"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors)
if(NOT status EQUAL 0 OR NOT errors STREQUAL "" OR output STREQUAL "")
    message(FATAL_ERROR "corrected dbl source failed (${status}):\n${errors}")
endif()

# Remove only the type modifier, retaining the field's valid pub modifier.
set(pub_struct "${TEST_DIR}/pub-struct.brf")
file(WRITE "${pub_struct}" "pub P { pub x: int }\r\nmain() {}\r\n")
set(expected "error: Struct types cannot be marked 'pub'.\n  --> ${pub_struct}:1:1\n  |\n1 | pub P { pub x: int }\n  | ^~~\nhelp: write `P { pub x: int }`\n")
foreach(command IN ITEMS dump-ast emit-c)
    execute_process(COMMAND "${BRIEF}" "${command}" "${pub_struct}"
        RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors)
    if(NOT status EQUAL 1 OR NOT output STREQUAL "" OR NOT errors STREQUAL expected)
        message(FATAL_ERROR "incorrect ${command} type-pub fix (${status}):\n${errors}")
    endif()
endforeach()
file(WRITE "${pub_struct}" "P { pub x: int }\r\nmain() {}\r\n")
execute_process(COMMAND "${BRIEF}" emit-c "${pub_struct}"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors)
if(NOT status EQUAL 0 OR NOT errors STREQUAL "" OR output STREQUAL "")
    message(FATAL_ERROR "corrected type visibility failed (${status}):\n${errors}")
endif()

# Indexing parses, but lowering still rejects it.
set(index "${TEST_DIR}/index.brf")
file(WRITE "${index}" "main() { 0.(0) }\r\n")
execute_process(COMMAND "${BRIEF}" dump-ast "${index}"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors)
if(NOT status EQUAL 0 OR NOT errors STREQUAL "" OR
   NOT output STREQUAL "(program (fn main (params) _ (block (index (int 0) (int 0)))))\n")
    message(FATAL_ERROR "index failed to parse (${status}):\n${errors}")
endif()
execute_process(COMMAND "${BRIEF}" emit-c "${index}"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors)
if(NOT status EQUAL 1 OR NOT output STREQUAL "" OR
   NOT errors MATCHES "error: This expression is not implemented yet\\.")
    message(FATAL_ERROR "index lost its scope rejection (${status}):\n${errors}")
endif()

set(casing "${TEST_DIR}/declaration-casing.brf")
file(WRITE "${casing}" "main() { let Count = 0 }\r\n")
set(expected "error: Binding names must start with a lowercase letter.\n  --> ${casing}:1:14\n  |\n1 | main() { let Count = 0 }\n  |              ^~~~~\nhelp: write `main() { let count = 0 }`\n")
foreach(command IN ITEMS dump-ast emit-c)
    execute_process(COMMAND "${BRIEF}" "${command}" "${casing}"
        RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors)
    if(NOT status EQUAL 1 OR NOT output STREQUAL "" OR NOT errors STREQUAL expected)
        message(FATAL_ERROR "incorrect ${command} name fix (${status}):\n${errors}")
    endif()
endforeach()
file(WRITE "${casing}" "main() { let count = 0 }\r\n")
execute_process(COMMAND "${BRIEF}" emit-c "${casing}"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors)
if(NOT status EQUAL 0 OR NOT errors STREQUAL "" OR output STREQUAL "")
    message(FATAL_ERROR "corrected declaration casing failed (${status}):\n${errors}")
endif()

set(tag_casing "${TEST_DIR}/tag-casing.brf")
file(WRITE "${tag_casing}" "main() { :Bad }\r\n")
set(expected "error: Symbol tags must start with a lowercase letter.\n  --> ${tag_casing}:1:11\n  |\n1 | main() { :Bad }\n  |           ^~~\nhelp: write `main() { :bad }`\n")
foreach(command IN ITEMS dump-ast emit-c)
    execute_process(COMMAND "${BRIEF}" "${command}" "${tag_casing}"
        RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors)
    if(NOT status EQUAL 1 OR NOT output STREQUAL "" OR NOT errors STREQUAL expected)
        message(FATAL_ERROR "incorrect ${command} tag fix (${status}):\n${errors}")
    endif()
endforeach()

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
