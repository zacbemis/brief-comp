# M6 standard-library document audit. Its single JSON fence is explicitly partial;
# no print/str/numeric-builtin examples occur in this chapter. See doc-stdlib.md.
cmake_minimum_required(VERSION 3.16)
include("${CMAKE_CURRENT_LIST_DIR}/doc_examples.cmake")
doc_examples_begin(lang-stdlib 1)
file(READ "${ROOT}/docs/lang-stdlib.txt" document)
string(REGEX REPLACE "[\r\n ]+" " " prose "${document}")
foreach(rule IN ITEMS
        "Object fields remain encapsulated."
        "Constructors enforce invariants."
        "JSON parsing errors are typed."
        "Validation errors are propagated explicitly."
        "The compiler checks error compatibility."
        "Functions remain short and readable."
        "JSON APIs and error-group composition shown here are illustrative and have not yet been finalized as standard-library contracts.")
    string(FIND "${prose}" "${rule}" at)
    if(at LESS 0)
        message(FATAL_ERROR "Missing standard-library scope/rule anchor: ${rule}")
    endif()
endforeach()
# Guard every original method, including the excluded serializer. Additional
# methods must receive an explicit classification instead of silently passing.
string(REGEX MATCHALL "\n  (pub )?[A-Za-z_][A-Za-z0-9_]*\\([^\n]*" methods "${block_1}")
set(names "")
foreach(method IN LISTS methods)
    string(REGEX REPLACE "^\n  (pub )?([A-Za-z_][A-Za-z0-9_]*)\\(.*$" "\\2" name "${method}")
    list(APPEND names "${name}")
endforeach()
if(NOT names STREQUAL "new;fromJson;toJson")
    message(FATAL_ERROR "Update the complete standard-library example method inventory: ${names}")
endif()
set(serializer [=[
  pub toJson(user: User): Json {
    Json.object()
      |> set("name", Json.str(user.name))
      |> set("age", Json.int(user.age))
  }
]=])
set(code "${block_1}")
replace_once(code "${serializer}" "")
replace_once(code "Json.Error" "JsonError")
set(user_api "${code}")

# Deterministic field-access stand-ins, not a JSON representation/parser/schema.
# Status 0 succeeds, 1 is missing, 2 has the wrong illustrative field kind.
# Error strings are allocated so failure propagation must own them independently
# after the caller's DTO and getter temporaries have been released.
set(json_api [=[
type JsonError = Sym<:missing(str), :wrongType(str)>
Json { name: str
  age: int
  nameStatus: int
  ageStatus: int }
impl Json {
  getStr(json: Json, key: str): Sym<:ok(str), JsonError> {
    print("getStr:" + key)
    if json.nameStatus == 1 { ret :missing(key + "") }
    if json.nameStatus == 2 { ret :wrongType(key + "") }
    json.name
  }
  getInt(json: Json, key: str): Sym<:ok(int), JsonError> {
    print("getInt:" + key)
    if json.ageStatus == 1 { ret :missing(key + "") }
    if json.ageStatus == 2 { ret :wrongType(key + "") }
    json.age
  }
}
input(nameStatus: int, ageStatus: int, age: int): Json {
  Json { name: "B" + "ob", age: age, nameStatus: nameStatus, ageStatus: ageStatus }
}
]=])
set(api "${json_api}\n${user_api}")
example(1 constructor "${api}" [=[
observe(age: int) { User.new("B" + "ob", age) |> {
  :ok(user) => print(user.name + ":" + str(user.age))
  :invalidAge(found) => print("invalid:" + str(found))
} }
main() { observe(-1)
  observe(17)
  observe(18)
  observe(20) }
// expect: invalid:-1
// expect: invalid:17
// expect: Bob:18
// expect: Bob:20
]=] "")
set(load_api [=[
load(nameStatus: int, ageStatus: int, age: int) {
  let json = input(nameStatus, ageStatus, age)
  User.fromJson(json)?
}
]=])
example(1 getter-flow-and-ownership "${api}\n${load_api}" [=[
observe(nameStatus: int, ageStatus: int, age: int) {
  load(nameStatus, ageStatus, age) |> {
    :ok(user) => print(user.name + ":" + str(user.age))
    :missing(key) => print("missing:" + key)
    :wrongType(key) => print("wrong:" + key)
    :invalidAge(found) => print("invalid:" + str(found))
  }
}
main() { observe(0, 0, 20)
  observe(1, 0, 20)
  observe(2, 0, 20)
  observe(0, 1, 20)
  observe(0, 2, 20)
  observe(0, 0, 17) }
// expect: getStr:name
// expect: getInt:age
// expect: Bob:20
// expect: getStr:name
// expect: missing:name
// expect: getStr:name
// expect: wrong:name
// expect: getStr:name
// expect: getInt:age
// expect: missing:age
// expect: getStr:name
// expect: getInt:age
// expect: wrong:age
// expect: getStr:name
// expect: getInt:age
// expect: invalid:17
]=] "")
example(1 local-error-group "${api}\n${load_api}" [=[
observe(nameStatus: int, ageStatus: int, age: int) {
  load(nameStatus, ageStatus, age) |> {
    :ok(user) => print(user.name)
    JsonError => print("field error")
    :invalidAge(found) => print("invalid:" + str(found))
  }
}
main() { observe(1, 0, 20)
  observe(0, 2, 20)
  observe(0, 0, 17)
  observe(0, 0, 18) }
// expect: getStr:name
// expect: field error
// expect: getStr:name
// expect: getInt:age
// expect: field error
// expect: getStr:name
// expect: getInt:age
// expect: invalid:17
// expect: getStr:name
// expect: getInt:age
// expect: Bob
]=] "")
set(code "${user_api}")
replace_once(code "ret :invalidAge(age)" "ret :invalidAge(name)")
error_at(code "ret :invalidAge(name)" "Return value does not match the written return type.")
example(1 wrong-validation-payload "${json_api}" "${code}" "main() {}")
set(code "${user_api}")
replace_once(code "Sym<:ok(User), JsonError, :invalidAge(int)>" "Sym<:ok(User), :invalidAge(int)>")
error_at(code "let name = json.getStr" "Propagated variants are not in the written return type.")
error_at(code "let age = json.getInt" "Propagated variants are not in the written return type.")
example(1 missing-field-error-contract "${json_api}" "${code}" "main() {}")
set(code "${user_api}")
replace_once(code "Sym<:ok(User), JsonError, :invalidAge(int)>" "Sym<:ok(User), JsonError>")
error_at(code "User.new(name, age)?" "Propagated variants are not in the written return type.")
example(1 missing-validation-contract "${json_api}" "${code}" "main() {}")
foreach(stage IN ITEMS name age constructor)
    set(code "${user_api}")
    if(stage STREQUAL "name")
        set(call "json.getStr(\"name\")")
    elseif(stage STREQUAL "age")
        set(call "json.getInt(\"age\")")
    else()
        set(call "User.new(name, age)")
    endif()
    replace_once(code "${call}?" "${call}")
    error_at(code "${call}" "A call that can fail must be handled with ?, !, or a match.")
    if(NOT stage STREQUAL "constructor")
        # The invalid binding retains the full outcome type, so passing it to
        # the constructor currently adds a follow-on argument-type error.
        error_at(code "User.new(name, age)?" "Function argument has the wrong type.")
    endif()
    example(1 "unhandled-${stage}" "${json_api}" "${code}" "main() {}")
endforeach()
set(code "User.new(\"Bob\", 18)")
error_at(code "User.new" "A call that can fail must be handled with ?, !, or a match.")
example(1 unhandled-constructor-caller "${api}\nmain() {" "${code}" "}")
set(code "User.fromJson(json)")
error_at(code "User.fromJson" "A call that can fail must be handled with ?, !, or a match.")
example(1 unhandled-from-json-caller "${api}\nmain() { let json = input(0, 0, 20)" "${code}" "}")
set(observer [=[
User.fromJson(json) |> {
  :ok(user) => print(user.name)
  :missing(key) => print(key)
  :wrongType(key) => print(key)
  :invalidAge(age) => print(str(age))
}
]=])
foreach(variant IN ITEMS missing wrongType invalidAge)
    set(code "${observer}")
    if(variant STREQUAL "invalidAge")
        set(arm "  :invalidAge(age) => print(str(age))\n")
    else()
        set(arm "  :${variant}(key) => print(key)\n")
    endif()
    replace_once(code "${arm}" "")
    error_at(code "User.fromJson(json)" "Match is not exhaustive.")
    example(1 "missing-${variant}-arm" "${api}\nmain() { let json = input(0, 0, 20)" "${code}" "}")
endforeach()
example(1 field-entry-error "${api}" "main() { let json = input(1, 0, 20)\nUser.fromJson(json)? }"
    "// expect: getStr:name\n// stderr: error: :missing(\"name\")\n// exit: 1")
example(1 validation-entry-error "${api}" "main() { let json = input(0, 0, 17)\nUser.fromJson(json)? }"
    "// expect: getStr:name\n// expect: getInt:age\n// stderr: error: :invalidAge(17)\n// exit: 1")
# Constructor checks are not a compiler-maintained invariant on every instance.
# Same-file direct initialization is legal; module privacy is not tested here.
example(1 direct-initialization "${api}" [=[
main() { let user = User { name: "young", age: 0 }
  print(user.name + ":" + str(user.age)) }
// expect: young:0
]=] "")

doc_examples_finish("" "" 17)
