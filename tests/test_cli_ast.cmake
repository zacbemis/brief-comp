cmake_minimum_required(VERSION 3.16)

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

# Structured fixes reach the CLI without changing failure status or publishing
# an AST/C output. Replacements also separate unspaced keyword operands.
set(fix_source "${TEST_DIR}/ast-fixes.brf")
file(WRITE "${fix_source}" "main() {\r\n  true&&false\r\n  true || false\r\n}\r\n")
set(expected "error: Use 'and' instead of '&&'.\n  --> ${fix_source}:2:7\n  |\n2 |   true&&false\n  |       ^~\nhelp: write `  true and false`\nerror: Use 'or' instead of '||'.\n  --> ${fix_source}:3:8\n  |\n3 |   true || false\n  |        ^~\nhelp: write `  true or false`\n")
foreach(command IN ITEMS dump-ast emit-c)
    execute_process(COMMAND "${BRIEF}" "${command}" "${fix_source}"
        RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors)
    if(NOT status EQUAL 1 OR NOT output STREQUAL "" OR NOT errors STREQUAL expected)
        message(FATAL_ERROR "incorrect ${command} fix-its (${status}):\n${errors}\nexpected:\n${expected}")
    endif()
endforeach()

# Named-field literals in control headers require parentheses. Check the
# opening-brace location/highlight exactly, without the old recovery cascade.
set(hint "Wrap struct literals in conditions or iterables in parentheses.")
set(code "main() {\r\n")
set(expected "")
set(line_number 2)
foreach(line IN ITEMS
        "  if Guest { name = name }.isAllowed() {}"
        "  while Guest { name = name }.ok {}"
        "  for i in 0..Limit { n = 2 }.n {}")
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
file(WRITE "${method}" "P { unit: () }\r\n${declaration}\r\nmain() { let p = P { unit = () }\r\n  p.save()\r\n}\r\n")
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

# The documented ambiguous spelling is still parsed as a generic call, not
# reinterpreted as comparisons. Unsupported generic headers get no follow-on
# ordinary-call lookup error. The one-argument check scaffold isolates that
# diagnostic; surrounding ordinary arity checks have separate stage coverage.
# Uppercase patterns are set references even when no declaration is found.
set(pattern_name "${TEST_DIR}/pattern-name.brf")
file(WRITE "${pattern_name}" "main() { :a |> { Unknown => () } }\r\n")
execute_process(COMMAND "${BRIEF}" dump-ast "${pattern_name}"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors)
if(NOT status EQUAL 0 OR NOT errors STREQUAL "" OR
   NOT output STREQUAL "(program (fn main (params) _ (block (|> (symbol a) (match (arm (set Unknown) (unit)))))))\n")
    message(FATAL_ERROR "incorrect named-set AST (${status}):\n${errors}\n${output}")
endif()
execute_process(COMMAND "${BRIEF}" emit-c "${pattern_name}"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors)
if(NOT status EQUAL 1 OR NOT output STREQUAL "" OR
   NOT errors MATCHES "^error: Unknown named symbol set\\." OR errors MATCHES "help:")
    message(FATAL_ERROR "unknown set became a binding (${status}):\n${errors}")
endif()

set(imports "${TEST_DIR}/imports.brf")
file(WRITE "${imports}" "import models.User\r\nmain() { User.new() }\r\n")
execute_process(COMMAND "${BRIEF}" dump-ast "${imports}"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors)
if(NOT status EQUAL 0 OR NOT errors STREQUAL "" OR
   NOT output STREQUAL "(program (import models.User) (fn main (params) _ (block (call (field (name User) new)))))\n")
    message(FATAL_ERROR "incorrect import AST (${status}):\n${errors}\n${output}")
endif()
set(expected "error: Imports are not implemented yet.\n  --> ${imports}:1:1\n  |\n1 | import models.User\n  | ^~~~~~~~~~~~~~~~~~\n")
execute_process(COMMAND "${BRIEF}" emit-c "${imports}"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors)
if(NOT status EQUAL 1 OR NOT output STREQUAL "" OR NOT errors STREQUAL expected)
    message(FATAL_ERROR "incorrect import scope rejection (${status}):\n${errors}")
endif()

set(collection "${TEST_DIR}/collection.brf")
file(WRITE "${collection}" "main() { Vec[str] {} }\r\n")
execute_process(COMMAND "${BRIEF}" dump-ast "${collection}"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors)
if(NOT status EQUAL 0 OR NOT errors STREQUAL "" OR
   NOT output STREQUAL "(program (fn main (params) _ (block (collection (name Vec) (types str)))))\n")
    message(FATAL_ERROR "incorrect collection AST (${status}):\n${errors}\n${output}")
endif()
execute_process(COMMAND "${BRIEF}" emit-c "${collection}"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors)
if(NOT status EQUAL 1 OR NOT output STREQUAL "" OR
   NOT errors MATCHES "^error: Collection literals are not implemented yet\\.")
    message(FATAL_ERROR "collection escaped its scope boundary (${status}):\n${errors}")
endif()

set(primitive_member "${TEST_DIR}/primitive-member.brf")
file(WRITE "${primitive_member}" "P { unit: ()\r\npub str(n: int) -> str { str(n) } }\r\nmain() { print(P.str(42)) }\r\n")
execute_process(COMMAND "${BRIEF}" dump-tokens "${primitive_member}"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors)
if(NOT status EQUAL 0 OR NOT errors STREQUAL "" OR
   NOT output MATCHES "TOK_STR \"str\"")
    message(FATAL_ERROR "member str changed its keyword token (${status}):\n${errors}")
endif()
execute_process(COMMAND "${BRIEF}" dump-ast "${primitive_member}"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors)
string(FIND "${output}" "(fn pub str" member_at)
string(FIND "${output}" "(field (name P) str)" call_at)
if(NOT status EQUAL 0 OR NOT errors STREQUAL "" OR member_at LESS 0 OR call_at LESS 0)
    message(FATAL_ERROR "primitive member syntax rejected (${status}):\n${errors}\n${output}")
endif()
execute_process(COMMAND "${BRIEF}" emit-c "${primitive_member}"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors)
if(NOT status EQUAL 0 OR NOT errors STREQUAL "" OR NOT output MATCHES "brm_1_P_str")
    message(FATAL_ERROR "primitive member lost owned C name (${status}):\n${errors}")
endif()
file(WRITE "${primitive_member}" "main() { Json.str(1) }\r\n")
execute_process(COMMAND "${BRIEF}" emit-c "${primitive_member}"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors)
if(NOT status EQUAL 1 OR NOT output STREQUAL "" OR NOT errors MATCHES "^error: Unknown binding")
    message(FATAL_ERROR "JSON spelling invented an API (${status}):\n${errors}")
endif()

set(receiver "${TEST_DIR}/self-receiver.brf")
file(WRITE "${receiver}" "Self = int\r\nP { n: Self\r\nread(& /* keep */ self, n: int) -> int { self.n += n\r\nself.n }\r\n}\r\nmain() { let p = P { n = 40 }\r\nprint(str(p.read(2))) }\r\n")
execute_process(COMMAND "${BRIEF}" dump-tokens "${receiver}"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors)
if(NOT status EQUAL 0 OR NOT errors STREQUAL "" OR
   NOT output MATCHES "TOK_SELF \"self\"" OR
   NOT output MATCHES "TOK_UPPER_NAME \"Self\"")
    message(FATAL_ERROR "self/Self token roles changed (${status}):\n${errors}\n${output}")
endif()
execute_process(COMMAND "${BRIEF}" dump-ast "${receiver}"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors)
string(FIND "${output}" "(params (&self) (n int))" receiver_at)
string(FIND "${output}" "(field (self) n)" self_at)
if(NOT status EQUAL 0 OR NOT errors STREQUAL "" OR receiver_at LESS 0 OR self_at LESS 0)
    message(FATAL_ERROR "receiver syntax rejected or fabricated (${status}):\n${errors}\n${output}")
endif()
execute_process(COMMAND "${BRIEF}" emit-c "${receiver}"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors)
if(NOT status EQUAL 0 OR NOT errors STREQUAL "" OR NOT output MATCHES "brm_1_P_read")
    message(FATAL_ERROR "receiver binding failed native emission (${status}):\n${errors}")
endif()
execute_process(COMMAND "${BRIEF}" run --debug "${receiver}"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors)
if(NOT status EQUAL 0 OR NOT output STREQUAL "42\n" OR NOT errors STREQUAL "")
    message(FATAL_ERROR "receiver native execution failed (${status}):\n${errors}\n${output}")
endif()

set(wrong_call "${TEST_DIR}/wrong-call.brf")
file(WRITE "${wrong_call}" "Date { n: int\r\nlabel() -> int { self.n } }\r\nmain() { let d = Date { n = 42 }\r\nDate.label(d) }\r\n")
set(wrong_call_output "${TEST_DIR}/wrong-call-output")
file(REMOVE "${wrong_call_output}")
foreach(mode IN ITEMS emit-c run build)
    set(arguments "${wrong_call}")
    if(mode STREQUAL "build")
        list(APPEND arguments --debug -o "${wrong_call_output}")
    endif()
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env "CC=${TEST_DIR}/must-not-run"
        "${BRIEF}" ${mode} ${arguments}
        RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors)
    string(FIND "${errors}" "help: write `d.label() }`" fix_at)
    if(NOT status EQUAL 1 OR NOT output STREQUAL "" OR EXISTS "${wrong_call_output}" OR
       NOT errors MATCHES "^error: Methods are called on a value, not the type" OR fix_at LESS 0)
        message(FATAL_ERROR "wrong-way call escaped fix/rejection (${status}):\n${errors}")
    endif()
endforeach()
file(WRITE "${wrong_call}" "Date { n: int\r\nlabel() -> int { self.n } }\r\nmain() { let d = Date { n = 42 }\r\nprint(str(d.label())) }\r\n")
execute_process(COMMAND "${BRIEF}" run --debug "${wrong_call}"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors)
if(NOT status EQUAL 0 OR NOT output STREQUAL "42\n" OR NOT errors STREQUAL "")
    message(FATAL_ERROR "fixed method call failed (${status}):\n${errors}")
endif()

set(conformance "${TEST_DIR}/conformance.brf")
file(WRITE "${conformance}" "A: models.Query[Missing] and Printable {\r\nfield: Unknown\r\nf() { missing() }\r\n}\r\nAlias = Cycle\r\nCycle = Alias\r\n")
execute_process(COMMAND "${BRIEF}" dump-ast "${conformance}"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors)
string(FIND "${output}" "(conforms (apply models.Query Missing) Printable)" header_at)
if(NOT status EQUAL 0 OR NOT errors STREQUAL "" OR header_at LESS 0)
    message(FATAL_ERROR "conformance header syntax rejected (${status}):\n${errors}\n${output}")
endif()
set(conformance_output "${TEST_DIR}/conformance-output")
file(REMOVE "${conformance_output}")
foreach(mode IN ITEMS emit-c run build)
    set(arguments "${conformance}")
    if(mode STREQUAL "build")
        list(APPEND arguments --debug -o "${conformance_output}")
    endif()
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env "CC=${TEST_DIR}/must-not-run"
        "${BRIEF}" ${mode} ${arguments}
        RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors)
    string(REGEX MATCHALL "(^|\n)error:" scope_errors "${errors}")
    list(LENGTH scope_errors scope_errors)
    if(NOT status EQUAL 1 OR NOT output STREQUAL "" OR
       NOT scope_errors EQUAL 1 OR EXISTS "${conformance_output}" OR
       NOT errors MATCHES "^error: Trait conformance is not implemented yet" OR
       errors MATCHES "Unknown|Recursive|main|help:")
        message(FATAL_ERROR "conformance escaped its native boundary (${status}):\n${errors}")
    endif()
endforeach()

set(old_impl "${TEST_DIR}/old-impl.brf")
file(WRITE "${old_impl}" "User { n: int }\r\nimpl User { read(u: User) { u.n } }\r\nmain() {}\r\n")
execute_process(COMMAND "${BRIEF}" dump-tokens "${old_impl}"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors)
if(NOT status EQUAL 0 OR NOT errors STREQUAL "" OR
   NOT output MATCHES "TOK_LOWER_NAME \"impl\"" OR output MATCHES "TOK_IMPL")
    message(FATAL_ERROR "impl remains a keyword (${status}):\n${errors}\n${output}")
endif()
foreach(mode IN ITEMS dump-ast emit-c)
    execute_process(COMMAND "${BRIEF}" ${mode} "${old_impl}"
        RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors)
    if(NOT status EQUAL 1 OR NOT output STREQUAL "" OR
       NOT errors MATCHES "^error: Declare functions inside the struct, without 'impl'" OR
       errors MATCHES "help:")
        message(FATAL_ERROR "old impl escaped rejection (${status}):\n${errors}")
    endif()
endforeach()

set(qualified "${TEST_DIR}/qualified-symbol.brf")
file(WRITE "${qualified}" "A = models.Db.:!bad[Missing]\r\nmain() {}\r\n")
execute_process(COMMAND "${BRIEF}" dump-ast "${qualified}"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors)
string(FIND "${output}" "(models.Db.:!bad Missing)" qualified_at)
if(NOT status EQUAL 0 OR NOT errors STREQUAL "" OR qualified_at LESS 0)
    message(FATAL_ERROR "qualified tag lost its module (${status}):\n${errors}\n${output}")
endif()
execute_process(COMMAND "${BRIEF}" emit-c "${qualified}"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors)
string(REGEX MATCHALL "(^|\n)error:" qualified_errors "${errors}")
list(LENGTH qualified_errors qualified_errors)
if(NOT status EQUAL 1 OR NOT output STREQUAL "" OR NOT qualified_errors EQUAL 1 OR
   NOT errors MATCHES "^error: Module-qualified symbols are not implemented yet")
    message(FATAL_ERROR "qualified tag escaped its scope (${status}):\n${errors}")
endif()
file(WRITE "${qualified}" "main() { Db.:none }\r\n")
execute_process(COMMAND "${BRIEF}" dump-ast "${qualified}"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors)
if(NOT status EQUAL 1 OR NOT output STREQUAL "" OR
   NOT errors MATCHES "^error: ':ok' and ':none' cannot have a module qualifier")
    message(FATAL_ERROR "shared tag accepted a module (${status}):\n${errors}")
endif()

set(empty_trait "${TEST_DIR}/empty-trait.brf")
file(WRITE "${empty_trait}" "Empty { /* keep */ }\r\nAlias = Empty\r\nf(n: Empty) { missing(n) }\r\n")
execute_process(COMMAND "${BRIEF}" dump-ast "${empty_trait}"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors)
if(NOT status EQUAL 0 OR NOT errors STREQUAL "" OR NOT output MATCHES "[(]trait Empty[)]")
    message(FATAL_ERROR "empty declaration was not a trait (${status}):\n${errors}\n${output}")
endif()
execute_process(COMMAND "${BRIEF}" emit-c "${empty_trait}"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors)
string(REGEX MATCHALL "(^|\n)error:" trait_errors "${errors}")
list(LENGTH trait_errors trait_errors)
if(NOT status EQUAL 1 OR NOT output STREQUAL "" OR NOT trait_errors EQUAL 1 OR
   NOT errors MATCHES "^error: A struct needs at least one field; traits are not implemented yet" OR
   errors MATCHES "Unknown|must define main|help:")
    message(FATAL_ERROR "trait escaped its scope or cascaded (${status}):\n${errors}")
endif()
file(WRITE "${empty_trait}" "pub Empty {}\r\n")
execute_process(COMMAND "${BRIEF}" dump-ast "${empty_trait}"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors)
string(FIND "${errors}" "help: write `Empty {}`" trait_fix)
if(NOT status EQUAL 1 OR NOT output STREQUAL "" OR
   NOT errors MATCHES "^error: Trait types cannot be marked 'pub'" OR trait_fix LESS 0)
    message(FATAL_ERROR "trait visibility did not reject/fix (${status}):\n${errors}")
endif()

set(native_alias "${TEST_DIR}/native-type-alias.brf")
file(WRITE "${native_alias}" "Num = int\r\nRef = Box\r\nBox { n: Num\r\nnew(n: Num) -> Ref { Ref { n = n } } }\r\nmain() { print(str(Ref.new(Num(42)).n)) }\r\n")
execute_process(COMMAND "${BRIEF}" emit-c "${native_alias}"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors)
if(NOT status EQUAL 0 OR NOT errors STREQUAL "" OR
   NOT output MATCHES "brs_Box" OR output MATCHES "brs_Ref")
    message(FATAL_ERROR "native alias lost canonical layout (${status}):\n${errors}")
endif()
file(WRITE "${native_alias}" "A = B\r\nB = A\r\nmain() {}\r\n")
execute_process(COMMAND "${BRIEF}" emit-c "${native_alias}"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors)
if(NOT status EQUAL 1 OR NOT output STREQUAL "" OR
   NOT errors MATCHES "^error: Recursive named types require a struct indirection")
    message(FATAL_ERROR "cyclic alias escaped checking (${status}):\n${errors}")
endif()

set(self_type "${TEST_DIR}/self-type.brf")
file(WRITE "${self_type}" "Box { n: int\r\nread(b: Self) -> int { b.n } }\r\nmain() {}\r\n")
execute_process(COMMAND "${BRIEF}" dump-tokens "${self_type}"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors)
if(NOT status EQUAL 0 OR NOT errors STREQUAL "" OR
   NOT output MATCHES "TOK_UPPER_NAME \"Self\"" OR output MATCHES "TOK_SELF")
    message(FATAL_ERROR "Self is still a keyword (${status}):\n${errors}\n${output}")
endif()
execute_process(COMMAND "${BRIEF}" dump-ast "${self_type}"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors)
if(NOT status EQUAL 0 OR NOT errors STREQUAL "")
    message(FATAL_ERROR "ordinary Self name rejected by parser (${status}):\n${errors}")
endif()
execute_process(COMMAND "${BRIEF}" emit-c "${self_type}"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors)
string(REGEX MATCHALL "(^|\n)error:" self_errors "${errors}")
list(LENGTH self_errors self_errors)
string(FIND "${errors}" "help: write `read(b: Box)" self_fix)
if(NOT status EQUAL 1 OR NOT output STREQUAL "" OR NOT self_errors EQUAL 1 OR
   NOT errors MATCHES "^error: There is no implicit 'Self' type" OR
   self_fix LESS 0 OR
   errors MATCHES "Unknown|not exhaustive")
    message(FATAL_ERROR "old Self escaped its diagnostic (${status}):\n${errors}")
endif()

set(typed_pattern "${TEST_DIR}/typed-payload-pattern.brf")
file(WRITE "${typed_pattern}" "main() { :data[1] |> { :data[value: Missing] => unknown(value) } }\r\n")
execute_process(COMMAND "${BRIEF}" dump-ast "${typed_pattern}"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors)
string(FIND "${output}" "(typed-bind value Missing)" typed_at)
if(NOT status EQUAL 0 OR NOT errors STREQUAL "" OR typed_at LESS 0)
    message(FATAL_ERROR "typed payload lost its annotation (${status}):\n${errors}\n${output}")
endif()
execute_process(COMMAND "${BRIEF}" emit-c "${typed_pattern}"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors)
string(REGEX MATCHALL "(^|\n)error:" typed_errors "${errors}")
list(LENGTH typed_errors typed_errors)
if(NOT status EQUAL 1 OR NOT output STREQUAL "" OR NOT typed_errors EQUAL 1 OR
   NOT errors MATCHES "^error: Typed symbol payload patterns are not implemented yet" OR
   errors MATCHES "Unknown|not exhaustive|help:")
    message(FATAL_ERROR "typed payload escaped scope or cascaded (${status}):\n${errors}")
endif()
file(WRITE "${typed_pattern}" "main() { value |> { whole: int => whole } }\r\n")
execute_process(COMMAND "${BRIEF}" dump-ast "${typed_pattern}"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors)
if(NOT status EQUAL 1 OR NOT output STREQUAL "" OR
   NOT errors MATCHES "^error: Typed bindings are only allowed in symbol payload patterns")
    message(FATAL_ERROR "whole-value type pattern accepted (${status}):\n${errors}")
endif()

set(kind_conflict "${TEST_DIR}/kind-conflict.brf")
file(WRITE "${kind_conflict}" "A = :bad\r\nB = :!bad\r\nmain() {}\r\n")
execute_process(COMMAND "${BRIEF}" dump-ast "${kind_conflict}"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors)
if(NOT status EQUAL 0 OR NOT errors STREQUAL "" OR NOT output MATCHES "\\(:!bad\\)")
    message(FATAL_ERROR "error kind missing from AST (${status}):\n${errors}\n${output}")
endif()
execute_process(COMMAND "${BRIEF}" emit-c "${kind_conflict}"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors)
if(NOT status EQUAL 1 OR NOT output STREQUAL "" OR
   NOT errors MATCHES "^error: A symbol name cannot be both neutral and error in one module" OR
   NOT errors MATCHES "note: The other symbol kind is declared here" OR errors MATCHES "help:")
    message(FATAL_ERROR "incorrect kind conflict (${status}):\n${errors}")
endif()

set(payloads "${TEST_DIR}/payload-lists.brf")
file(WRITE "${payloads}" "E = :many[Missing, Other]\r\nmain() { :many[unknown, absent()]\r\n  1 |> { :many[a, b] => missing(a, b) }\r\n}\r\n")
execute_process(COMMAND "${BRIEF}" dump-ast "${payloads}"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors)
if(NOT status EQUAL 0 OR NOT errors STREQUAL "" OR
   NOT output MATCHES "symbol many" OR NOT output MATCHES "tag many")
    message(FATAL_ERROR "payload lists did not retain syntax (${status}):\n${errors}\n${output}")
endif()
execute_process(COMMAND "${BRIEF}" emit-c "${payloads}"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors)
string(REGEX MATCHALL "(^|\n)error:" payload_errors "${errors}")
list(LENGTH payload_errors payload_errors)
if(NOT status EQUAL 1 OR NOT output STREQUAL "" OR NOT payload_errors EQUAL 3 OR
   NOT errors MATCHES "^error: Multiple symbol payloads are not implemented yet\\." OR
   errors MATCHES "Unknown|require a symbol input|help:")
    message(FATAL_ERROR "multiple payloads escaped scope or cascaded (${status}):\n${errors}")
endif()
file(WRITE "${payloads}" "E = :typed(int)\r\nmain() { :value(1)\r\n  1 |> { :some(x) => x }\r\n}\r\n")
foreach(command dump-ast emit-c)
    execute_process(COMMAND "${BRIEF}" "${command}" "${payloads}"
        RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors)
    string(REGEX MATCHALL "(^|\n)error:" payload_errors "${errors}")
    list(LENGTH payload_errors payload_errors)
    string(FIND "${errors}" "help: write `E = :typed[int]`" type_fix)
    string(FIND "${errors}" ":some[x] => x" pattern_fix)
    if(NOT status EQUAL 1 OR NOT output STREQUAL "" OR NOT payload_errors EQUAL 3 OR
       type_fix LESS 0 OR pattern_fix LESS 0)
        message(FATAL_ERROR "old payloads accepted or lost fixes (${command}, ${status}):\n${errors}")
    endif()
endforeach()

set(old_union "${TEST_DIR}/old-union.brf")
file(WRITE "${old_union}" "E = Sym<:a,:b>\r\nmain() {}\r\n")
foreach(command dump-ast emit-c)
    execute_process(COMMAND "${BRIEF}" "${command}" "${old_union}"
        RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors)
    string(FIND "${errors}" "help: write `E = :a | :b`" fix_at)
    if(NOT status EQUAL 1 OR NOT output STREQUAL "" OR fix_at LESS 0 OR
       NOT errors MATCHES "^error: Symbol unions use")
        message(FATAL_ERROR "old Sym syntax accepted or lost fixes (${command}, ${status}):\n${errors}")
    endif()
endforeach()
execute_process(COMMAND "${BRIEF}" dump-tokens "${old_union}"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors)
if(NOT status EQUAL 0 OR NOT errors STREQUAL "" OR NOT output MATCHES "TOK_UPPER_NAME.*Sym" OR output MATCHES "TOK_SYM")
    message(FATAL_ERROR "Sym is still reserved (${status}):\n${errors}\n${output}")
endif()

set(generic "${TEST_DIR}/generic-invalid.brf")
file(WRITE "${generic}" "check(value: int) {}\r\nmain() { let a = 1\r\n  let d = 2\r\n  check(a[b, c](d))\r\n}\r\n")
set(expected "error: Generic calls are not implemented yet.\n  --> ${generic}:4:9\n  |\n4 |   check(a[b, c](d))\n  |         ^~~~~~~~~~\n")
execute_process(COMMAND "${BRIEF}" emit-c "${generic}"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors)
if(NOT status EQUAL 1 OR NOT output STREQUAL "" OR NOT errors STREQUAL expected)
    message(FATAL_ERROR "incorrect generic-call recovery (${status}):\n${errors}\nexpected:\n${expected}")
endif()
execute_process(COMMAND "${BRIEF}" dump-ast "${generic}"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors)
string(FIND "${output}" "(call (name check) (call (name a) (types b c) (name d)))" generic_at)
if(NOT status EQUAL 0 OR NOT errors STREQUAL "" OR generic_at LESS 0)
    message(FATAL_ERROR "generic-call disambiguation changed (${status}):\n${errors}\n${output}")
endif()
execute_process(COMMAND "${BRIEF}" dump-tokens "${generic}"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors)
if(NOT status EQUAL 0 OR NOT errors STREQUAL "" OR output STREQUAL "")
    message(FATAL_ERROR "generic-call recovery must not affect lexing (${status}):\n${errors}")
endif()

# A recursive error must not erase its own inferred receiver and silently
# admit error-typed functions to lowering. All native paths reject it before
# consulting CC or publishing a binary, even though main does not call it.
set(recursive "${TEST_DIR}/recursive-lookup.brf")
set(blocked "${TEST_DIR}/recursive-lookup-output")
file(REMOVE "${blocked}")
file(WRITE "${recursive}" "P { unit: () }\r\nf(p: P, b: bool) { if b { ret p }\r\n  g(p, b) }\r\ng(p: P, b: bool) {\r\n  f(p, b).missing\r\n}\r\nmain() {}\r\n")
string(REPEAT "~" 14 tail)
set(expected "error: Unknown struct field.\n  --> ${recursive}:5:3\n  |\n5 |   f(p, b).missing\n  |   ^${tail}\n")
foreach(command IN ITEMS emit-c run build)
    set(arguments "${recursive}")
    if(command STREQUAL "build")
        list(APPEND arguments -o "${blocked}")
    endif()
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env "CC=${TEST_DIR}/must-not-run"
        "${BRIEF}" "${command}" ${arguments}
        RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors)
    if(NOT status EQUAL 1 OR NOT output STREQUAL "" OR NOT errors STREQUAL expected OR EXISTS "${blocked}")
        message(FATAL_ERROR "recursive lookup admitted by ${command} (${status}):\n${errors}\nexpected:\n${expected}")
    endif()
endforeach()

file(WRITE "${valid}" "")
execute_process(COMMAND "${BRIEF}" dump-ast "${valid}"
    RESULT_VARIABLE status OUTPUT_VARIABLE tree ERROR_VARIABLE errors)
if(NOT status EQUAL 0 OR NOT errors STREQUAL "" OR NOT tree STREQUAL "(program)\n")
    message(FATAL_ERROR "empty-file AST failed (${status}):\n${errors}\n${tree}")
endif()
