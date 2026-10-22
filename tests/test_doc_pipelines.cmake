# First M6 acceptance slice. See doc-pipelines.md for scope and adapters.
cmake_minimum_required(VERSION 3.16)
include("${CMAKE_CURRENT_LIST_DIR}/doc_examples.cmake")
doc_examples_begin(lang-pipelines 17)
string(STRIP "${block_1}" operator)
if(NOT operator STREQUAL "|>")
    message(FATAL_ERROR "Update the pipeline-operator syntax-reference classification")
endif()

set(text_api [=[
input() { print("input")
  " HEL" + "LO " }
trim(text: str) { print("trim")
  if text == " HELLO " { "HELLO" } else { text } }
lower(text: str) { print("lower")
  if text == "HELLO" { "hello" } else { text } }
]=])
foreach(index IN ITEMS 2 3)
    example(${index} "text-${index}" "${text_api}\nevaluate() { let text = input()"
        "${block_${index}}" [=[
}
main() { print(evaluate()) }
// expect: input
// expect: trim
// expect: lower
// expect: hello
]=])
endforeach()

# Deterministic scalar stand-ins, not a JSON parser or validation library.
set(outcome_api [=[
parse(json: str): Sym<:ok(int), :syntax(str)> {
  print("parse")
  if json == "syntax" { ret :syntax(json) }
  if json == "invalid" { -1 } else { 2 }
}
validate(n: int): Sym<:ok(int), :invalidAge(int)> {
  print("validate")
  if n < 0 { ret :invalidAge(n) }
  n + 10
}
]=])
set(outcome_observer [=[
observe(json: str) {
  load(json) |> {
    :ok(n) => print("ok:" + str(n))
    :syntax(message) => print("syntax:" + message)
    :invalidAge(n) => print("invalid:" + str(n))
  }
}
main() { observe("ok")
  observe("syntax")
  observe("invalid") }
// expect: parse
// expect: validate
// expect: ok:12
// expect: parse
// expect: syntax:syntax
// expect: parse
// expect: validate
// expect: invalid:-1
]=])
foreach(index IN ITEMS 7 8)
    example(${index} "propagation-${index}" "${outcome_api}\nload(json: str) {"
        "${block_${index}}" "}\n${outcome_observer}")
endforeach()
set(code "${block_7}")
replace_once(code "validate()?" "validate()")
error_at(code "json |>" "A call that can fail must be handled with ?, !, or a match.")
example(7 unhandled-stage "${outcome_api}\nload(json: str) {" "${code}" "}\nmain() {}")

# The range-value/sum line is later-version collection API notation. Keep all
# three in-scope expressions intact; the entire fence still has parser coverage.
set(code "${block_11}")
replace_once(code "0..10 |> sum()                 // (0..10) |> sum()\n" "")
set(precedence_api [=[
double(n: int) { print("double:" + str(n))
  n * 2 }
f(a: bool) { print("f")
  a }
g(b: bool) { print("g")
  b }
]=])
example(11 precedence "${precedence_api}\ncheck(x: int, count: int, a: bool, b: bool) {"
    "${code}" [=[
}
main() { check(2, 6, false, true)
  check(3, 4, true, true) }
// expect: double:3
// expect: double:6
// expect: f
// expect: double:4
// expect: double:4
// expect: f
// expect: g
]=])
example(12 comparison-input "label(age: int) {" "${block_12}" [=[
}
main() { print(label(-1))
  print(label(17))
  print(label(18))
  print(label(100)) }
// expect: Minor
// expect: Minor
// expect: Adult
// expect: Adult
]=])
example(13 whole-value-binding "transform() {" "${block_13}"
    "}\nmain() { print(str(transform())) }\n// expect: 20")
set(age_observer [=[
main() { print(label(-2147483648))
  print(label(17))
  print(label(18))
  print(label(99))
  print(label(100))
  print(label(2147483647)) }
// expect: Minor
// expect: Minor
// expect: Adult
// expect: Adult
// expect: Senior
// expect: Senior
]=])
example(14 age-ranges "label(age: int) {" "${block_14}" "}\n${age_observer}")
set(user_api [=[
User { name: str }
impl User { getName(user: User): str { user.name } }
makeUser() { User { name: "B" + "ob" } }
]=])
example(16 payload-arm "${user_api}\nmain() { let name = makeUser() |> {"
    "${block_16}" "}\nprint(name)\n}\n// expect: Bob")

# Fence 17 deliberately juxtaposes four alternatives. Split only their blank
# line boundaries; keep the explanatory comments and original arm text.
string(REPLACE "\n\n" ";" alternatives "${block_17}")
list(LENGTH alternatives alternative_count)
if(NOT alternative_count EQUAL 4)
    message(FATAL_ERROR "Update the four exhaustiveness-example boundaries")
endif()
list(GET alternatives 0 exhaustive)
list(GET alternatives 1 missing)
list(GET alternatives 2 wildcard)
list(GET alternatives 3 unreachable)
example(17 exhaustive "label(age: int) {" "${exhaustive}" "}\n${age_observer}")
example(17 wildcard "label(age: int) {" "${wildcard}" [=[
}
main() { print(label(17))
  print(label(18))
  print(label(99))
  print(label(100)) }
// expect: Other
// expect: Adult
// expect: Adult
// expect: Other
]=])
error_at(missing "age |> {" "Match is not exhaustive.")
error_at(unreachable "5 =>" "Unreachable match arm.")
foreach(mode IN ITEMS value discard)
    set(tail "")
    if(mode STREQUAL "discard")
        set(tail "\n()")
    endif()
    example(17 "missing-${mode}" "label(age: int) {" "${missing}"
        "${tail}\n}\nmain() {}")
    example(17 "unreachable-${mode}" "label(age: int) {" "${unreachable}"
        "${tail}\n}\nmain() {}")
endforeach()

# Inline syntax examples are also executable obligations. Guard their spelling
# in the actual document rather than silently testing an unrelated copied string.
file(READ "${ROOT}/docs/lang-pipelines.txt" document)
function(inline_code variable text)
    string(FIND "${document}" "`${text}`" at)
    if(at LESS 0)
        message(FATAL_ERROR "Missing inline pipeline example: ${text}")
    endif()
    set(${variable} "${text}" PARENT_SCOPE)
endfunction()
set(stage_api [=[
A { n: int }
f(x: A) { A { n: x.n + 1 } }
impl A { g(x: A) { x.n * 2 } }
]=])
set(i 0)
foreach(text IN ITEMS "x |> f() |> g()" "(x |> f()).g()")
    inline_code(code "${text}")
    example(0 "stage-valid-${i}" "${stage_api}\nmain() { let x = A { n: 5 }"
        "print(str(${code}))" "}\n// expect: 12")
    math(EXPR i "${i} + 1")
endforeach()
set(i 0)
foreach(text IN ITEMS "x |> f().g()" "x |> functions[0]()")
    inline_code(code "${text}")
    error_at(code "${text}" "A pipeline stage must be a call to a name or dotted path, or a match block.")
    example(0 "stage-invalid-${i}" "main() {" "${code}" "}")
    math(EXPR i "${i} + 1")
endforeach()

# Six later-version fences, one syntax-only operator reference. Fence 11 is
# explicitly partial: its standalone range-value line remains deferred.
doc_examples_finish("4;5;6;9;10;15" "1" 20)
