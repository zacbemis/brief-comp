# M6 symbol-document acceptance. See doc-symbols.md for scope/adapters.
cmake_minimum_required(VERSION 3.16)
include("${CMAKE_CURRENT_LIST_DIR}/doc_examples.cmake")
doc_examples_begin(lang-symbols 10)

set(user_api [=[
User { name: str
  age: int }
impl User {
  new(name: str, age: int): Sym<:ok(User), :invalidAge(int)> {
    if age < 0 { ret :invalidAge(age) }
    User { name: name + "", age: age }
  }
  getName(user: User): str { user.name }
}
]=])
string(STRIP "${block_1}" outcome)
example(1 outcome-type "${user_api}" "type Result = ${outcome}" [=[
make(age: int): Result {
  if age < 0 { ret :invalidAge(age) }
  User { name: "B" + "ob", age: age }
}
observe(age: int) { make(age) |> {
  :ok(user) => print(user.name + ":" + str(user.age))
  :invalidAge(age) => print("invalid:" + str(age))
} }
main() { observe(20)
  observe(-1) }
// expect: Bob:20
// expect: invalid:-1
]=])

# Fence 2 lists two alternatives, not two statements in one function.
string(STRIP "${block_2}" alternatives)
string(REPLACE "\n" ";" alternatives "${alternatives}")
list(LENGTH alternatives alternative_count)
if(NOT alternative_count EQUAL 2)
    message(FATAL_ERROR "Update the two symbol-construction alternatives")
endif()
list(GET alternatives 0 success)
list(GET alternatives 1 failure)
example(2 constructions "${user_api}\ntype Result = ${outcome}"
    "good(user: User): Result { ${success} }\nbad(): Result { ${failure} }" [=[
main() { let user = User { name: "B" + "ob", age: 20 }
  good(user) |> {
    :ok(found) => print(found.name)
    :invalidAge(age) => print(str(age))
  }
  bad() |> {
    :ok(found) => print(found.name)
    :invalidAge(age) => print(str(age))
  }
}
// expect: Bob
// expect: 15
]=])
string(STRIP "${block_3}" optional)
example(3 optional-type "${user_api}" "type Maybe = ${optional}" [=[
find(present: bool): Maybe {
  if present { :found(User { name: "B" + "ob", age: 20 }) }
  else { :missing }
}
observe(present: bool) { find(present) |> {
  :found(user) => print(user.name)
  :missing => print("missing")
} }
main() { find(false)
  observe(true)
  observe(false) }
// expect: Bob
// expect: missing
]=])
string(STRIP "${block_4}" classification)
example(4 classification-type "" "type Age = ${classification}" [=[
classify(age: int): Age {
  if age < 0 { :negative }
  else if age < 65 { :adult }
  else { :senior }
}
observe(age: int) { classify(age) |> {
  :negative => print("negative")
  :adult => print("adult")
  :senior => print("senior")
} }
main() { classify(20)
  observe(-1)
  observe(20)
  observe(65) }
// expect: negative
// expect: adult
// expect: senior
]=])

# Keep declarations at file scope and put the original local statements in main.
# The adjacent prose's ordering claim is deferred; no order is inferred from IDs.
foreach(tier IN ITEMS gold bronze silver)
    set(code "${block_5}")
    replace_once(code "\nlet account = Account { tier: :gold }"
        "\nmain() {\nlet account = Account { tier: :${tier} }")
    if(tier STREQUAL "gold")
        set(label Gold)
    elseif(tier STREQUAL "bronze")
        set(label Bronze)
    else()
        set(label Silver)
    endif()
    example(5 "tier-${tier}" "" "${code}" "}\n// expect: ${label}")
endforeach()

set(load_tail [=[
  print("after")
  user.name + ":" + str(user.age)
}
main() { load() |> {
  :ok(text) => print(text)
  :invalidAge(age) => print("invalid:" + str(age))
} }
]=])
example(6 propagate-success "${user_api}\nload(): Sym<:ok(str), :invalidAge(int)> {"
    "${block_6}" "${load_tail}\n// expect: after\n// expect: Bob:20")
set(code "${block_6}")
replace_once(code "\"Bob\", 20" "\"Bob\", -1")
example(6 propagate-failure "${user_api}\nload(): Sym<:ok(str), :invalidAge(int)> {"
    "${code}" "${load_tail}\n// expect: invalid:-1")
set(code "${block_6}")
error_at(code "User.new(" "Propagated variants are not in the written return type.")
example(6 incompatible-propagation "${user_api}\nload(): Sym<:ok(str), :other> {"
    "${code}" "user.name\n}\nmain() {}")
set(code "${block_6}")
replace_once(code "User.new(\"Bob\", 20)?" "User.new(\"Bob\", 20)")
error_at(code "User.new(" "A call that can fail must be handled with ?, !, or a match.")
example(6 unhandled-call "${user_api}\nmain() {" "${code}" "}")
example(7 panic-success "${user_api}\nmain() {" "${block_7}"
    "print(user.name + \":\" + str(user.age))\n}\n// expect: Bob:20")
set(code "${block_7}")
replace_once(code "\"Bob\", 20" "\"Bob\", -1")
panic_example(7 panic-failure "${user_api}\nmain() {" "${code}\nprint(\"not reached\")"
    "panic: :invalidAge(-1)" "User.new(\"Bob\", -1)!")
example(8 match-success "${user_api}\nmain() {" "${block_8}" "}\n// expect: Bob")
set(code "${block_8}")
replace_once(code "\"Bob\", 20" "\"Bob\", -1")
example(8 match-failure "${user_api}\nmain() {" "${code}"
    "}\n// expect: Not a valid age: -1")
set(code "${block_8}")
replace_once(code "  :invalidAge(age) => print(\"Not a valid age: \" + str(age))\n" "")
error_at(code "User.new(" "Match is not exhaustive.")
example(8 nonexhaustive "${user_api}\nmain() {" "${code}" "}")
set(code "${block_8}")
replace_once(code "\n}" "\n  :invalidAge(_) => ()\n}")
error_at(code ":invalidAge(_) =>" "Unreachable match arm.")
example(8 unreachable "${user_api}\nmain() {" "${code}" "}")

example(9 error-alias "" "${block_9}" [=[
make(n: int): Error {
  if n == 0 { :syntax(7) }
  else if n == 1 { :missingKey("na" + "me") }
  else { :wrongType("a" + "ge") }
}
observe(n: int) { make(n) |> {
  :syntax(line) => print("syntax:" + str(line))
  :missingKey(key) => print("missing:" + key)
  :wrongType(key) => print("wrong:" + key)
} }
main() { make(0)
  observe(0)
  observe(1)
  observe(2) }
// expect: syntax:7
// expect: missing:name
// expect: wrong:age
]=])

# First-version prose rules. Fence 10 itself stays deferred: Json.Error is a
# module-qualified type, not the local alias below. No module/JSON API is claimed.
file(READ "${ROOT}/docs/lang-symbols.txt" document)
string(REGEX REPLACE "[\r\n ]+" " " prose "${document}")
function(require_prose text)
    string(FIND "${prose}" "${text}" at)
    if(at LESS 0)
        message(FATAL_ERROR "Missing symbol-rule anchor: ${text}")
    endif()
endfunction()
require_prose("An entry that is a type name includes every variant of that group.")
example(0 local-composition "${user_api}\n${block_9}" [=[
make(n: int): Sym<:ok(User), Error, :invalidAge(int)> {
  if n == 0 { ret :syntax(7) }
  if n == 1 { ret :missingKey("na" + "me") }
  if n == 2 { ret :wrongType("a" + "ge") }
  if n == 3 { ret :invalidAge(-1) }
  User { name: "B" + "ob", age: 20 }
}
observe(n: int) { make(n) |> {
  :ok(user) => print(user.name)
  Error => print("group")
  :invalidAge(age) => print(str(age))
} }
main() { observe(0)
  observe(1)
  observe(2)
  observe(3)
  observe(4) }
// expect: group
// expect: group
// expect: group
// expect: -1
// expect: Bob
]=] "")
require_prose("Named sets are transparent aliases and may refer forward to other sets or structs.")
require_prose("Recursive value shapes require a struct indirection rather than a direct cycle through aliases and payload types.")
example(0 forward-recursive-shape "" [=[
type All = Sym<Later, :other>
type Later = Sym<:value(Node), :none>
Node { next: All }
main() {
  let leaf = Node { next: :none }
  let root = Node { next: :value(leaf) }
  let same: Sym<:other, :none, :value(Node)> = root.next
  same |> {
    :value(node) => node.next |> {
      :value(_) => print("unexpected")
      :none => print("leaf")
      :other => print("unexpected")
    }
    :none => print("unexpected")
    :other => print("unexpected")
  }
}
// expect: leaf
]=] "")
foreach(kind IN ITEMS alias payload)
    if(kind STREQUAL "alias")
        set(code "type A = Sym<A>")
    else()
        set(code "type A = Sym<:next(A)>")
    endif()
    error_at(code "type A" "Recursive named types require a struct indirection.")
    example(0 "cycle-${kind}" "" "${code}" "main() {}")
endforeach()
require_prose("Disjoint sets may use the same tag with different payload types, but their union is invalid.")
set(disjoint [=[
type A = Sym<:value(int)>
type B = Sym<:value(str)>
]=])
example(0 disjoint-payloads "${disjoint}" [=[
main() { let a: A = :value(7)
  let b: B = :value("h" + "i")
  a |> { :value(n) => print(str(n)) }
  b |> { :value(text) => print(text) }
}
// expect: 7
// expect: hi
]=] "")
set(code "type Both = Sym<A, B>")
error_at(code "type Both" "Symbol tag has conflicting payload types.")
example(0 conflicting-payloads "${disjoint}" "${code}" "main() {}")
require_prose("an absent payload and a `()` payload are different.")
set(shapes [=[
type Absent = Sym<:empty>
type Unit = Sym<:empty(())>
]=])
example(0 absent-and-unit "${shapes}" [=[
mark() { print("payload") }
main() {
  let a: Absent = :empty
  let b: Unit = :empty(())
  a |> { :empty => print("absent") }
  b |> { :empty(_) => print("unit") }
  let c: Unit = :empty(mark())
  c |> { :empty(_) => print("unit") }
}
// expect: absent
// expect: unit
// expect: payload
// expect: unit
]=] "")
foreach(kind IN ITEMS absent unit)
    if(kind STREQUAL "absent")
        set(code "let value: Absent = :empty(())")
    else()
        set(code "let value: Unit = :empty")
    endif()
    error_at(code "let value" "Binding value does not match its annotation.")
    example(0 "wrong-shape-${kind}" "${shapes}\nmain() {" "${code}" "}")
endforeach()
set(code "type Both = Sym<Absent, Unit>")
error_at(code "type Both" "Symbol tag has conflicting payload types.")
example(0 conflicting-shapes "${shapes}" "${code}" "main() {}")
require_prose("A tag's payload type must match exactly wherever sets are combined")
set(code "let value: Sym<:number(lng)> = :number(n)")
error_at(code "let value" "Binding value does not match its annotation.")
example(0 typed-payload-mismatch "main() { let n: int = 1" "${code}" "}")
require_prose("Payload expressions receive the variant's contextual type")
example(0 numeric-context "" [=[
main() { let value: Sym<:number(lng)> = :number(4294967296)
  value |> { :number(n) => print(str(n)) }
}
// expect: 4294967296
]=] "")
require_prose("which can widen a nested symbol payload.")
set(nested [=[
type Inner = Sym<:yes(str), :no>
type Outer = Sym<:wrap(Inner)>
]=])
example(0 nested-context "${nested}" [=[
make(b: bool): Outer {
  if b { :wrap(:yes("h" + "i")) }
  else { :wrap(:no) }
}
observe(b: bool) { make(b) |> { :wrap(inner) => inner |> {
  :yes(text) => print(text)
  :no => print("no")
} } }
main() { observe(true)
  observe(false)
  let inner = :no
  let outer: Outer = :wrap(inner)
  outer |> { :wrap(value) => value |> {
    :yes(text) => print(text)
    :no => print("widened")
  } }
}
// expect: hi
// expect: no
// expect: widened
]=] "")
require_prose("Inferred nested payload types do not themselves grow covariantly")
set(code "make(b: bool) { if b { :wrap(:yes(\"hi\")) } else { :wrap(:no) } }")
error_at(code "make(b:" "Symbol tag has conflicting payload types.")
example(0 inferred-nested-mismatch "" "${code}" "main() {}")

# Module-qualified type composition, unlike a local named-set inclusion, is
# deferred. Payload-free symbol ordering is also deferred prose, not a code fence;
# anchor it so a changed rule revisits the deferral in doc-symbols.md.
require_prose("Tags without a payload sort in the order they are listed in their type")
doc_examples_finish("10" "" 32)
