# M6 function-document acceptance. See doc-functions.md for scope/adapters.
cmake_minimum_required(VERSION 3.16)
include("${CMAKE_CURRENT_LIST_DIR}/doc_examples.cmake")
doc_examples_begin(lang-functions 27)

set(user_api [=[
User { name: str
  age: int }
impl User { getName(user: User): str { user.name } }
]=])
example(1 add "" "${block_1}" [=[
left() { print("left")
  4 }
right() { print("right")
  2 }
main() { print(str(add(left(), right()))) }
// expect: left
// expect: right
// expect: 6
]=])
example(2 explicit-returns "" "${block_2}" [=[
main() { print(str(validate(-1)))
  print(str(validate(17)))
  print(str(validate(18)))
  print(str(validate(100))) }
// expect: false
// expect: false
// expect: true
// expect: true
]=])

# Fence 9 lists three incomplete alternatives. Complete the first two with the
# same local body; the third's module-qualified sets remain explicitly deferred.
string(STRIP "${block_9}" signatures)
string(REPLACE "\n" ";" signatures "${signatures}")
list(LENGTH signatures signature_count)
if(NOT signature_count EQUAL 3)
    message(FATAL_ERROR "Update the three return-signature alternatives")
endif()
list(GET signatures 0 inferred)
list(GET signatures 1 partial)
list(GET signatures 2 full)
foreach(pair IN ITEMS inferred partial full)
    if(pair STREQUAL "inferred")
        set(anchor "pub loadUser(path: str) {")
    elseif(pair STREQUAL "partial")
        set(anchor "pub loadUser(path: str): Sym<:ok(User), _> {")
    else()
        set(anchor "Sym<:ok(User), File.Error, Json.Error, :invalidAge(int)>")
    endif()
    string(FIND "${${pair}}" "${anchor}" at)
    if(at LESS 0)
        message(FATAL_ERROR "Update the ${pair} return-signature adapter")
    endif()
endforeach()
set(load_body [=[
  if path == "missing" { ret :missing(path) }
  User { name: path, age: 20 }
}
]=])
set(load_observer [=[
observe(path: str) { loadUser(path) |> {
  :ok(user) => print(user.name + ":" + str(user.age))
  :missing(path) => print(path)
} }
main() { observe("Bob")
  observe("missing") }
// expect: Bob:20
// expect: missing
]=])
example(9 inferred-signature "${user_api}" "${inferred}\n${load_body}" "${load_observer}")
example(9 partial-signature "${user_api}" "${partial}\n${load_body}" "${load_observer}")
example(10 inferred-number "" "${block_10}" [=[
main() { let value: int = double(3)
  print(str(value)) }
// expect: 6
]=])
example(11 inferred-data "" "${block_11}" [=[
observe(age: int) { describe(age) |> {
  :minor => print("minor")
  :adult => print("adult")
} }
main() { describe(20)
  observe(17)
  observe(18) }
// expect: minor
// expect: adult
]=])

# Local associated functions with deterministic outcomes, not modules, JSON or
# filesystem support. The document's loadUser bodies remain unchanged.
set(load_api [=[
File {}
Json { text: str }
impl File { read(path: str): Sym<:ok(str), :notFound(str), :permissionDenied(str)> {
  if path == "missing" { ret :notFound(path) }
  if path == "denied" { ret :permissionDenied(path) }
  path + ""
} }
impl Json { parse(text: str): Sym<:ok(Json), :syntax(int)> {
  if text == "syntax" { ret :syntax(7) }
  Json { text: text }
} }
impl User { fromJson(json: Json): Sym<:ok(User), :invalidAge(int)> {
  if json.text == "age" { ret :invalidAge(-1) }
  User { name: json.text, age: 20 }
} }
]=])
set(full_observer [=[
observe(path: str) { loadUser(path) |> {
  :ok(user) => print(user.name + ":" + str(user.age))
  :notFound(path) => print("missing:" + path)
  :permissionDenied(path) => print("denied:" + path)
  :syntax(line) => print("syntax:" + str(line))
  :invalidAge(age) => print("age:" + str(age))
} }
main() { observe("Bob")
  observe("missing")
  observe("denied")
  observe("syntax")
  observe("age") }
// expect: Bob:20
// expect: missing:missing
// expect: denied:denied
// expect: syntax:7
// expect: age:-1
]=])
foreach(index IN ITEMS 12 13)
    example(${index} "load-${index}" "${user_api}\n${load_api}" "${block_${index}}" "${full_observer}")
endforeach()
set(code "${block_14}")
error_at(code "loadUser(path:" "Function body does not match the written return type.")
example(14 wrong-success "${user_api}\nAdmin { name: str\n  age: int }\n${load_api}" "${code}" "main() {}")
set(code "${block_15}")
error_at(code "double(x:" "Function body does not match the written return type.")
example(15 wrong-tail "" "${code}" "main() {}")
set(code "${block_16}")
error_at(code "ret :minor" "Return value does not match the written return type.")
example(16 undeclared-variant "" "${code}" "main() {}")
example(18 assignment-unit "${user_api}" "${block_18}" [=[
main() { let user = User { name: "Bob", age: 1 }
  setAge(user, 20) |> { :ok => print("unit") }
  print(str(user.age)) }
// expect: unit
// expect: 20
]=])
example(19 call-unit "${user_api}" "${block_19}" [=[
main() { let user = User { name: "B" + "ob", age: 20 }
  logName(user) |> { :ok => print("unit") } }
// expect: Bob
// expect: unit
]=])

# A nominal one-slot stand-in, not Vec or a collection implementation. Keeping
# the User requires &, exactly as in both declarations from the document.
set(slot_api [=[
UserSlot { user: User }
UserCollection { userVec: UserSlot }
impl UserSlot { add(slot: &UserSlot, user: &User): str {
  slot.user = user
  "stored:" + user.getName()
} }
]=])
set(slot_main [=[
main() {
  let slot = UserSlot { user: User { name: "old", age: 1 } }
  let collection = UserCollection { userVec: slot }
  let user = User { name: "B" + "ob", age: 20 }
]=])
example(20 inferred-owned-tail "${user_api}\n${slot_api}" "${block_20}"
    "${slot_main}\nprint(add(collection, user))\nprint(slot.user.name)\n}\n// expect: stored:Bob\n// expect: Bob")
example(21 discarded-owned-tail "${user_api}\n${slot_api}" "${block_21}"
    "${slot_main}\nadd(collection, user) |> { :ok => print(\"unit\") }\nprint(slot.user.name)\n}\n// expect: unit\n// expect: Bob")

set(file_api [=[
File { fail: bool }
impl File { close(file: &File): Sym<:ok, :ioError(str)> {
  print("close")
  if file.fail { ret :ioError("close" + " failed") }
  :ok
} }
]=])
string(FIND "${block_22}" "\n\n" split)
if(split LESS 0)
    message(FATAL_ERROR "Update the two closeLog alternatives")
endif()
string(SUBSTRING "${block_22}" 0 ${split} unhandled)
math(EXPR split "${split} + 2")
string(SUBSTRING "${block_22}" ${split} -1 handled)
error_at(unhandled "file.close()" "A call that can fail must be handled with ?, !, or a match.")
example(22 unit-does-not-handle "${file_api}" "${unhandled}" "main() {}")
example(22 unit-handled "${file_api}" "${handled}" [=[
main() { let file = File { fail: false }
  closeLog(file) }
// expect: close
]=])
panic_example(22 unit-panic "// expect: close\n${file_api}\n${handled}\nmain() { let file = File { fail: true }"
    "closeLog(file)" "panic: :ioError(\"close failed\")" "file.close()!")
example(23 ignored-int [=[
Users { size: int }
impl Users { len(users: Users): int { print("len")
  users.size } }
main() { let users = Users { size: 3 }
]=] "${block_23}" "print(\"after\")\n}\n// expect: len\n// expect: after")
set(code "${block_24}")
error_at(code "file.close()" "A call that can fail must be handled with ?, !, or a match.")
example(24 ignored-failure "${file_api}\nmain() { let file = File { fail: false }" "${code}" "}")
set(call_api "${user_api}")
replace_once(call_api "getName(user: User): str { user.name }"
    "getName(user: User): str { print(\"name\")\nuser.name }")
example(25 call-syntax "${call_api}\nsave(user: User) { print(\"save:\" + user.name) }\nmain() { let user = User { name: \"B\" + \"ob\", age: 20 }"
    "${block_25}" "}\n// expect: name\n// expect: save:Bob\n// expect: save:Bob")
example(26 local-summary "${user_api}" "${block_26}" [=[
main() { let user = User { name: "B" + "ob", age: 20 }
  print(summary(user))
  print(user |> summary()) }
// expect: User: Bob
// expect: User: Bob
]=])

# Fence 27's first line is a first-version method error. Its other two lines
# require the Report module; test the same function locally via fence 26.
string(STRIP "${block_27}" calls)
string(REPLACE "\n" ";" calls "${calls}")
list(LENGTH calls call_count)
if(NOT call_count EQUAL 3)
    message(FATAL_ERROR "Update the three method/module call alternatives")
endif()
list(GET calls 0 code)
list(GET calls 1 direct)
list(GET calls 2 piped)
string(FIND "${direct}" "Report.summary(user)" direct_at)
string(FIND "${piped}" "user |> Report.summary()" piped_at)
if(direct_at LESS 0 OR piped_at LESS 0)
    message(FATAL_ERROR "Update the deferred module-call adapters")
endif()
error_at(code "user.summary()" "Unknown method for this type.")
example(27 not-a-method "${user_api}\n${block_26}\nmain() { let user = User { name: \"Bob\", age: 20 }"
    "${code}" "}")

# Selected first-version prose obligations, beyond the chapter's code fences.
file(READ "${ROOT}/docs/lang-functions.txt" document)
string(REGEX REPLACE "[\r\n ]+" " " prose "${document}")
function(require_prose text)
    string(FIND "${prose}" "${text}" at)
    if(at LESS 0)
        message(FATAL_ERROR "Missing function-rule anchor: ${text}")
    endif()
endfunction()
require_prose("Ordinary parameters are immutable bindings")
set(code "${block_1}")
replace_once(code "  x + y" "  x = 0\n  x + y")
error_at(code "x = 0" "Cannot reassign an immutable binding.")
example(1 immutable-parameter "" "${code}" "main() {}")
require_prose("A `&` parameter permits changing the borrowed data, not replacing the parameter binding.")
set(code "${block_18}")
replace_once(code "  user.age = age" "  user = User { name: \"new\", age: 0 }\n  user.age = age")
error_at(code "user = User" "Cannot reassign an immutable binding.")
example(18 immutable-borrow-binding "${user_api}" "${code}" "main() {}")
require_prose("Ordinary struct parameters and directly derived local/field aliases are read-only")
set(code "${block_18}")
replace_once(code "user: &User" "user: User")
error_at(code "user.age = age" "Cannot mutate through an ordinary parameter; use &.")
example(18 readonly-parameter "${user_api}" "${code}" "main() {}")
require_prose("`f() { f() }`")
set(code "f() { f() }")
error_at(code "f() { f() }" "Cannot infer recursive return type; write a return type.")
example(0 unanchored-recursion "" "${code}" "main() {}")
example(0 annotated-recursion "" [=[
f(n: int): int { if n == 0 { ret 1 }
  f(n - 1) }
]=] "main() { print(str(f(3))) }\n// expect: 1")
require_prose("Context is shared across these return paths when typing number literals")
example(0 literal-context "" [=[
f(early: bool) { if early { ret 1 }
  2.5 }
main() { let value: dub = f(true)
  print(str(value))
  print(str(f(false))) }
// expect: 1
// expect: 2.5
]=] "")
require_prose("already-typed numbers are never implicitly converted")
set(code "narrow(): int { wide() }")
error_at(code "narrow(): int" "Function body does not match the written return type.")
example(0 typed-return-mismatch "wide(): lng { 4294967296 }" "${code}" "main() {}")
require_prose("Statements after an unconditional return are still checked, but do not contribute a return type.")
example(0 unreachable-return "" "f() { ret 1\nret false }"
    "main() { print(str(f())) }\n// expect: 1")
set(code "f() { ret 1\nmissing }")
error_at(code "missing }" "Unknown binding.")
example(0 checked-dead-syntax "" "${code}" "main() {}")
require_prose("Operator operands and function arguments are evaluated from left to right.")
require_prose("`and` and `or` short-circuit")
require_prose("Assignment evaluates the target before the right side; compound assignment reads the target's old value before evaluating the right side.")
example(0 evaluation-order "${block_1}" [=[
left() { print("left")
  4 }
right() { print("right")
  2 }
no() { print("no")
  false }
yes() { print("yes")
  true }
Cell { value: int }
target(cell: &Cell) { print("target")
  cell }
rhs(cell: &Cell) { print("rhs")
  cell.value = 10
  2 }
main() { print(str(add(left(), right())))
  print(str(left() + right()))
  no() and yes()
  yes() or no()
  let cell = Cell { value: 1 }
  target(cell).value += rhs(cell)
  print(str(cell.value))
  target(cell).value = rhs(cell)
  print(str(cell.value)) }
// expect: left
// expect: right
// expect: 6
// expect: left
// expect: right
// expect: 6
// expect: no
// expect: yes
// expect: target
// expect: rhs
// expect: 3
// expect: target
// expect: rhs
// expect: 2
]=] "")

# Fn/lambdas/captures, collection iteration and traits are later-version work.
# Fences 9 and 27 have explicit deferred subfragments, recorded in the audit.
doc_examples_finish("3;4;5;6;7;8;17" "" 33)
