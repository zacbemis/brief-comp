# M6 syntax-document acceptance. See doc-syntax.md for scope/adapters.
cmake_minimum_required(VERSION 3.16)
include("${CMAKE_CURRENT_LIST_DIR}/doc_examples.cmake")
doc_examples_begin(lang-syntax 33)

set(user_api [=[
User { name: str
  age: int
  active: bool
  banned: bool }
impl User {
  new(name: str, age: int): Sym<:ok(User), :invalidAge(int)> {
    if age < 0 { ret :invalidAge(age) }
    User { name: name + "", age: age, active: true, banned: false }
  }
  getName(user: User): str { user.name }
  isActive(user: User): bool { user.active }
  isBanned(user: User): bool { user.banned }
}
]=])
# These helpers preserve each original expression and comment while adding an
# observation. Count guards make comment semicolons/new fragment boundaries fail
# explicitly rather than silently changing a CMake list's interpretation.
function(source_lines index expected_count variable)
    string(STRIP "${block_${index}}" text)
    string(REPLACE "\n" ";" lines "${text}")
    list(LENGTH lines count)
    if(NOT count EQUAL expected_count)
        message(FATAL_ERROR "Update lang-syntax fence ${index}'s ${expected_count} line adapter")
    endif()
    set(${variable} "${lines}" PARENT_SCOPE)
endfunction()
function(expression_parts line)
    string(FIND "${line}" "//" at)
    if(at LESS 0)
        set(expr "${line}")
        set(comment "")
    else()
        string(SUBSTRING "${line}" 0 ${at} expr)
        string(SUBSTRING "${line}" ${at} -1 comment)
    endif()
    string(STRIP "${expr}" expr)
    set(expr "${expr}" PARENT_SCOPE)
    set(comment "${comment}" PARENT_SCOPE)
endfunction()
function(expression_case index name count prefix expectations)
    source_lines(${index} ${count} lines)
    set(code "")
    foreach(line IN LISTS lines)
        expression_parts("${line}")
        string(APPEND code "print(str(${expr})) ${comment}\n")
    endforeach()
    example(${index} "${name}" "${prefix}" "${code}" "}\n${expectations}")
endfunction()
function(two_alternatives index)
    string(STRIP "${block_${index}}" text)
    replace_once(text "\n\n" "\n\n")
    string(FIND "${text}" "\n\n" at)
    string(SUBSTRING "${text}" 0 ${at} first)
    math(EXPR at "${at} + 2")
    string(SUBSTRING "${text}" ${at} -1 second)
    set(first "${first}" PARENT_SCOPE)
    set(second "${second}" PARENT_SCOPE)
endfunction()

example(1 comments "main() {" "${block_1}" "print(\"after comments\")\n}\n// expect: after comments")
example(2 newline-statements "${user_api}\nmain() {" "${block_2}"
    "print(name)\n}\n// expect: Bob")
set(code "${block_3}")
# Preserve all other continuations; do not replace the lambda with an invented API.
replace_once(code [=[
let name = users
  .find((user) => user.getAge() > 30)!
  .getName()

]=] "")
set(continuation_api [=[
Json { age: int }
parse(text: str): Sym<:ok(Json), :syntax> {
  print("parse")
  if text == "bad" { ret :syntax }
  Json { age: if text == "young" { 17 } else { 20 } }
}
validate(json: Json): Sym<:ok(bool), :invalidAge(int)> {
  print("validate")
  if json.age < 18 { ret :invalidAge(json.age) }
  true
}
]=])
example(3 continuations "${user_api}\n${continuation_api}\nwork(json: str) {\nlet price = 2\nlet tax = 3\nlet shipping = 4"
    "${code}" [=[
print(str(total) + ":" + str(valid) + ":" + user.getName())
}
observe(text: str) { work(text) |> {
  :ok => print("done")
  :syntax => print("syntax")
  :invalidAge(age) => print(str(age))
} }
main() { observe("ok")
  observe("bad")
  observe("young") }
// expect: parse
// expect: validate
// expect: 9:true:Bob
// expect: done
// expect: parse
// expect: syntax
// expect: parse
// expect: validate
// expect: 17
]=])
set(code "${block_4}")
replace_once(code "const MAX_USERS = 100\n" "")
example(4 binding-kinds "${user_api}\nmain() {" "${code}" [=[
user.age = 21
counter++
print(str(user.age) + ":" + str(counter))
}
// expect: 21:1
]=])
two_alternatives(5)
error_at(first "user = User.new(\"Ann\"" "Cannot reassign an immutable binding.")
example(5 immutable-let "${user_api}\nmain() {" "${first}" "}")
example(5 mutable-var "main() {" "${second}" "print(str(counter))\n}\n// expect: 1")
set(code "${block_6}")
replace_once(code "const MAX_USERS: int = 100\n" "")
replace_once(code "let users: Vec<User> = Vec<User>()\n" "")
example(6 annotated-bindings "main() {" "${code}" [=[
ratio /= 3.0
print(str(count + 4294967296))
print(str(ratio))
}
// expect: 4294967296
// expect: 0.16666667
]=])
example(7 inferred-bindings "${user_api}\nmain() {" "${block_7}"
    "print(str(count))\nprint(name)\nprint(user.getName())\n}\n// expect: 0\n// expect: Bob\n// expect: Bob")
set(code "${block_8}")
error_at(code "let count:" "Binding value does not match its annotation.")
example(8 annotation-mismatch "main() {" "${code}" "}")
expression_case(9 arithmetic 6 "main() { let x = 9\nlet y = 2"
    "// expect: 11\n// expect: 7\n// expect: 18\n// expect: 4\n// expect: 1\n// expect: 81")
source_lines(10 7 lines)
set(code "")
foreach(line IN LISTS lines)
    string(APPEND code "${line}\nprint(str(x) + \":\" + str(y))\n")
endforeach()
example(10 updates "main() { var x = 10\nvar y = 3" "${code}" [=[
}
// expect: 11:3
// expect: 11:2
// expect: 13:2
// expect: 11:2
// expect: 22:2
// expect: 11:2
// expect: 1:2
]=])
expression_case(11 comparisons 6 "main() { let x = 9\nlet y = 2"
    "// expect: false\n// expect: true\n// expect: false\n// expect: false\n// expect: true\n// expect: true")
expression_case(12 bitwise 6 "main() { let x = 10\nlet y = 3"
    "// expect: 2\n// expect: 11\n// expect: 9\n// expect: -11\n// expect: 80\n// expect: 1")
example(13 logical-conditions "${user_api}\nallowed(age: int, user: User): bool {" "${block_13}" [=[
true
}
main() { let user = User.new("Bob", 20)!
  print(str(allowed(20, user)))
  user.active = false
  print(str(allowed(10, user)))
  user.active = true
  user.banned = true
  print(str(allowed(10, user)))
  user.banned = false
  print(str(allowed(10, user))) }
// expect: true
// expect: false
// expect: false
// expect: true
]=])
expression_case(14 arithmetic-precedence 5 "${user_api}\nmain() { let a = 10\nlet b = 3\nlet c = 2\nlet user = User.new(\"Bob\", 20)!"
    "// expect: 16\n// expect: 5\n// expect: 512\n// expect: -4\n// expect: false")
expression_case(15 bitwise-precedence 2 "main() { let flags = 10\nlet MASK = 2\nlet a = 8\nlet b = 4\nlet c = 3\nlet d = 1"
    "// expect: false\n// expect: 13")
source_lines(16 2 lines)
list(GET lines 0 first)
list(GET lines 1 second)
error_at(first "a < b < c" "Comparison operators do not chain; use 'and' or parentheses.")
example(16 chained-comparison "main() { let a = 1\nlet b = 2\nlet c = 3" "${first}" "}")
expression_parts("${second}")
example(16 separate-comparisons "main() { let a = 1\nlet b = 2\nlet c = 3"
    "print(str(${expr})) ${comment}" "}\n// expect: true")
source_lines(17 2 lines)
list(GET lines 0 first)
list(GET lines 1 deferred)
string(FIND "${deferred}" "0..10 |> sum()" at)
if(at LESS 0)
    message(FATAL_ERROR "Update the deferred range-value/collection adapter")
endif()
expression_parts("${first}")
example(17 range-precedence "main() { let count = 2" "for i in ${expr} { ${comment}\nprint(str(i))\n}"
    "}\n// expect: 0\n// expect: 1\n// expect: 2")
set(pipeline_api [=[
double(x: int) { x * 2 }
f(value: bool) { print("f")
  value }
g(value: bool) { print("g")
  value }
]=])
expression_case(18 pipeline-precedence 3 "${pipeline_api}\nmain() { let count = 6\nlet a = true\nlet b = false\nlet x = 2"
    "// expect: true\n// expect: f\n// expect: g\n// expect: false\n// expect: 6")
two_alternatives(19)
error_at(first "age >= 18" "Operator operands must have the same type; numeric conversions are explicit.")
error_at(first "true =>" "Pattern value has the wrong type.")
error_at(first "false =>" "Pattern value has the wrong type.")
example(19 comparison-match-unparenthesized "main() { let age = 20" "${first}" "}")
example(19 comparison-match-parenthesized "label(age: int) {" "${second}" [=[
}
main() { print(label(17))
  print(label(18)) }
// expect: Minor
// expect: Adult
]=])
source_lines(20 2 lines)
list(GET lines 0 first)
list(GET lines 1 second)
error_at(first "x |> double()" "A pipeline stage must be a call to a name or dotted path, or a match block.")
example(20 pipeline-arithmetic-unparenthesized "${pipeline_api}\nmain() { let x = 2" "${first}" "}")
expression_parts("${second}")
example(20 pipeline-arithmetic-parenthesized "${pipeline_api}\nmain() { let x = 2"
    "print(str(${expr})) ${comment}" "}\n// expect: 5")
source_lines(21 2 lines)
list(GET lines 0 first)
list(GET lines 1 second)
expression_parts("${first}")
example(21 logical-precedence "main() { let a = true\nlet b = false\nlet c = false\nlet age = 20\nlet hasId = false"
    "print(str(${expr})) ${comment}\n${second}" "print(str(ok))\n}\n// expect: true\n// expect: false")
example(22 if-branches "label(age: int) {" "${block_22}"
    "}\nmain() { label(17)\nlabel(18)\nlabel(100) }\n// expect: Minor\n// expect: Adult\n// expect: Senior")
two_alternatives(23)
foreach(form IN ITEMS first second)
    example(23 "if-value-${form}" "label(age: int) {" "${${form}}"
        "label\n}\nmain() { print(label(17))\nprint(label(18)) }\n// expect: Minor\n// expect: Adult")
endforeach()
set(code "${block_24}")
error_at(code "let label" "An if without else cannot be used as a value.")
example(24 if-missing-else "main() { let age = 20" "${code}" "}")
example(25 while "main() {" "${block_25}" "print(str(count))\n}\n// expect: 10")
two_alternatives(26)
string(FIND "${second}" "for user in users" at)
if(at LESS 0)
    message(FATAL_ERROR "Update the deferred collection-loop adapter")
endif()
example(26 integer-range "main() {" "${first}" [=[
}
// expect: 0
// expect: 1
// expect: 2
// expect: 3
// expect: 4
// expect: 5
// expect: 6
// expect: 7
// expect: 8
// expect: 9
]=])
set(code "${block_28}")
error_at(code "break" "break and continue are only allowed inside loops.")
example(28 break-outside-loop "" "${code}" "")
set(guest_api [=[
Guest { name: str }
impl Guest { isAllowed(guest: Guest): bool {
  print("method")
  guest.name == "Bob"
} }
isAllowed(guest: Guest): bool {
  print("function")
  guest.name == "Bob"
}
]=])
string(STRIP "${block_31}" alternatives)
string(REPLACE "\n\n" ";" alternatives "${alternatives}")
list(LENGTH alternatives alternative_count)
if(NOT alternative_count EQUAL 3)
    message(FATAL_ERROR "Update the three struct-condition alternatives")
endif()
list(GET alternatives 0 first)
list(GET alternatives 1 second)
list(GET alternatives 2 third)
# The original has two equal diagnostics on one line. The runner expects one
# error directive per source line. A newline before '.' is filtered out, so this
# preserves the token stream and lets both diagnostics be checked independently.
replace_once(first "}.isAllowed()" "}\n.isAllowed()")
error_at(first "if Guest" "Expected a newline between items.")
error_at(first ".isAllowed()" "Expected a newline between items.")
example(31 struct-condition-unparenthesized "${guest_api}\nmain() { let name = \"Bob\"" "${first}" "}")
example(31 struct-condition-parenthesized "${guest_api}\nmain() { let name = \"B\" + \"ob\""
    "${second}\n${third}" "print(\"done\")\n}\n// expect: method\n// expect: function\n// expect: done")
source_lines(32 4 lines)
list(GET lines 0 generic)
list(GET lines 1 nested_generic)
string(FIND "${generic}" "Vec<User>()" at)
string(FIND "${nested_generic}" "Vec<Vec<int>>()" nested_at)
if(at LESS 0 OR nested_at LESS 0)
    message(FATAL_ERROR "Update the deferred generic-call adapters")
endif()
list(GET lines 2 first)
list(GET lines 3 second)
example(32 generic-versus-comparison "observe(a: int, b: int, c: int) {"
    "${first}\n${second}" [=[
print(str(smaller) + ":" + str(inside))
}
main() { observe(1, 2, 1)
  observe(3, 2, 1) }
// expect: true:true
// expect: false:false
]=])
source_lines(33 3 lines)
list(GET lines 0 generic)
string(FIND "${generic}" "check(a < b, c > (d))" at)
if(at LESS 0)
    message(FATAL_ERROR "Update the deferred ambiguous-generic adapter")
endif()
list(GET lines 1 first)
list(GET lines 2 second)
example(33 unambiguous-comparison-arguments
    "check(left: bool, right: bool) { print(str(left) + \":\" + str(right)) }\nmain() { let a = 1\nlet b = 2\nlet c = 3\nlet d = 1"
    "${first}\n${second}" "}\n// expect: true:true\n// expect: true:true")

file(READ "${ROOT}/docs/lang-syntax.txt" document)
string(REGEX REPLACE "[\r\n ]+" " " prose "${document}")
function(require_prose text)
    string(FIND "${prose}" "${text}" at)
    if(at LESS 0)
        message(FATAL_ERROR "Missing syntax-rule anchor: ${text}")
    endif()
endfunction()
function(inline_error name spelling prefix code message)
    string(FIND "${document}" "`${spelling}`" at)
    if(at LESS 0)
        message(FATAL_ERROR "Missing inline syntax example: ${spelling}")
    endif()
    error_at(code "${code}" "${message}")
    example(0 "${name}" "${prefix}" "${code}" "}")
endfunction()
set(update_error "Increment and decrement are only allowed as standalone statements.")
set(assignment_error "Assignment is only allowed as a standalone, non-chaining statement, with a name, field, or index on the left.")
set(update_prefix "f(x: int) {}\nmain() { var x = 0\nvar count = 0")
inline_error(update-initializer "let old = x++" "${update_prefix}" "let old = x++" "${update_error}")
inline_error(update-argument "f(x++)" "${update_prefix}" "f(x++)" "${update_error}")
inline_error(update-condition "while count++ < 3 { ... }" "${update_prefix}" "while count++ < 3 {}" "${update_error}")
set(prefix_update_error "Increment and decrement must be postfix standalone statements (x++ or x--).")
inline_error(prefix-increment "++x" "${update_prefix}" "++x" "${prefix_update_error}")
inline_error(prefix-decrement "--x" "${update_prefix}" "--x" "${prefix_update_error}")
set(assignment_prefix "f(x: int) {}\nmain() { var a = 0\nlet b = 1\nlet c = 2")
inline_error(chained-assignment "a = b = c" "${assignment_prefix}" "a = b = c" "${assignment_error}")
inline_error(assignment-condition "if a = b { }" "${assignment_prefix}" "if a = b { }" "${assignment_error}")
inline_error(assignment-argument "f(a = 1)" "${assignment_prefix}" "f(a = 1)" "${assignment_error}")
inline_error(bare-block-argument "print({ ... })" "main() {" "print({ 1 })"
    "Bare blocks are not allowed as expressions or statements.")
require_prose("`a = b` and `user.age += 1` are statements")
example(0 assignment-targets "${user_api}" [=[
main() { var a = 0
  let b = 2
  let user = User.new("Bob", 20)!
  a = b
  user.age += 1
  user.age++
  user.age--
  print(str(a) + ":" + str(user.age)) }
// expect: 2:21
]=] "")
require_prose("Names are visible after their declaration, not inside their own initializer.")
set(code "let value = value")
error_at(code "let value" "Unknown binding.")
example(0 self-initializer "main() {" "${code}" "}")
require_prose("An initializer can still refer to an outer binding with the same name.")
example(0 outer-initializer "" [=[
main() { let value = 1
  if true { let value = value + 1
    print(str(value)) }
  print(str(value)) }
// expect: 2
// expect: 1
]=] "")
require_prose("A name cannot be declared twice in one lexical scope")
set(code "let value = 2")
error_at(code "let value" "Duplicate binding in this scope.")
example(0 duplicate-binding "main() { let value = 1" "${code}" "}")
require_prose("Function parameters are immutable bindings")
set(code "f(value: int) { value = 2 }")
error_at(code "f(value:" "Cannot reassign an immutable binding.")
example(0 immutable-function-parameter "" "${code}" "main() {}")
require_prose("The iteration binding is immutable and is visible only in the loop body.")
set(code "for i in 0..1 { i++ }")
error_at(code "for i" "Cannot reassign an immutable binding.")
example(0 immutable-iteration-binding "main() {" "${code}" "}")
set(code "print(str(i))")
error_at(code "print(str(i))" "Unknown binding.")
example(0 iteration-binding-scope "main() { for i in 0..1 {}" "${code}" "}")
require_prose("including at the tail of a `: ()` function, branch values are ignored individually.")
example(0 discarded-branches "" [=[
discard(flag: bool): () { if flag { "owned" + " string" } else { 1 } }
main() { discard(true)
  discard(false)
  print("done") }
// expect: done
]=] "")
set(code "let value = if true { 1 } else { false }")
error_at(code "let value" "Value-producing if branches must have compatible types.")
example(0 incompatible-if-values "main() {" "${code}" "}")
require_prose("their results cannot be assigned or passed as arguments.")
set(code "let value = while false {}")
error_at(code "let value" "Loops cannot be used as values.")
example(0 loop-initializer "main() {" "${code}" "}")
set(code "take(for i in 0..1 {})")
error_at(code "take(for" "Loops cannot be used as values.")
example(0 loop-argument "take(value: ()) {}\nmain() {" "${code}" "}")
require_prose("bounds are integers of the same type and are evaluated once, left to right, before the loop starts.")
example(0 range-bounds "" [=[
Counter { calls: int }
low(counter: &Counter): shrt { counter.calls++
  print("low")
  1 }
high(counter: &Counter): shrt { counter.calls++
  print("high")
  3 }
main() { let counter = Counter { calls: 0 }
  for i in low(counter)..high(counter) { print(str(i)) }
  print(str(counter.calls))
  for i in 3..3 { print("unexpected") }
  for i in 4..3 { print("unexpected") }
  let end: ushrt = 65535
  for i in 65534..end { print(str(i)) }
  for i in end..end { print("unexpected") }
}
// expect: low
// expect: high
// expect: 1
// expect: 2
// expect: 2
// expect: 65534
]=] "")
set(code "for i in low..high {}")
error_at(code "for i" "Range bounds must have the same type.")
example(0 range-type-mismatch "main() { let low: int = 0\nlet high: lng = 2" "${code}" "}")
require_prose("`..` and the comparison operators do not chain.")
set(code "for i in 0..1..2 {}")
error_at(code "for i" "Range operators do not chain.")
example(0 chained-range "main() {" "${code}" "}")
set(code "for i in 0.0..\n  1.0 {}")
error_at(code "for i" "Range bounds must be integers.")
error_at(code "1.0 {}" "Range bounds must be integers.")
example(0 range-floating-bounds "main() {" "${code}" "}")
require_prose("`continue` advances to the next value before testing the bound again.")
require_prose("In nested loops they apply to the innermost one.")
example(0 range-jumps "" [=[
main() {
  for row in 0..2 {
    for cell in 0..4 {
      if cell == 0 { continue }
      if cell == 2 { break }
      print(str(row) + ":" + str(cell))
    }
    print("row")
  }
}
// expect: 0:1
// expect: row
// expect: 1:1
// expect: row
]=] "")
require_prose("Both are only allowed inside a loop.")
set(code "continue")
error_at(code "continue" "break and continue are only allowed inside loops.")
example(0 continue-outside-loop "main() {" "${code}" "}")
require_prose("`&&` and `||` are not part of the language.")
foreach(op IN ITEMS "&&" "||")
    if(op STREQUAL "&&")
        set(name c-logical-and)
        set(message "Use 'and' instead of '&&'.")
    else()
        set(name c-logical-or)
        set(message "Use 'or' instead of '||'.")
    endif()
    set(code "true ${op} false")
    error_at(code "true" "${message}")
    example(0 "${name}" "main() {" "${code}" "}")
endforeach()
require_prose("After a keyword that still needs its declaration, condition, iterable, or body")
example(0 keyword-continuations "" [=[
pub
type
Result = Sym<:ok, :bad>
pub
Counter { value:
  int }
impl
Counter { bump(counter: &Counter) { counter.value +=
  1 } }
main() {
  let
  counter =
    Counter { value: 0 }
  var
  total = (
    1 +
    2
  )
  if
  true { counter
    .bump() }
  else
  { print("unexpected") }
  while
  counter.value < 2 { counter.
    bump() }
  for
  i in
  0..2 { total += i }
  print(str(counter.value) + ":" + str(total))
  print(true |>
  {
    true =>
      "yes"
    false =>
      "no"
  })
}
// expect: 2:4
// expect: yes
]=] "")

# Whole collection-only fences are deferred. Partial fences: 3 lambda chain,
# 4 const, 6 const/Vec, 17 range-as-value/sum, 26 collection loop, 32/33 generics.
doc_examples_finish("27;29;30" "" 65)
