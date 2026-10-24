# M6 overview acceptance: prose contracts, not invented fenced examples.
cmake_minimum_required(VERSION 3.16)
include("${CMAKE_CURRENT_LIST_DIR}/doc_examples.cmake")
doc_examples_begin(lang-desc 0)
file(READ "${ROOT}/docs/lang-desc.txt" document)

# A scope change must prompt a review of this audit, not silently redefine M6.
set(scope_contract [=[
The first version of the compiler handles a single source file and this part of the language:

- `main()` as the program's entry point.
- `bool`, `chr`, `str`, and every integer and floating-point type.
- `let` and `var`, with optional type annotations, and literals that take their type from context.
- Arithmetic with overflow and division-by-zero panics, comparisons, `and`, `or`, and `!`.
- Functions with typed parameters and inferred, partly written, or fully written return types.
- `if` as a value, `while`, `for` over ranges, `break`, and `continue`.
- Structs, `impl` blocks, struct literals, method calls with `.`, and `&` parameters.
- `Sym` with named sets, `?`, `!`, `|>` match blocks, exhaustiveness checking, error accumulation, and the rule that a call that can fail is handled where it is made.
- `|>` calls.
- `print`, the `str(...)` converter, and the numeric converters such as `int(...)` and `dub(...)`.
- Reference counting for structs and strings, without the cycle collector.

These come in later versions: `const`, lambdas and `Fn`, `Vec` and the collection functions, named tuples, maps, traits, generics written by the programmer, modules and finding them by name, and the cycle collector.
]=])
replace_once(document "${scope_contract}" "${scope_contract}")
string(REGEX REPLACE "[\r\n ]+" " " prose "${document}")
function(require_prose text)
    string(FIND "${prose}" "${text}" at)
    if(at LESS 0)
        message(FATAL_ERROR "Missing overview-rule anchor: ${text}")
    endif()
endfunction()
function(section first last variable)
    replace_once(document "${first}" "${first}")
    replace_once(document "${last}" "${last}")
    string(FIND "${document}" "${first}" start)
    string(FIND "${document}" "${last}" end)
    if(NOT start LESS end)
        message(FATAL_ERROR "Overview section boundaries changed: ${first}")
    endif()
    math(EXPR size "${end} - ${start}")
    string(SUBSTRING "${document}" ${start} ${size} text)
    set(${variable} "${text}" PARENT_SCOPE)
endfunction()
section("\n## 3. Documents\n" "\n## 4. Compiler Responsibilities\n" inventory)
string(REGEX MATCHALL "\\| `lang-[a-z-]+\\.txt` \\|" links "${inventory}")
list(LENGTH links link_count)
if(NOT link_count EQUAL 13)
    message(FATAL_ERROR "Overview document inventory changed; update the scope audit")
endif()
set(seen "")
foreach(link IN LISTS links)
    string(REGEX REPLACE "^\\| `(lang-[a-z-]+\\.txt)` \\|$" "\\1" filename "${link}")
    if(filename IN_LIST seen OR NOT EXISTS "${ROOT}/docs/${filename}" OR IS_DIRECTORY "${ROOT}/docs/${filename}")
        message(FATAL_ERROR "Duplicate or missing overview document: ${filename}")
    endif()
    list(APPEND seen "${filename}")
endforeach()
section("\n## 5. Features Still Under Consideration\n" "\n## 6. Guidance for Coding Agents\n" questions)
string(REGEX MATCHALL "\n[0-9]+\\. " entries "${questions}")
list(LENGTH entries question_count)
if(NOT question_count EQUAL 11)
    message(FATAL_ERROR "Overview open-question inventory changed; update the scope audit")
endif()
require_prose("These are design questions rather than commitments to specific implementations.")
require_prose("examples should be treated as design references rather than proof of an implemented feature.")

# Representative scenarios for the guarded prose. There are no source programs
# in this overview to extract/adapt, and these are not new language examples.
example(0 primitives-and-control "" [=[
twice(n: int) { n * 2 }
factorial(n: int): int { if n == 0 { ret 1 }
  n * factorial(n - 1) }
main() {
  let s: shrt = -32768
  let us: ushrt = 65535
  let i: int = -2147483648
  let ui: uint = 4294967295
  let l: lng = 4294967296
  let ul: ulng = 18446744073709551615
  let f: flt = 0.5
  let d: dub = 0.25
  let b: bool = true
  let c: chr = 'a'
  print(str(s))
  print(str(us))
  print(str(i))
  print(str(ui))
  print(str(l + 1))
  print(str(ul))
  print(str(dub(f) + d))
  print(str(b and !false and (c == 'a' or false)))
  var total = 0
  for n in 0..5 {
    if n == 1 { continue }
    if n == 4 { break }
    total += n
  }
  var repeats = 0
  while repeats < 2 { total += 1
    repeats++ }
  print(str(if b { total } else { -1 }))
  let inferred = 3
  print(str(inferred |> twice()))
  print(str(factorial(3)))
  print(str(int('a')))
}
// expect: -32768
// expect: 65535
// expect: -2147483648
// expect: 4294967295
// expect: 4294967297
// expect: 18446744073709551615
// expect: 0.75
// expect: true
// expect: 7
// expect: 6
// expect: 6
// expect: 97
]=] "")
set(box_api [=[
Box { name: str
  count: int }
Holder { value: Box }
impl Box {
  new(name: str): Box { Box { name: name + "", count: 0 } }
  name(box: Box): str { box.name }
  rename(box: &Box, name: str): () {
    box.name = name
    box.count += 1
  }
}
]=])
require_prose("Potentially expensive object copies must be explicit.")
example(0 aliases-and-ownership "${box_api}" [=[
make() { Box.new("old" + ":name") }
main() {
  let box = make()
  let alias = box
  let snapshot = box.name()
  alias.rename("new" + ":name")
  print(snapshot)
  print(box.name() + ":" + str(box.count))
  print(str(box == alias))
}
// expect: old:name
// expect: new:name:1
// expect: true
]=] "")
set(outcome_api [=[
type Rejected = Sym<:bad(str)>
type Result = Sym<:ok(int), Rejected>
source(n: int): Result {
  if n < 0 { ret :bad("bad:" + str(n)) }
  n
}
double(n: int): Sym<:ok(int), _> {
  let value = n |> source()?
  value * 2
}
]=])
example(0 outcomes-and-inference "${outcome_api}" [=[
observe(n: int) { double(n) |> {
  :ok(value) => print(str(value))
  Rejected => print("rejected")
} }
main() { observe(3)
  observe(-1)
  print(str(source(2)!)) }
// expect: 6
// expect: rejected
// expect: 2
]=] "")
example(0 entry-owned-result "${box_api}" "main() { Box.new(\"owned\" + \" result\") }" "")
example(0 entry-failure "${outcome_api}" "main() { source(-1)?\n0 }"
    "// stderr: error: :bad(\"bad:-1\")\n// exit: 1")
panic_example(0 arithmetic-overflow "main() { var n: int = 2147483647"
    "n += 1" "integer overflow" "n += 1")
panic_example(0 arithmetic-zero "main() { let zero = 0"
    "1 / zero" "division by zero" "1 / zero")

require_prose("The compiler should detect as many mistakes as reasonably possible")
set(code "helper() {}")
error_at(code "helper()" "A program must define main().")
# Missing main is a whole-program diagnostic anchored at byte zero.
example(0 missing-entry "${code}" "" "")
set(code "main(n: int) {}")
error_at(code "main(" "main() cannot have parameters.")
example(0 entry-parameters "" "${code}" "")
set(code "let value: int = true")
error_at(code "let value" "Binding value does not match its annotation.")
example(0 binding-type "main() {" "${code}" "}")
set(code "twice(true)")
error_at(code "twice(" "Function argument has the wrong type.")
example(0 parameter-type "twice(n: int) { n * 2 }\nmain() {" "${code}" "}")
set(code "let n = 0\nn = 1")
error_at(code "n = 1" "Cannot reassign an immutable binding.")
example(0 immutable-binding "main() {" "${code}" "}")
set(code "read(box: Box) { box.count = 1 }")
error_at(code "box.count = 1" "Cannot mutate through an ordinary parameter; use &.")
example(0 ordinary-access "${box_api}" "${code}" "main() {}")
set(code "read(box: &Box) { box = Box.new(\"replacement\") }")
error_at(code "box = Box.new" "Cannot reassign an immutable binding.")
example(0 mutable-parameter-binding "${box_api}" "${code}" "main() {}")
set(code "read(box: Box) { box.rename(\"replacement\") }")
error_at(code "box.rename" "A read-only value cannot be passed to an & parameter; take the parameter it comes from as &.")
example(0 mutable-forwarding "${box_api}" "${code}" "main() {}")
set(code "save(holder: &Holder, box: Box) { holder.value = box }")
error_at(code "holder.value = box" "A read-only value cannot be stored in a field; take the parameter it comes from as &.")
example(0 retained-access "${box_api}" "${code}" "main() {}")
require_prose("Safe object initialization.")
set(code "Box { name: \"missing\" }")
error_at(code "Box {" "Missing struct field initializer.")
example(0 incomplete-object "${box_api}\nmain() {" "${code}" "}")
require_prose("Pattern-match exhaustiveness.")
set(code [=[
source(1) |> { :ok(value) => print(str(value)) }
]=])
error_at(code "source(1)" "Match is not exhaustive.")
example(0 incomplete-match "${outcome_api}\nmain() {" "${code}" "}")
require_prose("Do not allow fallible operations to be silently discarded.")
set(code "source(1)")
error_at(code "source(1)" "A call that can fail must be handled with ?, !, or a match.")
example(0 unhandled-outcome "${outcome_api}\nmain() {" "${code}" "}")
require_prose("Invalid error propagation.")
set(code "restricted(n: int): int { source(n)? }")
error_at(code "source(n)?" "Propagated variants are not in the written return type.")
example(0 propagation-contract "${outcome_api}" "${code}" "main() {}")
require_prose("Validation of implicit function returns.")
set(code "wrong(): int { true }")
error_at(code "wrong()" "Function body does not match the written return type.")
example(0 return-contract "" "${code}" "main() {}")
require_prose("Typed symbol compatibility.")
set(code "let value: Result = :extra")
error_at(code "let value" "Binding value does not match its annotation.")
example(0 symbol-compatibility "${outcome_api}\nmain() {" "${code}" "}")
set(code "break")
error_at(code "break" "break and continue are only allowed inside loops.")
example(0 jump-context "main() {" "${code}" "}")
set(code "let value = if true { 1 }")
error_at(code "let value" "An if without else cannot be used as a value.")
example(0 conditional-value "main() {" "${code}" "}")

doc_examples_finish("" "" 24)
