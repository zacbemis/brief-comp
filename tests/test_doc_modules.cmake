# M6 modules-document audit: single-file structs/entry, not a module loader.
# See doc-modules.md for every fence, scaffold and deferred privacy/module rule.
cmake_minimum_required(VERSION 3.16)
include("${CMAKE_CURRENT_LIST_DIR}/doc_examples.cmake")
doc_examples_begin(lang-modules 17)
file(READ "${ROOT}/docs/lang-modules.txt" document)
string(REGEX REPLACE "[\r\n ]+" " " prose "${document}")
function(require_prose text)
    string(FIND "${prose}" "${text}" at)
    if(at LESS 0)
        message(FATAL_ERROR "Missing modules-document rule anchor: ${text}")
    endif()
endfunction()
require_prose("Fields are private by default.")
require_prose("Making a type public does not automatically make its fields public.")
require_prose("A struct literal sets each field by name, exactly once; there are no defaults.")
require_prose("Field initializers run in their written order, not declaration order.")
require_prose("There is no implicit receiver: it is an explicit first parameter")
require_prose("including `Self` in their type positions")
require_prose("`Self` can also name the enclosing struct in literals and associated calls.")
require_prose("visibility across files and external `impl` blocks remain later-version work.")
require_prose("A program starts at the zero-argument `main()` function of the file given to the compiler.")
require_prose("Returning a plain value (including `()`) is successful: the value is discarded and the process exits with status 0.")
require_prose("A numeric result is not a process exit status.")
require_prose("Constructors are ordinary associated functions, conventionally named `new`.")
require_prose("Fallible construction is represented explicitly through typed return symbols.")
require_prose("Each source file is one module, named after the file.")
require_prose("Code outside the module can use the public `User` type and call its public functions, but cannot directly initialize or modify private fields.")
require_prose("A module is used by writing its name. No `import` is needed.")
require_prose("A misspelled module name is reported as such.")
require_prose("Modules cannot depend on each other in a cycle.")
# Bind the deferred table entries to their actual subjects, not just indices.
set(anchor_5 "// file: Report")
set(anchor_6 "impl Admin: User.HasUser")
set(anchor_7 "models.User.new")
set(anchor_8 "// the project has its own Json.brf")
set(anchor_9 "Usr.new")
set(anchor_10 "import models.User")
set(anchor_11 "import std.Json")
set(anchor_12 "impl Admin: HasUser")
set(anchor_13 "// file: Admin.brf")
foreach(index RANGE 5 13)
    set(fragment "${block_${index}}")
    replace_once(fragment "${anchor_${index}}" "${anchor_${index}}")
endforeach()

example(1 nominal-definition "" "${block_1}" [=[
main() { let user = User { name: "B" + "ob", age: 20, userVer: 0.5 }
  print(user.name + ":" + str(user.age) + ":" + str(user.userVer)) }
// expect: Bob:20:0.5
]=])
string(STRIP "${block_2}" alternatives)
replace_once(alternatives "\n\n" "\n\n")
string(FIND "${alternatives}" "\n\n" split)
string(SUBSTRING "${alternatives}" 0 ${split} comma)
math(EXPR split "${split} + 2")
string(SUBSTRING "${alternatives}" ${split} -1 newline)
foreach(kind IN ITEMS comma newline)
    example(2 "literal-${kind}" "${block_1}\nmain() {" "${${kind}}"
        "print(user.name + \":\" + str(user.age) + \":\" + str(user.userVer))\n}\n// expect: Bob:20:0.5")
endforeach()
# Independent prose scenario, deliberately reversing declaration order.
example(0 initializer-order "${block_1}" [=[
name() { print("name")
  "B" + "ob" }
age(): int { print("age")
  20 }
version(): flt { print("version")
  0.5 }
main() { let user = User { userVer: version(), age: age(), name: name() }
  print(user.name + ":" + str(user.age) + ":" + str(user.userVer)) }
// expect: version
// expect: age
// expect: name
// expect: Bob:20:0.5
]=] "")
foreach(kind IN ITEMS missing duplicate unknown wrong-type)
    set(code "${comma}")
    if(kind STREQUAL "missing")
        replace_once(code "age: 20, " "")
        set(message "Missing struct field initializer.")
    elseif(kind STREQUAL "duplicate")
        replace_once(code "age: 20" "age: 20, age: 21")
        set(message "Duplicate struct field initializer.")
    elseif(kind STREQUAL "unknown")
        replace_once(code "userVer: 0.5" "userVer: 0.5, extra: ()")
        set(message "Unknown struct field.")
    else()
        replace_once(code "name: \"Bob\"" "name: true")
        set(message "Struct field initializer has the wrong type.")
    endif()
    error_at(code "let user" "${message}")
    example(2 "field-${kind}" "${block_1}\nmain() {" "${code}" "}")
endforeach()
require_prose("Return inference includes nominal struct types.")
example(0 nominal-return-inference "${block_1}" [=[
forward() { make() }
make() { User { name: "owned" + " name", age: 20, userVer: 0.5 } }
main() { let user = forward()
  print(user.name + ":" + str(user.age)) }
// expect: owned name:20
]=] "")
set(self_api [=[
impl User {
  make(name: str, age: int): Self {
    Self { name: name + "", age: age, userVer: 0.5 }
  }
  again(name: str, age: int): Self { Self.make(name, age) }
  readName(value: Self): str { value.name }
  rename(value: &Self, name: str): () { value.name = name }
}
]=])
example(0 self-and-receivers "${block_1}\n${self_api}" [=[
main() { let user = User.again("B" + "ob", 20)
  let alias = user
  let old = user.readName()
  user.rename("Su" + "e")
  print(old)
  print(alias.readName())
  print(User.readName(user)) }
// expect: Bob
// expect: Sue
// expect: Sue
]=] "")
set(code "wrong(value: Self): str { name }")
error_at(code "name }" "Unknown binding.")
example(0 no-implicit-field "${block_1}\nimpl User {" "${code}" "}\nmain() {}")
set(code "wrong(value: User): User { new(\"Bob\", 20) }")
error_at(code "new(" "Unknown function.")
example(0 no-implicit-associated-call "${block_3}\nimpl User {" "${code}" "}\nmain() {}")
set(code "wrong(value: Self) { value.age = 21 }")
error_at(code "value.age" "Cannot mutate through an ordinary parameter; use &.")
example(0 ordinary-receiver "${block_1}\nimpl User {" "${code}" "}\nmain() {}")
set(code "wrong(value: &Self) { value = Self.make(\"Bob\", 20) }")
error_at(code "value =" "Cannot reassign an immutable binding.")
example(0 immutable-receiver-binding "${block_1}\n${self_api}\nimpl User {" "${code}" "}\nmain() {}")

# The file label stays a comment. All definitions and callers are in one file;
# no name discovery, publication/privacy or filesystem dependency is asserted.
example(3 local-associated-constructor "" "${block_3}" [=[
main() { let user = User.new("B" + "ob", 20)
  print(user.name + ":" + str(user.age)) }
// expect: Bob:20
]=])
set(code "${block_4}")
replace_once(code "User.VERSION          // constant\n" "")
replace_once(code "User.HasUser          // trait\n" "")
replace_once(code "User.new(\"Bob\", 20)" "let user = User.new(\"Bob\", 20)")
example(4 associated-item-call "${block_3}\nmain() {" "${code}"
    "print(user.name + \":\" + str(user.age))\n}\n// expect: Bob:20")
set(code "${block_14}")
error_at(code "print(\"Hello\")               // Compiler error:" "Expected a parameter name.")
example(14 top-level-statement "" "${code}" "")
set(code "${block_14}")
replace_once(code "print(\"Hello\")               // Compiler error: a statement at the top level\n" "")
example(14 entry-statement "" "${code}" "// expect: Hello")

# Source comment describes expected runtime stderr, not a compile-error fixture.
set(entry_file "${block_15}")
replace_once(entry_file "// error: :notFound(\"data.txt\")"
    "// illustrative entry failure: :notFound(\"data.txt\")")
foreach(kind IN ITEMS success failure)
    if(kind STREQUAL "failure")
        set(fail true)
        set(expected "// stderr: error: :notFound(\"data.txt\")\n// exit: 1")
    else()
        set(fail false)
        set(expected "// expect: contents:data.txt")
    endif()
    set(file_api "File {}\nimpl File { read(path: str): Sym<:ok(str), :notFound(str)> {\nif ${fail} { ret :notFound(path + \"\") }\n\"contents:\" + path\n} }")
    example(15 "entry-file-${kind}" "${file_api}" "${entry_file}" "${expected}")
endforeach()
# Only the original two-field User declaration is shared with the fallible
# constructor; its separate impl from fence 3 is not silently rewritten.
set(user_type "${block_3}")
set(impl_start "\nimpl User {")
replace_once(user_type "${impl_start}" "${impl_start}")
string(FIND "${user_type}" "${impl_start}" split)
string(SUBSTRING "${user_type}" 0 ${split} user_type)
set(fallible_api "${user_type}\nimpl User {\n${block_17}\npub getName(user: User): str { user.name }\n}")
example(16 entry-user "${fallible_api}" "${block_16}" "// expect: Bob")
example(17 validating-constructor "${fallible_api}" [=[
observe(age: int) { User.new("B" + "ob", age) |> {
  :ok(user) => print(user.name + ":" + str(user.age))
  :invalidAge(found) => print("invalid:" + str(found))
} }
main() { observe(17)
  observe(18)
  observe(20) }
// expect: invalid:17
// expect: Bob:18
// expect: Bob:20
]=] "")
foreach(kind IN ITEMS number unit string object data-symbol)
    if(kind STREQUAL "number")
        set(body "23")
    elseif(kind STREQUAL "unit")
        set(body "()")
    elseif(kind STREQUAL "string")
        set(body "\"owned\" + \" entry\"")
    elseif(kind STREQUAL "object")
        set(body "User { name: \"owned\" + \" user\", age: 20, userVer: 0.5 }")
    else()
        set(body ":data")
    endif()
    example(0 "entry-result-${kind}" "${block_1}" "main() { ${body} }" "")
endforeach()
example(0 entry-explicit-ok "" "main(): Sym<:ok, :failed> { :ok }" "")
set(code "helper() {}")
error_at(code "helper()" "A program must define main().")
# Whole-program errors are located at byte zero, on the first fixture line.
example(0 missing-entry "${code}" "" "")
set(code "main(value: int) {}")
error_at(code "main(" "main() cannot have parameters.")
example(0 entry-parameters "" "${code}" "")

# Cross-file privacy/discovery, paths/imports, ambiguity, traits and dependency
# cycles are deferred, not accepted via unrelated unknown-name diagnostics.
doc_examples_finish("5;6;7;8;9;10;11;12;13" "" 30)
