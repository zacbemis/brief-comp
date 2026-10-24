# M6 collections scope audit. Only isolated symbol handlers and the written
# manual while expansion run; no Vec, lambda, query, trait or iterator lowering.
cmake_minimum_required(VERSION 3.16)
include("${CMAKE_CURRENT_LIST_DIR}/doc_examples.cmake")
doc_examples_begin(lang-collections 31)
file(READ "${ROOT}/docs/lang-collections.txt" document)
string(REGEX REPLACE "[\r\n ]+" " " prose "${document}")
foreach(rule IN ITEMS
        "Collections are queried and changed through ordinary functions. There is no query syntax."
        "Changing functions take the collection as `&`"
        "The first element that fails stops the operation, and its error is returned."
        "For `update`, changes made to earlier elements remain."
        "Positions are kept: the fifth input produces the fifth output."
        "Each function runs as soon as it is called and returns a new collection. Elements are shared, not copied."
        "`orderBy` is stable: elements with equal keys keep their original order."
        "every ASCII capital letter comes before every ASCII lowercase letter"
        "Symbols sort in the order their tags are listed in their type"
        "`min` and `max` return the element, not the value."
        "`avg` always returns a `dub`."
        "Otherwise the cursor checks the length at each step and panics if it has changed."
        "A `for` loop is compiled into calls to `next`."
        "Because the loop body is not a lambda, `break`, `continue`, and `ret` inside it behave as in any loop."
        "This needs the compiler to be able to inspect a lambda as data and is not planned for the first versions."
        "`select` is the only name for transforming elements. There is no `map` alias.")
    string(FIND "${prose}" "${rule}" at)
    if(at LESS 0)
        message(FATAL_ERROR "Missing collections rule/scope anchor: ${rule}")
    endif()
endforeach()
# All 26 table operations are deferred, not supplied as fixture implementations.
string(REGEX MATCHALL "\n\\| `[A-Za-z]+\\([^\n]*" rows "${document}")
set(names "")
foreach(row IN LISTS rows)
    string(REGEX REPLACE "^\n\\| `([A-Za-z]+)\\(.*$" "\\1" name "${row}")
    list(APPEND names "${name}")
endforeach()
if(NOT names STREQUAL "where;select;orderBy;distinct;limit;offset;groupBy;join;find;first;reverse;selectMany;toMap;count;sum;avg;min;max;isEmpty;contains;any;all;fold;add;update;remove")
    message(FATAL_ERROR "Update the complete deferred collection operation inventory: ${names}")
endif()
set(anchor_1 "|> where(")
set(anchor_2 ".where(")
set(anchor_3 "const frozen = Vec<User>()")
set(anchor_4 "|> offset(40)")
set(anchor_5 "|> sum(")
set(anchor_6 "|> any(")
set(anchor_8 "pub removeByName")
set(anchor_9 "|> groupBy(")
set(anchor_10 "users |> join(")
set(anchor_12 "users |> update(")
set(anchor_13 "pub ageOf(users: Vec<User>")
set(anchor_14 "loadAll(items: Vec<Json>)")
set(anchor_15 "let names = users |> select")
set(anchor_16 "items |> select((json) => User.fromJson(json))")
set(anchor_17 "Json.Error => User.blank()")
set(anchor_18 "for json in items")
set(anchor_19 "// adults is a new Vec")
set(anchor_20 "desc(user.getAge())")
set(anchor_21 "tier: desc(user.getTier())")
set(anchor_23 "let groups = users |> groupBy")
set(anchor_24 "let byTier = users |> toMap")
set(anchor_25 "const users = loadUsers()!")
set(anchor_26 "// Compiler error: users is being walked")
set(anchor_27 "// panics: the collection changed during the loop")
set(anchor_28 "next(cursor: &Cursor<T>)")
set(anchor_30 "pub trait Query<T>")
set(anchor_31 "impl UserCollection: Query<User>")
set(deferred "1;2;3;4;5;6;8;9;10;12;13;14;15;16;17;18;19;20;21;23;24;25;26;27;28;30;31")
foreach(index IN LISTS deferred)
    set(code "${block_${index}}")
    replace_once(code "${anchor_${index}}" "${anchor_${index}}")
endforeach()
function(match_tail variable output)
    set(code "${${variable}}")
    replace_once(code "|> {" "|> {")
    string(FIND "${code}" "|> {" at)
    string(SUBSTRING "${code}" ${at} -1 code)
    set(${output} "${code}" PARENT_SCOPE)
endfunction()
set(user_api [=[
User { name: str
  age: int }
impl User {
  getName(user: User): str { user.name }
  getAge(user: User): int { user.age }
}
input(found: bool): Sym<:ok(User), :none> {
  if !found { ret :none }
  :ok(User { name: "B" + "ob", age: 20 })
}
]=])
# Preserve only the original retagging handler, not the UserCollection/find
# signature, field access or lambda. The input is a controlled symbol outcome.
set(code "${block_7}")
replace_once(code "pub findByName(collection: UserCollection, name: str): Sym<:found(User), :missing> {"
    "pub findByName(collection: UserCollection, name: str): Sym<:found(User), :missing> {")
match_tail(code retag)
replace_once(retag "\n}\n" "\n")
set(retag_prefix "${user_api}\nretag(found: bool): Sym<:found(User), :missing> {\ninput(found)")
foreach(found IN ITEMS true false)
    if(found)
        set(expected "found:Bob")
    else()
        set(expected missing)
    endif()
    example(7 "retag-${found}" "${retag_prefix}" "${retag}" "}
main() { retag(${found}) |> {
  :found(user) => print(\"found:\" + user.getName())
  :missing => print(\"missing\")
} }
// expect: ${expected}")
endforeach()
set(code "${retag}")
replace_once(code "      :none => :missing\n" "")
set(prefix "${retag_prefix}")
error_at(prefix "input(found)" "Match is not exhaustive.")
example(7 retag-nonexhaustive "${prefix}" "${code}" "}\nmain() {}")
# No max implementation: run the exact original display handler with both tags.
match_tail(block_11 display)
foreach(found IN ITEMS true false)
    if(found)
        set(expected Bob)
    else()
        set(expected "No users")
    endif()
    example(11 "display-${found}" "${user_api}\nmain() { input(${found})" "${display}"
        "}\n// expect: ${expected}")
endforeach()
# Retain direct ! handling and the age projection; replace only the deferred
# max/lambda query, not its handler, with a controlled fallible call.
set(highest "${block_22}")
replace_once(highest "users |> max((user) => user.getAge())" "input(true)")
example(22 highest-age "${user_api}\nmain() {" "${highest}"
    "print(str(highestAge))\n}\n// expect: 20")
set(delayed "${highest}")
replace_once(delayed "input(true)!" "input(true)")
replace_once(delayed "oldest.getAge()" "oldest!.getAge()")
error_at(delayed "input(true)" "A call that can fail must be handled with ?, !, or a match.")
example(0 delayed-handling "${user_api}\nmain() {" "${delayed}" "}")
replace_once(highest "input(true)" "input(false)")
panic_example(22 highest-empty "${user_api}\nmain() {"
    "${highest}" "panic: :none" "input(false)!")

# A concrete, synthetic cursor producing fresh User values, not stored elements
# or a collection implementation. There is no trait/generic for-loop lowering.
set(cursor_api [=[
Source { size: int }
Cursor { index: int
  end: int }
impl Source {
  cursor(source: Source): Cursor { Cursor { index: 0, end: source.size } }
}
impl Cursor {
  next(cursor: &Cursor): Sym<:ok(User), :none> {
    let i = cursor.index
    print("next:" + str(i))
    if i >= cursor.end { ret :none }
    cursor.index++
    :ok(User { name: "User" + str(i), age: 18 + i })
  }
}
]=])
set(manual "${block_29}")
replace_once(manual [=[for user in users {
  print(user.getName())
}

// is compiled as

]=] "")
foreach(size IN ITEMS 0 3)
    if(size EQUAL 0)
        set(expected "// expect: next:0")
    else()
        set(expected "// expect: next:0\n// expect: User0\n// expect: next:1\n// expect: User1\n// expect: next:2\n// expect: User2\n// expect: next:3")
    endif()
    example(29 "manual-cursor-${size}" "${user_api}\n${cursor_api}\nmain() { let users = Source { size: ${size} }"
        "${manual}" "}\n${expected}")
endforeach()
foreach(jump IN ITEMS break continue ret)
    set(code "${manual}")
    replace_once(code "    :ok(user) => print(user.getName())" "    :ok(user) => {
      if user.age == 19 { ${jump} }
      print(user.getName())
    }")
    set(expected "// expect: next:0\n// expect: User0\n// expect: next:1")
    if(jump STREQUAL "continue")
        string(APPEND expected "\n// expect: next:2\n// expect: User2\n// expect: next:3")
    endif()
    if(NOT jump STREQUAL "ret")
        string(APPEND expected "\n// expect: done")
    endif()
    string(APPEND expected "\n// expect: returned")
    example(29 "manual-cursor-${jump}" "${user_api}\n${cursor_api}\nwalk() { let users = Source { size: 3 }"
        "${code}" "print(\"done\")\n}\nmain() { walk()\nprint(\"returned\") }\n${expected}")
endforeach()
# Byte comparisons from the ordering prose are supported; sorting itself is not.
example(0 byte-key-comparisons "" [=[
main() {
  print(str("Zoe" < "adam"))
  print(str("Z" < "a"))
  print(str("Éclair" < "adam"))
}
// expect: true
// expect: true
// expect: false
]=] "")
doc_examples_finish("${deferred}" "" 14)
