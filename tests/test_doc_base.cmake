# M6 raw running-sample acceptance. See doc-base.md for the full span audit.
cmake_minimum_required(VERSION 3.16)
include("${CMAKE_CURRENT_LIST_DIR}/doc_examples.cmake")
doc_examples_begin(lang-ex-base 0)
file(READ "${ROOT}/docs/lang-ex-base.txt" document)
set_property(GLOBAL PROPERTY base_remaining "${document}")
set_property(GLOBAL PROPERTY base_parts "")
set_property(GLOBAL PROPERTY base_covered "")

# Consume contiguous spans, including all comments/whitespace. No raw source can
# disappear between selected snippets. Every boundary must occur exactly once.
function(part name kind boundary)
    get_property(names GLOBAL PROPERTY base_parts)
    if(name IN_LIST names OR NOT kind MATCHES "^(tested|partial|reference|deferred)$")
        message(FATAL_ERROR "Invalid/duplicate base-sample classification: ${name}")
    endif()
    get_property(text GLOBAL PROPERTY base_remaining)
    if(boundary STREQUAL "EOF")
        set(code "${text}")
        set(text "")
    else()
        replace_once(text "${boundary}" "${boundary}")
        string(FIND "${text}" "${boundary}" at)
        if(NOT at GREATER 0)
            message(FATAL_ERROR "Empty base-sample span: ${name}")
        endif()
        string(SUBSTRING "${text}" 0 ${at} code)
        string(SUBSTRING "${text}" ${at} -1 text)
    endif()
    set(part_${name} "${code}" PARENT_SCOPE)
    set_property(GLOBAL PROPERTY base_remaining "${text}")
    set_property(GLOBAL APPEND PROPERTY base_parts "${name}")
    set_property(GLOBAL PROPERTY base_kind_${name} "${kind}")
endfunction()
part(comments tested "\nbool x\n")
part(types reference "\nint[5]\n")
part(array deferred "\nlet // immutable binding")
part(bindings reference "\nconst // immutable binding")
part(const-keyword deferred "\n// the type after the name")
part(typed tested "\n// a number takes")
part(defaults tested "\n// no implicit conversions")
part(implicit tested "\nlet total = lng(small)")
part(conversions tested "\n// integer overflow panics")
part(overflow reference "\n& // mutable borrow")
part(borrow reference "\nAnimal {")
part(animal tested "\n// there is no enum")
part(color tested "\nadd(x: int,")
part(function tested "\nlet add = (x:")
part(lambda deferred "\nx + y\n")
part(arithmetic tested "\nx++\n")
part(updates tested "\nx and y\n")
part(logic tested "\n// precedence:")
part(precedence tested "\na < b < c // error")
part(chained tested "\nif x < y {")
part(conditional tested "\nwhile x < 10 {")
part(while tested "\nfor i in 0..10 {")
part(range tested "\n// () is the empty type")
part(unit tested "\n// each file is a module")
part(user-module deferred "\npub User {")
part(user partial "\n// file: Admin.brf")
part(admin-module deferred "\npub Admin {")
part(admin partial "\n// file: UserCollection.brf")
part(collection-module deferred "\npub UserCollection {")
part(collection partial "\n// files cannot use each other in a cycle")
part(module-cycle deferred "\n// the top level of a file")
part(entry tested "\n// in a function in another file")
part(constructors partial "\nconst message = user.getAgeRange()")
part(age-message partial "\n// x.f() only works")
part(calls tested "\n// pipe passes the value")
part(pipeline partial "\nlet user = User.new(\"Bob\", 20)!\n\n// Shares")
part(aliases tested "\n// Creates an independent")
part(clone-const deferred EOF)

function(cover parts)
    get_property(names GLOBAL PROPERTY base_parts)
    foreach(name IN LISTS parts)
        get_property(kind GLOBAL PROPERTY base_kind_${name})
        if(NOT name IN_LIST names OR kind STREQUAL "deferred")
            message(FATAL_ERROR "Cannot claim native coverage of base span: ${name}")
        endif()
        set_property(GLOBAL APPEND PROPERTY base_covered "${name}")
    endforeach()
endfunction()
function(sample parts name prefix code suffix)
    cover("${parts}")
    example(0 "${name}" "${prefix}" "${code}" "${suffix}")
endfunction()
# Extract a subfragment without losing its original body or silently matching a
# different overload/file. Outer module/trait contexts stay explicitly partial.
function(slice variable first last output)
    set(text "${${variable}}")
    replace_once(text "${first}" "${first}")
    string(FIND "${text}" "${first}" start)
    string(SUBSTRING "${text}" ${start} -1 tail)
    replace_once(tail "${last}" "${last}")
    string(FIND "${tail}" "${last}" end)
    if(NOT end GREATER 0)
        message(FATAL_ERROR "Invalid base-sample subfragment: ${first}")
    endif()
    string(SUBSTRING "${tail}" 0 ${end} code)
    set(${output} "${code}" PARENT_SCOPE)
endfunction()
function(expressions variable count output)
    string(STRIP "${${variable}}" text)
    string(REPLACE "\n" ";" lines "${text}")
    list(LENGTH lines actual)
    if(NOT actual EQUAL count)
        message(FATAL_ERROR "Update base-sample expression-line adapter: ${variable}")
    endif()
    set(code "")
    foreach(line IN LISTS lines)
        if(line MATCHES "^//")
            string(APPEND code "${line}\n")
            continue()
        endif()
        string(FIND "${line}" "//" at)
        if(at LESS 0)
            set(expr "${line}")
            set(comment "")
        else()
            string(SUBSTRING "${line}" 0 ${at} expr)
            string(SUBSTRING "${line}" ${at} -1 comment)
        endif()
        string(STRIP "${expr}" expr)
        if(ARGC EQUAL 4 AND ARGV3 STREQUAL "text")
            string(APPEND code "print(${expr}) ${comment}\n")
        else()
            string(APPEND code "print(str(${expr})) ${comment}\n")
        endif()
    endforeach()
    set(${output} "${code}" PARENT_SCOPE)
endfunction()

sample(comments comments "main() {" "${part_comments}"
    "let a = 1\nlet b = 2\nprint(str(a + b))\n}\n// expect: 3")
# TYPE x is reference notation, not a valid Brief declaration. Only its type/name
# tokens are used to generate correctly annotated binding probes.
string(STRIP "${part_types}" type_lines)
string(REPLACE "\n" ";" type_lines "${type_lines}")
list(LENGTH type_lines type_count)
if(NOT type_count EQUAL 11)
    message(FATAL_ERROR "Update the eleven primitive type-reference lines")
endif()
set(probes "")
set(calls "main() {\n")
set(expected "")
foreach(line IN LISTS type_lines)
    if(NOT line MATCHES "^([a-z]+) x$")
        message(FATAL_ERROR "Invalid primitive type-reference line: ${line}")
    endif()
    set(type "${CMAKE_MATCH_1}")
    if(type STREQUAL "bool")
        set(value true)
        set(observe "str(x)")
        set(result true)
    elseif(type STREQUAL "str")
        set(value "\"text\"")
        set(observe x)
        set(result text)
    elseif(type STREQUAL "chr")
        set(value "'a'")
        set(observe "str(int(x))")
        set(result 97)
    else()
        set(value 1)
        set(observe "str(x)")
        set(result 1)
    endif()
    string(APPEND probes "probe_${type}() { let x: ${type} = ${value}\nprint(${observe}) }\n")
    string(APPEND calls "probe_${type}()\n")
    string(APPEND expected "// expect: ${result}\n")
endforeach()
sample(types primitive-reference "${probes}" "${calls}}" "${expected}")
sample("typed;bindings" annotated-bindings "main() {" "${part_typed}"
    "print(str(count + 4294967296))\nprint(str(ratio / 3.0))\nratio = 1.5\nprint(str(ratio))\n}\n// expect: 4294967296\n// expect: 0.16666667\n// expect: 1.5")
sample(defaults inferred-bindings "acceptInt(n: int) { n }\nacceptDub(n: dub) { n }\nmain() {" "${part_defaults}"
    "print(str(acceptInt(small)))\nprint(str(acceptDub(half)))\n}\n// expect: 10\n// expect: 0.5")
set(code "${part_implicit}")
error_at(code "let total:" "Binding value does not match its annotation.")
sample(implicit implicit-conversion "main() { ${part_defaults}" "${code}" "}")
sample(conversions conversions "main() { ${part_defaults}" "${part_conversions}"
    "print(str(total))\nprint(label)\nprint(str(parsed))\n}\n// expect: 10\n// expect: 10\n// expect: 42")
set(code "${part_conversions}")
replace_once(code "int(\"42\")?" "int(\"bad\")?")
sample(conversions invalid-conversion "main() { ${part_defaults}" "${code}"
    "}\n// stderr: error: :invalid(\"bad\")\n// exit: 1")
sample(animal animal "type Species = Sym<:cat, :dog>" "${part_animal}" [=[
main() { let animal = Animal { id: 7, species: :cat }
  print(str(animal.id))
  animal.species |> { :cat => print("cat")
    :dog => print("dog") } }
// expect: 7
// expect: cat
]=])
sample(color color "" "${part_color}" [=[
show(color: Color) { color |> { :red => print("red")
  :green => print("green")
  :blue => print("blue") } }
main() { show(:red)
  show(:green)
  show(:blue) }
// expect: red
// expect: green
// expect: blue
]=])
sample(function addition "" "${part_function}"
    "main() { print(str(add(3, 4)))\nprint(str(3 |> add(4))) }\n// expect: 7\n// expect: 7")
expressions(part_arithmetic 4 code)
sample(arithmetic arithmetic "main() { let x = 12\nlet y = 4" "${code}"
    "}\n// expect: 16\n// expect: 8\n// expect: 48\n// expect: 3")
set(code "${part_updates}")
foreach(update IN ITEMS "x++" "y--" "x += y" "x -= y" "x *= y" "x /= y")
    if(update STREQUAL "y--")
        set(binding y)
    else()
        set(binding x)
    endif()
    replace_once(code "${update}\n" "${update}\nprint(str(${binding}))\n")
endforeach()
sample("updates;bindings" updates "main() { var x = 12\nvar y = 4" "${code}"
    "}\n// expect: 13\n// expect: 3\n// expect: 16\n// expect: 13\n// expect: 39\n// expect: 13")
cover("overflow;updates")
panic_example(0 integer-overflow "main() { var x: int = 2147483647\nvar y = 4"
    "${part_updates}" "integer overflow" "x++")
expressions(part_logic 2 code)
sample(logic logic "observe(x: bool, y: bool) {" "${code}" [=[
}
main() { observe(true, true)
  observe(true, false)
  observe(false, false)
  observe(false, true) }
// expect: true
// expect: true
// expect: false
// expect: true
// expect: false
// expect: false
// expect: false
// expect: true
]=])
expressions(part_precedence 2 code)
sample(precedence pipeline-precedence "double(n: lng) { n * 2 }\nobserve(count: lng) {" "${code}"
    "}\nmain() { observe(0)\nobserve(6) }\n// expect: false\n// expect: true")
set(code "${part_chained}")
# The raw sample already has // error: metadata; replace its shorthand rather
# than appending a second directive on the same source line.
replace_once(code "// error: comparisons do not chain"
    "// error: Comparison operators do not chain; use 'and' or parentheses.")
sample(chained chained-comparison "main() { let a = 1\nlet b = 2\nlet c = 3" "${code}" "}")
sample(conditional conditional "observe(x: int, y: int) {" "${part_conditional}"
    "}\nmain() { observe(1, 2)\nobserve(2, 2)\nobserve(3, 2) }\n// expect: less\n// expect: equal\n// expect: more")
sample(while while "observe(initial: int) { var x = initial" "${part_while}"
    "print(str(x))\n}\nmain() { observe(7)\nobserve(10)\nobserve(11) }\n// expect: 10\n// expect: 10\n// expect: 11")
set(code "${part_range}")
replace_once(code "for i in 0..10 { // 0 through 9" "for i in 0..10 { // 0 through 9\n  print(\"visit:\" + str(i))")
replace_once(code "    continue // next iteration" "    print(\"continue\")\n    continue // next iteration")
replace_once(code "    break // leave the loop" "    print(\"break\")\n    break // leave the loop")
replace_once(code "\n  }\n}\n" "\n  }\n  print(\"body:\" + str(i))\n}\n")
sample(range range-jumps "main() {" "${code}" [=[
}
// expect: visit:0
// expect: body:0
// expect: visit:1
// expect: body:1
// expect: visit:2
// expect: body:2
// expect: visit:3
// expect: continue
// expect: visit:4
// expect: body:4
// expect: visit:5
// expect: body:5
// expect: visit:6
// expect: body:6
// expect: visit:7
// expect: body:7
// expect: visit:8
// expect: break
]=])
sample(unit logging "" "${part_unit}"
    "main() { log(\"log\" + \" message\") }\n// expect: log message")
foreach(kind IN ITEMS written inferred)
    set(code "${part_unit}")
    replace_once(code "print(message)" "echo(message)")
    if(kind STREQUAL "inferred")
        replace_once(code "log(message: str): ()" "log(message: str)")
        set(call "let result = log(\"message\")\nprint(result)")
        set(output "// expect: message\n// expect: message")
    else()
        set(call "log(\"message\")")
        set(output "// expect: message")
    endif()
    sample(unit "tail-${kind}" "echo(message: str) { print(message)\nmessage + \"\" }"
        "${code}" "main() { ${call} }\n${output}")
endforeach()

# Guard the entire method inventory of each partial multi-feature span, including
# excluded methods. A new method must not silently disappear from this manifest.
function(method_inventory variable expected)
    string(REGEX MATCHALL "\n  (ovr )?(pub )?[A-Za-z_][A-Za-z0-9_]*\\([^\n]*" signatures "${${variable}}")
    set(names "")
    foreach(signature IN LISTS signatures)
        string(REGEX REPLACE "^\n  (ovr )?(pub )?([A-Za-z_][A-Za-z0-9_]*)\\(.*$" "\\3" name "${signature}")
        list(APPEND names "${name}")
    endforeach()
    if(NOT names STREQUAL expected)
        message(FATAL_ERROR "Update ${variable}'s complete method inventory: ${names}")
    endif()
endfunction()
method_inventory(part_user "getUser;getName;getAge;getAgeRange;getUser;new;fromJson;toJson")
method_inventory(part_admin "getUser;getName;new")
method_inventory(part_collection "new;add;fromJson;toJson;findByName;removeByName;count;olderThan")

# Preserve first-version bodies, but do not claim their trait/module/const/JSON
# surroundings work. These subfragment adapters are all exact single anchors.
slice(part_user "\npub User {" "\npub type Tier" user_type)
slice(part_user "\npub type Tier" "\npub trait HasUser" tier_type)
slice(part_user "\n  pub getName(value:" "\n}\n\nimpl User:" getters)
slice(part_user "  pub const VERSION:" "\n  getUser(user:" version)
string(STRIP "${version}" version)
replace_once(version "pub const " "let ")
slice(part_user "\n  getUser(user:" "\n  pub new(name:" identity)
slice(part_user "\n  pub new(name:" "\n  pub fromJson(" constructor)
replace_once(constructor ": Sym<:ok(User), :err(str)> {" ": Sym<:ok(User), :err(str)> {\n    ${version}")
set(user_api "${user_type}\n${tier_type}\nimpl User {\n${identity}\n${getters}\n${constructor}\n}\n")
sample(user user-methods "${user_api}" [=[
observe(age: int) {
  let user = User { name: "B" + "ob", age: age, userVer: 0.5 }
  print(user.getName() + ":" + str(user.getAge()))
  print(str(user.getUser() == user))
  user.getAgeRange() |> { :negative => print("negative")
    :ofAge => print("ofAge")
    :old => print("old") }
}
main() { observe(-1)
  observe(20)
  observe(100) }
// expect: Bob:-1
// expect: true
// expect: negative
// expect: Bob:20
// expect: true
// expect: ofAge
// expect: Bob:100
// expect: true
// expect: old
]=] "")
sample(user user-constructor "${user_api}" [=[
observe(age: int) { User.new("Bob", age) |> {
  :ok(user) => print(user.name + ":" + str(user.age) + ":" + str(user.userVer))
  :err(message) => print(message)
} }
main() { observe(20)
  observe(17) }
// expect: Bob:20:0.5
// expect: Age is too low
]=] "")
slice(part_user "\n  pub fromJson(" "\n  pub toJson(" from_json)
replace_once(from_json "Json.Error" "JsonError")
set(json_api [=[
type JsonError = Sym<:missing(str)>
Json { name: str
  age: int
  hasName: bool
  hasAge: bool }
impl Json {
  getStr(json: Json, key: str): Sym<:ok(str), JsonError> {
    print("getStr:" + key)
    if !json.hasName { ret :missing(key) }
    json.name
  }
  getInt(json: Json, key: str): Sym<:ok(int), JsonError> {
    print("getInt:" + key)
    if !json.hasAge { ret :missing(key) }
    json.age
  }
}
]=])
sample(user user-from-json "${user_api}\n${json_api}\nimpl User { ${from_json}\n}" [=[
observe(hasName: bool, hasAge: bool, age: int) {
  let json = Json { name: "J" + "son", age: age, hasName: hasName, hasAge: hasAge }
  User.fromJson(json) |> { :ok(user) => print(user.name)
    :missing(key) => print("missing:" + key)
    :err(message) => print(message) }
}
main() { observe(true, true, 20)
  observe(false, true, 20)
  observe(true, false, 20)
  observe(true, true, 17) }
// expect: getStr:name
// expect: getInt:age
// expect: Json
// expect: getStr:name
// expect: missing:name
// expect: getStr:name
// expect: getInt:age
// expect: missing:age
// expect: getStr:name
// expect: getInt:age
// expect: Age is too low
]=] "")
slice(part_admin "\npub Admin {" "\nimpl Admin:" admin_type)
replace_once(admin_type "User.Tier" "Tier")
slice(part_admin "\n  getUser(admin:" "\n  ovr pub getName(" admin_identity)
slice(part_admin "\n  ovr pub getName(" "\n  // ending with a plain value" admin_getter)
replace_once(admin_getter "ovr pub" "pub")
slice(part_admin "\n  // ending with a plain value" "\n}\n" admin_constructor)
set(admin_api "${user_api}\n${admin_type}\nimpl Admin { ${admin_identity}\n${admin_getter}\n${admin_constructor}\n}")
sample(admin admin "${admin_api}" [=[
observe(age: int) { Admin.new("Bob", age) |> {
  :ok(admin) => {
    print(admin.getName() + ":" + str(admin.getUser().age))
    print(str(admin.userTier == :tier1))
  }
  :err(message) => print(message)
} }
main() { observe(20)
  observe(17) }
// expect: Bob:20
// expect: true
// expect: Age is too low
]=] "")
slice(part_collection "\n  pub add(collection:" "\n  pub fromJson(" retaining)
set(slot_api [=[
UserSlot { user: User }
UserCollection { userVec: UserSlot
  userVersion: flt }
impl UserSlot { add(slot: &UserSlot, user: &User): str {
  slot.user = user
  "stored:" + user.name
} }
]=])
sample("collection;borrow" retaining "${user_api}\n${slot_api}\nimpl UserCollection { ${retaining}\n}" [=[
fill(collection: &UserCollection) {
  let user = User.new("B" + "ob", 20)!
  collection.add(user)
}
main() {
  let slot = UserSlot { user: User.new("old", 20)! }
  let collection = UserCollection { userVec: slot, userVersion: 0.5 }
  fill(collection)
  print(collection.userVec.user.name)
}
// expect: Bob
]=] "")
sample(entry entry "${user_api}" "${part_entry}" "// expect: Bob")
# The three const alternatives test only construction/handling with let. Const
# access permissions are not inferred from these binding-keyword adaptations.
slice(part_constructors "const user = User.new(\"Bob\", 20)?" "\n// or\nconst user = User.new(\"Bob\", 20)!" propagate)
slice(part_constructors "const user = User.new(\"Bob\", 20)!" "\n// or\nconst user = User.new(\"Bob\", 20) |>" bang)
string(FIND "${part_constructors}" "const user = User.new(\"Bob\", 20) |>" match_start)
if(match_start LESS 0)
    message(FATAL_ERROR "Missing explicit constructor match")
endif()
string(SUBSTRING "${part_constructors}" ${match_start} -1 handled)
replace_once(handled "const user =" "let user =")
foreach(kind IN ITEMS propagate bang handled)
    set(code "${${kind}}")
    if(NOT kind STREQUAL "handled")
        replace_once(code "const user =" "let user =")
    endif()
    replace_once(code "User.new(\"Bob\", 20)" "User.new(\"Bob\", age)")
    if(kind STREQUAL "bang")
        set(main "main() { print(build(20).name) }\n// expect: Bob")
    else()
        set(main [=[
observe(age: int) { build(age) |> { :ok(user) => print(user.name)
  :err(message) => print(message) } }
main() { observe(20)
  observe(17) }
// expect: Bob
// expect: Age is too low
]=])
    endif()
    sample(constructors "construct-${kind}" "${user_api}\nbuild(age: int) {" "${code}" "user\n}\n${main}")
endforeach()
set(code "${bang}")
replace_once(code "const user =" "let user =")
replace_once(code "User.new(\"Bob\", 20)!" "User.new(\"Bob\", 17)!")
cover(constructors)
panic_example(0 construct-panic "${user_api}\nmain() {" "${code}"
    "panic: :err(\"Age is too low\")" "User.new(\"Bob\", 17)!")
set(code "${part_age-message}")
replace_once(code "const message =" "let message =")
sample(age-message age-message "${user_api}\nobserve(age: int) { let user = User { name: \"Bob\", age: age, userVer: 0.5 }" "${code}"
    "print(message)\n}\nmain() { observe(-1)\nobserve(20)\nobserve(100) }\n// expect: Cannot be negative\n// expect: Age is in adult range\n// expect: Age is above 100")
expressions(part_calls 5 code text)
sample(calls call-forms "${user_api}\nsave(user: User) { \"saved:\" + user.name }\nmain() { let user = User.new(\"Bob\", 20)!" "${code}"
    "}\n// expect: Bob\n// expect: saved:Bob\n// expect: saved:Bob")
# getName belongs to User; save is only a module function. Its dotted spelling
# must not silently use the direct/pipeline fallback.
set(code "user.save()")
error_at(code "user.save" "Unknown method for this type.")
sample(calls invalid-method "${user_api}\nsave(user: User) { user.name }\nmain() { let user = User.new(\"Bob\", 20)!" "${code}" "}")
set(code "${part_pipeline}")
replace_once(code "const valid =" "let valid =")
set(pipeline_api [=[
Parsed { age: int }
parse(text: str): Sym<:ok(Parsed), :syntax> {
  print("parse")
  if text == "syntax" { ret :syntax }
  Parsed { age: if text == "young" { 17 } else { 20 } }
}
validate(parsed: Parsed): Sym<:ok(bool), :invalidAge(int)> {
  print("validate")
  if parsed.age < 18 { ret :invalidAge(parsed.age) }
  true
}
]=])
sample(pipeline pipeline-handling "${pipeline_api}\nwork(json: str) {" "${code}" [=[
print(str(valid))
}
observe(text: str) { work(text) |> { :ok => print("done")
  :syntax => print("syntax")
  :invalidAge(age) => print("age:" + str(age)) } }
main() { observe("ok")
  observe("syntax")
  observe("young") }
// expect: parse
// expect: validate
// expect: true
// expect: done
// expect: parse
// expect: syntax
// expect: parse
// expect: validate
// expect: age:17
]=])
sample(aliases shared-object "${user_api}\nmain() {" "${part_aliases}"
    "other.age = 21\nprint(str(user.age))\nuser.age = 22\nprint(str(other.age))\n}\n// expect: 21\n// expect: 22")
set(code "let user = User.new(\"Bob\", 20)!")
# This exact original line also occurs in other scopes; use the isolated alias
# span to guard the complete line before exercising top-level rejection.
set(alias_source "${part_aliases}")
replace_once(alias_source "${code}" "${code}")
error_at(code "let user" "Expected a declaration (function, struct, named type, const, or impl).")
sample(entry top-level-binding "${user_api}" "${code}" "main() {}")

get_property(rest GLOBAL PROPERTY base_remaining)
get_property(parts GLOBAL PROPERTY base_parts)
get_property(covered GLOBAL PROPERTY base_covered)
list(LENGTH parts part_count)
if(NOT rest STREQUAL "" OR NOT part_count EQUAL 38)
    message(FATAL_ERROR "Incomplete base-sample span manifest")
endif()
set(reconstructed "")
foreach(kind IN ITEMS tested partial reference deferred)
    set(count_${kind} 0)
endforeach()
foreach(name IN LISTS parts)
    string(APPEND reconstructed "${part_${name}}")
    get_property(kind GLOBAL PROPERTY base_kind_${name})
    math(EXPR count_${kind} "${count_${kind}} + 1")
    if((kind STREQUAL "tested" OR kind STREQUAL "partial") AND NOT name IN_LIST covered)
        message(FATAL_ERROR "Missing base-sample scenario for ${name}")
    endif()
endforeach()
if(NOT reconstructed STREQUAL document OR NOT count_tested EQUAL 20 OR
   NOT count_partial EQUAL 6 OR NOT count_reference EQUAL 4 OR NOT count_deferred EQUAL 8)
    message(FATAL_ERROR "Base-sample source reconstruction/classifications changed")
endif()
message(STATUS "lang-ex-base: all 38 contiguous raw-source spans classified (20 tested, 6 partial, 4 references, 8 deferred; no code fences)")
doc_examples_finish("" "" 38)
