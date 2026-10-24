# M6 traits scope audit. Transplanted ordinary method bodies are not traits,
# inherited defaults, conformance checking, overrides or generic algorithms.
cmake_minimum_required(VERSION 3.16)
include("${CMAKE_CURRENT_LIST_DIR}/doc_examples.cmake")
doc_examples_begin(lang-traits 6)
file(READ "${ROOT}/docs/lang-traits.txt" document)
string(REGEX REPLACE "[\r\n ]+" " " prose "${document}")
foreach(rule IN ITEMS
        "The language does not support traditional class inheritance."
        "Traits may contain both required methods and methods with default implementations."
        "`Self` represents the concrete type implementing the trait."
        "A type implementing `HasUser` only needs to provide the required `getUser()` function to receive the default methods."
        "The `ovr` keyword explicitly replaces a trait's default implementation."
        "Default trait methods calling other trait methods should resolve to the implementing type's applicable implementation, including overrides."
        "Trait-method conflicts and implementation coherence rules still require further specification."
        "Generic types and trait-constrained implementations are planned as the primary mechanism for reusing algorithms across different types."
        "Generics do not imply inheritance or automatically allow unrelated concrete types to be stored interchangeably.")
    string(FIND "${prose}" "${rule}" at)
    if(at LESS 0)
        message(FATAL_ERROR "Missing traits rule/scope anchor: ${rule}")
    endif()
endforeach()
function(method_inventory variable expected)
    string(REGEX MATCHALL "\n  (ovr )?(pub )?[A-Za-z_][A-Za-z0-9_]*\\([^\n]*" methods "${${variable}}")
    set(names "")
    foreach(method IN LISTS methods)
        string(REGEX REPLACE "^\n  (ovr )?(pub )?([A-Za-z_][A-Za-z0-9_]*)\\(.*$" "\\3" name "${method}")
        list(APPEND names "${name}")
    endforeach()
    if(NOT names STREQUAL expected)
        message(FATAL_ERROR "Update complete ${variable} method inventory: ${names}")
    endif()
endfunction()
method_inventory(block_1 "getUser;getName;getAge;getAgeRange")
method_inventory(block_2 "getUser")
method_inventory(block_3 "getUser")
method_inventory(block_5 "getUser;getName")
set(generic "${block_6}")
replace_once(generic "pub Collection<T>" "pub Collection<T>")
replace_once(generic "impl<T: HasUser> Collection<T>" "impl<T: HasUser> Collection<T>")

# The chapter omits User's concrete fields. No constructor, module or trait is
# supplied: these are ordinary local fields and freshly allocated test values.
set(user_type "User { name: str\n  age: int }")
set(user_required "${block_2}")
replace_once(user_required "impl User: HasUser" "impl User")
set(user_defaults "${block_1}")
replace_once(user_defaults "pub trait HasUser" "impl User")
replace_once(user_defaults "  getUser(value: Self): User\n\n" "")
set(admin_required "${block_3}")
replace_once(admin_required "impl Admin: HasUser" "impl Admin")
set(admin_defaults "${user_defaults}")
replace_once(admin_defaults "impl User" "impl Admin")
set(user_api "${user_type}\n${user_required}\n${user_defaults}")
set(api "${user_api}\n${admin_required}\n${admin_defaults}")
set(factory [=[
makeUser(age: int): User { User { name: "B" + "ob", age: age } }
makeAdmin(age: int): Admin { let user = makeUser(age)
  Admin { user: user, tier: 3 } }
]=])
example(1 age-boundaries "${api}\n${factory}" [=[
label(range: Sym<:adult, :senior>): str { range |> {
  :adult => "adult"
  :senior => "senior"
} }
show(age: int) { let user = makeUser(age)
  let admin = Admin { user: user, tier: 3 }
  print(str(age) + ":" + label(user.getAgeRange()) + ":" + label(admin.getAgeRange())) }
main() { show(-2147483648)
  show(-1)
  show(0)
  show(18)
  show(99)
  show(100)
  show(101)
  show(2147483647) }
// expect: -2147483648:adult:adult
// expect: -1:adult:adult
// expect: 0:adult:adult
// expect: 18:adult:adult
// expect: 99:adult:adult
// expect: 100:senior:senior
// expect: 101:senior:senior
// expect: 2147483647:senior:senior
]=] "")
example(2 identity-lifetime "${api}\n${factory}" [=[
identity() { let user = makeUser(20)
  user.getUser() }
main() { let user = identity()
  print(user.name + ":" + str(user.age)) }
// expect: Bob:20
]=] "")
example(3 composition-lifetime "${api}\n${factory}" [=[
project() { let admin = makeAdmin(100)
  admin.getUser() }
main() { let user = project()
  print(user.name + ":" + str(user.age)) }
// expect: Bob:100
]=] "")
set(calls "${block_4}")
string(REGEX MATCHALL "[a-z]+\\.get(Name|Age)\\(\\)" inventory "${calls}")
if(NOT inventory STREQUAL "user.getName();admin.getName();user.getAge();admin.getAge()")
    message(FATAL_ERROR "Update the complete traits call inventory: ${inventory}")
endif()
replace_once(calls "user.getName()" "print(user.getName())")
replace_once(calls "admin.getName()" "print(admin.getName())")
replace_once(calls "user.getAge()" "print(str(user.getAge()))")
replace_once(calls "admin.getAge()" "print(str(admin.getAge()))")
example(4 explicit-local-calls "${api}\n${factory}\nmain() { let user = makeUser(20)\nlet admin = makeAdmin(100)"
    "${calls}" "}\n// expect: Bob\n// expect: Bob\n// expect: 20\n// expect: 100")

# Replace the trait header/ovr modifier only. Avoid a duplicate ordinary method
# by explicitly omitting the default getName when installing the custom body.
set(admin_type "${block_3}")
set(marker "\nimpl Admin: HasUser {")
replace_once(admin_type "${marker}" "${marker}")
string(FIND "${admin_type}" "${marker}" at)
string(SUBSTRING "${admin_type}" 0 ${at} admin_type)
set(custom "${block_5}")
replace_once(custom "impl Admin: HasUser" "impl Admin")
replace_once(custom "ovr pub getName" "pub getName")
set(other_defaults "${admin_defaults}")
replace_once(other_defaults [=[  pub getName(value: Self): str {
    value.getUser().name
  }

]=] "")
set(custom_api "${user_api}\n${admin_type}\n${other_defaults}\n${custom}")
example(5 ordinary-custom-name "${custom_api}\n${factory}" [=[
main() { let user = makeUser(20)
  let admin = Admin { user: user, tier: 3 }
  print(user.getName())
  print(admin.getName())
  print(str(admin.getAge()))
  admin.getAgeRange() |> {
    :adult => print("adult")
    :senior => print("senior")
  }
}
// expect: Bob
// expect: Admin: Bob
// expect: 20
// expect: adult
]=] "")
example(5 custom-name-lifetime "${custom_api}\n${factory}" [=[
name() { let admin = makeAdmin(20)
  admin.getName() }
main() { print(name()) }
// expect: Admin: Bob
]=] "")
example(1 forwarded-name-lifetime "${api}\n${factory}" [=[
name() { let admin = makeAdmin(20)
  admin.getName() }
main() { print(name()) }
// expect: Bob
]=] "")
example(3 shared-user-snapshot "${api}\n${factory}" [=[
rename(user: &User) { user.name = "Su" + "e" }
main() { let user = makeUser(20)
  let admin = Admin { user: user, tier: 3 }
  let old = admin.getName()
  let view = admin.getUser()
  rename(view)
  print(old)
  print(user.getName())
  print(admin.getName())
  print(view.getName())
  print(str(admin.tier)) }
// expect: Bob
// expect: Sue
// expect: Sue
// expect: Sue
// expect: 3
]=] "")
foreach(kind IN ITEMS identity composition)
    if(kind STREQUAL "identity")
        set(source_type User)
        set(index 2)
    else()
        set(source_type Admin)
        set(index 3)
    endif()
    set(code "let view = source.getUser()\nview.age = 21")
    error_at(code "view.age" "Cannot mutate through an ordinary parameter; use &.")
    example(${index} "readonly-${kind}" "${api}\ninspect(source: ${source_type}) {" "${code}" "}\nmain() {}")
endforeach()
set(code "let user: User = admin")
error_at(code "let user" "Binding value does not match its annotation.")
example(0 distinct-nominal-binding "${api}\n${factory}\nmain() { let admin = makeAdmin(20)"
    "${code}" "}")
set(code "onlyUser(admin)")
error_at(code "onlyUser(admin)" "Function argument has the wrong type.")
example(0 distinct-nominal-argument "${api}\n${factory}\nonlyUser(user: User) { print(user.name) }\nmain() { let admin = makeAdmin(20)"
    "${code}" "}")
example(3 explicit-projection "${api}\n${factory}" [=[
onlyUser(user: User) { print(user.name) }
main() { let admin = makeAdmin(20)
  onlyUser(admin.getUser()) }
// expect: Bob
]=] "")
# An ordinary impl with only getUser does not automatically forward User's
# methods. This is not a trait-required-method or conformance diagnostic test.
set(code "admin.getName()")
error_at(code "admin.getName()" "Unknown method for this type.")
example(0 no-implicit-forwarding "${user_api}\n${admin_required}\n${factory}\nmain() { let admin = makeAdmin(20)"
    "${code}" "}")
foreach(kind IN ITEMS adult senior)
    set(code "${user_defaults}")
    if(kind STREQUAL "adult")
        replace_once(code "      < 100 => :adult\n" "")
    else()
        replace_once(code "      >= 100 => :senior\n" "")
    endif()
    error_at(code "value.getAge() |> {" "Match is not exhaustive.")
    example(1 "age-missing-${kind}" "${user_type}\n${user_required}" "${code}" "main() {}")
endforeach()
doc_examples_finish("6" "" 16)
