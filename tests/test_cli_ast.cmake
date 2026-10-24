file(MAKE_DIRECTORY "${TEST_DIR}")
set(valid "${TEST_DIR}/ast-valid.brf")
file(WRITE "${valid}" "main() {\n  print(\n    \"Hello\"\n  )\n}\n")
execute_process(COMMAND "${BRIEF}" dump-ast "${valid}"
    RESULT_VARIABLE status OUTPUT_VARIABLE tree ERROR_VARIABLE errors)
set(expected "(program (fn main (params) _ (block (call (name print) (str \"Hello\")))))\n")
if(NOT status EQUAL 0 OR NOT errors STREQUAL "" OR NOT tree STREQUAL expected)
    message(FATAL_ERROR "incorrect AST (${status}):\n${errors}\n${tree}\nexpected:\n${expected}")
endif()

set(invalid "${TEST_DIR}/ast-invalid.brf")
file(WRITE "${invalid}" "main() {\n  let = 1\n  bad(1\n}\nnext() { 3 }\n")
execute_process(COMMAND "${BRIEF}" dump-ast "${invalid}"
    RESULT_VARIABLE status OUTPUT_VARIABLE tree ERROR_VARIABLE errors)
string(REGEX MATCHALL "error:" messages "${errors}")
list(LENGTH messages count)
if(NOT status EQUAL 1 OR NOT tree STREQUAL "" OR NOT count EQUAL 2)
    message(FATAL_ERROR "expected two syntax errors and no AST (${status}):\n${errors}\n${tree}")
endif()
foreach(expected IN ITEMS "Expected a binding name." "${invalid}:2:7" "Expected ')' after call arguments." "${invalid}:4:1")
    string(FIND "${errors}" "${expected}" found)
    if(found LESS 0)
        message(FATAL_ERROR "missing diagnostic text ${expected}:\n${errors}")
    endif()
endforeach()

# Lexical errors must not suppress unrelated parser diagnostics, and an
# error-token initializer must not produce a duplicate expression error.
file(WRITE "${invalid}" "main() {\n  @\n  let a = @\n  let = 1\n}\nnext(x) {}\nlast() {}\n")
execute_process(COMMAND "${BRIEF}" dump-ast "${invalid}"
    RESULT_VARIABLE status OUTPUT_VARIABLE tree ERROR_VARIABLE errors)
string(REGEX MATCHALL "error:" messages "${errors}")
list(LENGTH messages count)
if(NOT status EQUAL 1 OR NOT tree STREQUAL "" OR NOT count EQUAL 4)
    message(FATAL_ERROR "expected two lexer and two parser errors (${status}):\n${errors}\n${tree}")
endif()
foreach(expected IN ITEMS "Unexpected character." "Expected a binding name." "Expected ':' before type.")
    string(FIND "${errors}" "${expected}" found)
    if(found LESS 0)
        message(FATAL_ERROR "missing mixed diagnostic ${expected}:\n${errors}")
    endif()
endforeach()
string(FIND "${errors}" "Expected an expression" duplicate)
if(NOT duplicate LESS 0)
    message(FATAL_ERROR "duplicate diagnostic on a lexer error token:\n${errors}")
endif()
# dump-tokens deliberately remains a lexer-only command.
execute_process(COMMAND "${BRIEF}" dump-tokens "${invalid}"
    RESULT_VARIABLE status OUTPUT_VARIABLE tree ERROR_VARIABLE errors)
string(REGEX MATCHALL "error:" messages "${errors}")
list(LENGTH messages count)
if(NOT status EQUAL 1 OR NOT tree STREQUAL "" OR NOT count EQUAL 2)
    message(FATAL_ERROR "dump-tokens should report only the two lexer errors (${status}):\n${errors}")
endif()

# Top-level calls/bindings point at the statement, not a fictitious parameter.
file(WRITE "${invalid}" "print(\"Hello\")\r\n  let value = 1\r\ntick()\r\nmain() {}\r\n")
execute_process(COMMAND "${BRIEF}" dump-ast "${invalid}"
    RESULT_VARIABLE status OUTPUT_VARIABLE tree ERROR_VARIABLE errors)
string(REGEX MATCHALL "error:" messages "${errors}")
list(LENGTH messages count)
string(REGEX MATCHALL "Statements are only allowed inside a function\\." hints "${errors}")
list(LENGTH hints hint_count)
if(NOT status EQUAL 1 OR NOT tree STREQUAL "" OR NOT count EQUAL 3 OR NOT hint_count EQUAL 3)
    message(FATAL_ERROR "expected three top-level statement hints (${status}):\n${errors}\n${tree}")
endif()
foreach(location IN ITEMS "1:1" "2:3" "3:1")
    string(FIND "${errors}" "${invalid}:${location}\n" found)
    if(found LESS 0)
        message(FATAL_ERROR "missing top-level source location ${location}:\n${errors}")
    endif()
endforeach()
execute_process(COMMAND "${BRIEF}" dump-tokens "${invalid}"
    RESULT_VARIABLE status OUTPUT_VARIABLE tokens ERROR_VARIABLE errors)
if(NOT status EQUAL 0 OR NOT errors STREQUAL "" OR tokens STREQUAL "")
    message(FATAL_ERROR "statement syntax must not affect lexer-only dumping (${status}):\n${errors}")
endif()

# Invalid C-style operators remain lexically valid; parsing highlights the
# complete operator and gives the Brief spelling/form, including on CRLF input.
file(WRITE "${invalid}" "main() {\r\n  true && false\r\n  true || false\r\n  ++count\r\n  --count\r\n  let kept = 1\r\n}\r\nafter() {}\r\n")
execute_process(COMMAND "${BRIEF}" dump-ast "${invalid}"
    RESULT_VARIABLE status OUTPUT_VARIABLE tree ERROR_VARIABLE errors)
string(REGEX MATCHALL "error:" messages "${errors}")
list(LENGTH messages count)
string(REGEX MATCHALL "Increment and decrement must be postfix standalone statements" updates "${errors}")
list(LENGTH updates update_count)
if(NOT status EQUAL 1 OR NOT tree STREQUAL "" OR NOT count EQUAL 4 OR NOT update_count EQUAL 2)
    message(FATAL_ERROR "expected four operator hints (${status}):\n${errors}\n${tree}")
endif()
foreach(hint IN ITEMS "Use 'and' instead of '&&'." "Use 'or' instead of '||'.")
    string(FIND "${errors}" "${hint}" found)
    if(found LESS 0)
        message(FATAL_ERROR "missing operator hint ${hint}:\n${errors}")
    endif()
endforeach()
foreach(location IN ITEMS "2:8" "3:8" "4:3" "5:3")
    string(FIND "${errors}" "${invalid}:${location}\n" found)
    if(found LESS 0)
        message(FATAL_ERROR "missing operator source location ${location}:\n${errors}")
    endif()
endforeach()
execute_process(COMMAND "${BRIEF}" dump-tokens "${invalid}"
    RESULT_VARIABLE status OUTPUT_VARIABLE tokens ERROR_VARIABLE errors)
if(NOT status EQUAL 0 OR NOT errors STREQUAL "" OR tokens STREQUAL "")
    message(FATAL_ERROR "operator syntax must not affect lexer-only dumping (${status}):\n${errors}")
endif()

# Named-field literals in control headers require parentheses. Check the
# opening-brace location/highlight exactly, without the old recovery cascade.
set(hint "Wrap struct literals in conditions or iterables in parentheses.")
set(code "main() {\r\n")
set(expected "")
set(line_number 2)
foreach(line IN ITEMS
        "  if Guest { name: name }.isAllowed() {}"
        "  while Guest { name: name }.ok {}"
        "  for i in 0..Limit { n: 2 }.n {}")
    string(APPEND code "${line}\r\n")
    string(FIND "${line}" "{" brace)
    math(EXPR column "${brace} + 1")
    string(REPEAT " " ${brace} indent)
    string(APPEND expected "error: ${hint}\n  --> ${invalid}:${line_number}:${column}\n  |\n${line_number} | ${line}\n  | ${indent}^\n")
    math(EXPR line_number "${line_number} + 1")
endforeach()
string(APPEND code "  let kept = 1\r\n}\r\nafter() {}\r\n")
file(WRITE "${invalid}" "${code}")
foreach(command IN ITEMS dump-ast emit-c)
    execute_process(COMMAND "${BRIEF}" "${command}" "${invalid}"
        RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors)
    if(NOT status EQUAL 1 OR NOT output STREQUAL "" OR NOT errors STREQUAL expected)
        message(FATAL_ERROR "incorrect ${command} struct-header hints (${status}):\n${errors}\nexpected:\n${expected}")
    endif()
endforeach()
execute_process(COMMAND "${BRIEF}" dump-tokens "${invalid}"
    RESULT_VARIABLE status OUTPUT_VARIABLE tokens ERROR_VARIABLE errors)
if(NOT status EQUAL 0 OR NOT errors STREQUAL "" OR tokens STREQUAL "")
    message(FATAL_ERROR "struct-header syntax must remain lexically valid (${status}):\n${errors}")
endif()

# A dotted call stays invalid even when a same-name top-level function could
# take the receiver. Emit one primary error plus an exact declaration note;
# syntax-only commands must remain independent of method lookup.
set(method "${TEST_DIR}/method-invalid.brf")
set(declaration "save(value: P) {}")
file(WRITE "${method}" "P {}\r\n${declaration}\r\nmain() { let p = P {}\r\n  p.save()\r\n}\r\n")
string(LENGTH "${declaration}" length)
math(EXPR tail_length "${length} - 1")
string(REPEAT "~" ${tail_length} tail)
set(expected "error: Unknown method for this type.\n  --> ${method}:4:3\n  |\n4 |   p.save()\n  |   ^~~~~~~~\nnote: This is a top-level function, not a method; use save(value) or value |> save().\n  --> ${method}:2:1\n  |\n2 | ${declaration}\n  | ^${tail}\n")
execute_process(COMMAND "${BRIEF}" emit-c "${method}"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors)
if(NOT status EQUAL 1 OR NOT output STREQUAL "" OR NOT errors STREQUAL expected)
    message(FATAL_ERROR "incorrect top-level-function advice (${status}):\n${errors}\nexpected:\n${expected}")
endif()
foreach(command IN ITEMS dump-tokens dump-ast)
    execute_process(COMMAND "${BRIEF}" "${command}" "${method}"
        RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors)
    if(NOT status EQUAL 0 OR NOT errors STREQUAL "" OR output STREQUAL "")
        message(FATAL_ERROR "${command} must not perform method lookup (${status}):\n${errors}")
    endif()
endforeach()

file(WRITE "${valid}" "")
execute_process(COMMAND "${BRIEF}" dump-ast "${valid}"
    RESULT_VARIABLE status OUTPUT_VARIABLE tree ERROR_VARIABLE errors)
if(NOT status EQUAL 0 OR NOT errors STREQUAL "" OR NOT tree STREQUAL "(program)\n")
    message(FATAL_ERROR "empty-file AST failed (${status}):\n${errors}\n${tree}")
endif()
