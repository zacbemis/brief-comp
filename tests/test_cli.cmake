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

# The spelling fix must separate keywords even for adjacent if!false input.
# Token dumping remains lexer-only; parsing/emission cannot accept old prefix !.
set(old_not "${TEST_DIR}/old-not.brf")
file(WRITE "${old_not}" "main() { if!false {} }\r\n")
string(REPEAT " " 11 indent)
set(expected "error: Logical not is written 'not'.\n  --> ${old_not}:1:12\n  |\n1 | main() { if!false {} }\n  | ${indent}^\nhelp: write `main() { if not false {} }`\n")
foreach(command IN ITEMS dump-ast emit-c)
    execute_process(COMMAND "${BRIEF}" "${command}" "${old_not}"
        RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors)
    if(NOT status EQUAL 1 OR NOT output STREQUAL "" OR NOT errors STREQUAL expected)
        message(FATAL_ERROR "incorrect ${command} prefix-not fix (${status}):\n${errors}\nexpected:\n${expected}")
    endif()
endforeach()
execute_process(COMMAND "${BRIEF}" dump-tokens "${old_not}"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors)
if(NOT status EQUAL 0 OR NOT errors STREQUAL "" OR NOT output MATCHES "TOK_BANG")
    message(FATAL_ERROR "old prefix ! must stay lexically valid (${status}):\n${errors}")
endif()
file(WRITE "${old_not}" "main() { if not false {} }\r\n")
execute_process(COMMAND "${BRIEF}" dump-ast "${old_not}"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors)
string(FIND "${output}" "(not (bool false))" found)
if(NOT status EQUAL 0 OR NOT errors STREQUAL "" OR found LESS 0)
    message(FATAL_ERROR "corrected logical-not source failed (${status}):\n${errors}\n${output}")
endif()

# dbl is the binary64 keyword; dub is lexed only for precise parser fixes.
set(binary64 "${TEST_DIR}/binary64.brf")
file(WRITE "${binary64}" "dbl dub dblx dubx\n")
execute_process(COMMAND "${BRIEF}" dump-tokens "${binary64}"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors)
set(expected "1:1 TOK_DBL \"dbl\"\n1:5 TOK_OLD_DUB \"dub\"\n1:9 TOK_LOWER_NAME \"dblx\"\n1:14 TOK_LOWER_NAME \"dubx\"\n1:18 TOK_NEWLINE \"\\n\"\n2:1 TOK_EOF \"\"\n")
if(NOT status EQUAL 0 OR NOT errors STREQUAL "" OR NOT output STREQUAL expected)
    message(FATAL_ERROR "incorrect dbl/recovery tokens (${status}):\n${errors}\n${output}")
endif()
file(WRITE "${binary64}" "main() { let n = dub(1) }\r\n")
string(REPEAT " " 17 indent)
set(expected "error: Double precision is written 'dbl', not 'dub'.\n  --> ${binary64}:1:18\n  |\n1 | main() { let n = dub(1) }\n  | ${indent}^~~\nhelp: write `main() { let n = dbl(1) }`\n")
foreach(command IN ITEMS dump-ast emit-c)
    execute_process(COMMAND "${BRIEF}" "${command}" "${binary64}"
        RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors)
    if(NOT status EQUAL 1 OR NOT output STREQUAL "" OR NOT errors STREQUAL expected)
        message(FATAL_ERROR "incorrect ${command} dub fix (${status}):\n${errors}\nexpected:\n${expected}")
    endif()
endforeach()
file(WRITE "${binary64}" "main() { let n = dbl(1) }\r\n")
execute_process(COMMAND "${BRIEF}" emit-c "${binary64}"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors)
if(NOT status EQUAL 0 OR NOT errors STREQUAL "" OR output STREQUAL "")
    message(FATAL_ERROR "corrected dbl source failed (${status}):\n${errors}")
endif()

# Return annotations use arrows; parameter/binding colons are unchanged.
set(old_return "${TEST_DIR}/old-return.brf")
file(WRITE "${old_return}" "main(): () { let n: int = 1 }\r\n")
string(REPEAT " " 6 indent)
set(expected "error: Return types follow '->', not ':'.\n  --> ${old_return}:1:7\n  |\n1 | main(): () { let n: int = 1 }\n  | ${indent}^\nhelp: write `main() -> () { let n: int = 1 }`\n")
foreach(command IN ITEMS dump-ast emit-c)
    execute_process(COMMAND "${BRIEF}" "${command}" "${old_return}"
        RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors)
    if(NOT status EQUAL 1 OR NOT output STREQUAL "" OR NOT errors STREQUAL expected)
        message(FATAL_ERROR "incorrect ${command} return-arrow fix (${status}):\n${errors}\nexpected:\n${expected}")
    endif()
endforeach()
file(WRITE "${old_return}" "main() -> () { let n: int = 1 }\r\n")
execute_process(COMMAND "${BRIEF}" emit-c "${old_return}"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors)
if(NOT status EQUAL 0 OR NOT errors STREQUAL "" OR output STREQUAL "")
    message(FATAL_ERROR "corrected return arrow failed (${status}):\n${errors}")
endif()

# The retired type word is an ordinary name; only its old declaration form errors.
set(alias "${TEST_DIR}/alias.brf")
file(WRITE "${alias}" "type A = :a\r\nmain() {}\r\n")
set(expected "error: Declare an alias as 'Name = type', without 'type'.\n  --> ${alias}:1:1\n  |\n1 | type A = :a\n  | ^~~~\nhelp: write `A = :a`\n")
foreach(command IN ITEMS dump-ast emit-c)
    execute_process(COMMAND "${BRIEF}" "${command}" "${alias}"
        RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors)
    if(NOT status EQUAL 1 OR NOT output STREQUAL "" OR NOT errors STREQUAL expected)
        message(FATAL_ERROR "incorrect ${command} alias fix (${status}):\n${errors}")
    endif()
endforeach()
file(WRITE "${alias}" "A = :a\r\nmain() {}\r\n")
execute_process(COMMAND "${BRIEF}" emit-c "${alias}"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors)
if(NOT status EQUAL 0 OR NOT errors STREQUAL "" OR output STREQUAL "")
    message(FATAL_ERROR "corrected alias failed (${status}):\n${errors}")
endif()

# Literal fields use '=', while field declarations retain ':'.
set(literal "${TEST_DIR}/literal.brf")
file(WRITE "${literal}" "P { x: int }\r\nmain() { P { x:1 } }\r\n")
string(REPEAT " " 14 indent)
set(expected "error: Struct literal fields use '=', not ':'.\n  --> ${literal}:2:15\n  |\n2 | main() { P { x:1 } }\n  | ${indent}^\nhelp: write `main() { P { x = 1 } }`\n")
foreach(command IN ITEMS dump-ast emit-c)
    execute_process(COMMAND "${BRIEF}" "${command}" "${literal}"
        RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors)
    if(NOT status EQUAL 1 OR NOT output STREQUAL "" OR NOT errors STREQUAL expected)
        message(FATAL_ERROR "incorrect ${command} literal fix (${status}):\n${errors}\nexpected:\n${expected}")
    endif()
endforeach()
file(WRITE "${literal}" "P { x: int }\r\nmain() { P { x = 1 } }\r\n")
execute_process(COMMAND "${BRIEF}" emit-c "${literal}"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors)
if(NOT status EQUAL 0 OR NOT errors STREQUAL "" OR output STREQUAL "")
    message(FATAL_ERROR "corrected literal failed (${status}):\n${errors}")
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

# A legacy index requires both non-overlapping delimiter edits. Each help line
# previews its own edit; the structured fix group applies them together.
set(old_index "${TEST_DIR}/old-index.brf")
file(WRITE "${old_index}" "main() { 0[0] }\r\n")
set(expected "error: Indexing is written '.(...)', not '[...]'.\n  --> ${old_index}:1:11\n  |\n1 | main() { 0[0] }\n  |           ^\nhelp: write `main() { 0.(0) }`\n")
foreach(command IN ITEMS dump-ast emit-c)
    execute_process(COMMAND "${BRIEF}" "${command}" "${old_index}"
        RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors)
    if(NOT status EQUAL 1 OR NOT output STREQUAL "" OR NOT errors STREQUAL expected)
        message(FATAL_ERROR "incorrect ${command} index fixes (${status}):\n${errors}")
    endif()
endforeach()
file(WRITE "${old_index}" "main() { 0.(0) }\r\n")
execute_process(COMMAND "${BRIEF}" dump-ast "${old_index}"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors)
if(NOT status EQUAL 0 OR NOT errors STREQUAL "" OR
   NOT output STREQUAL "(program (fn main (params) _ (block (index (int 0) (int 0)))))\n")
    message(FATAL_ERROR "corrected index failed to parse (${status}):\n${errors}")
endif()
execute_process(COMMAND "${BRIEF}" emit-c "${old_index}"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors)
if(NOT status EQUAL 1 OR NOT output STREQUAL "" OR
   NOT errors MATCHES "error: This expression is not implemented yet\\.")
    message(FATAL_ERROR "corrected index lost its scope rejection (${status}):\n${errors}")
endif()

set(old_arguments "${TEST_DIR}/old-arguments.brf")
file(WRITE "${old_arguments}" "main() { load<int>() }\r\n")
set(expected "error: Type arguments use '[...]', not '<...>'.\n  --> ${old_arguments}:1:14\n  |\n1 | main() { load<int>() }\n  |              ^\nhelp: write `main() { load[int]() }`\n")
foreach(command IN ITEMS dump-ast emit-c)
    execute_process(COMMAND "${BRIEF}" "${command}" "${old_arguments}"
        RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors)
    if(NOT status EQUAL 1 OR NOT output STREQUAL "" OR NOT errors STREQUAL expected)
        message(FATAL_ERROR "incorrect ${command} type argument fixes (${status}):\n${errors}")
    endif()
endforeach()

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

# Retiring the keyword does not turn an old trait declaration into a struct.
set(old_trait "${TEST_DIR}/old-trait.brf")
file(WRITE "${old_trait}" "trait HasUser {}\r\nmain() {}\r\n")
set(expected "error: Traits are declared without 'trait'; trait declarations are not implemented yet.\n  --> ${old_trait}:1:1\n  |\n1 | trait HasUser {}\n  | ^~~~~\nhelp: write `HasUser {}`\n")
foreach(command IN ITEMS dump-ast emit-c)
    execute_process(COMMAND "${BRIEF}" "${command}" "${old_trait}"
        RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors)
    if(NOT status EQUAL 1 OR NOT output STREQUAL "" OR NOT errors STREQUAL expected)
        message(FATAL_ERROR "incorrect ${command} old trait rejection (${status}):\n${errors}")
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
