# M5 acceptance: execute the in-scope lang-errors examples, not just dump-ast.
# See doc-errors.md for the complete fence audit and scaffolding/exclusions.
cmake_minimum_required(VERSION 3.16)
file(READ "${ROOT}/docs/lang-errors.txt" rest)
set(count 0)
while(TRUE)
    string(FIND "${rest}" "```" opening)
    if(opening LESS 0)
        break()
    endif()
    math(EXPR opening "${opening} + 3")
    string(SUBSTRING "${rest}" ${opening} -1 rest)
    string(FIND "${rest}" "\n" newline)
    math(EXPR newline "${newline} + 1")
    string(SUBSTRING "${rest}" ${newline} -1 rest)
    string(FIND "${rest}" "```" closing)
    if(closing LESS 0)
        message(FATAL_ERROR "Unclosed lang-errors code fence")
    endif()
    math(EXPR count "${count} + 1")
    string(SUBSTRING "${rest}" 0 ${closing} block_${count})
    math(EXPR closing "${closing} + 3")
    string(SUBSTRING "${rest}" ${closing} -1 rest)
endwhile()
if(NOT count EQUAL 36)
    message(FATAL_ERROR "lang-errors fence count changed; update doc-errors.md and this audit")
endif()

# Adapt only explicit fragment boundaries/signatures; fail if an anchor drifts.
function(replace_once variable needle replacement)
    set(code "${${variable}}")
    string(FIND "${code}" "${needle}" at)
    if(at LESS 0)
        message(FATAL_ERROR "Missing example adapter anchor: ${needle}")
    endif()
    string(LENGTH "${needle}" size)
    math(EXPR end "${at} + ${size}")
    string(SUBSTRING "${code}" 0 ${at} before)
    string(SUBSTRING "${code}" ${end} -1 after)
    string(FIND "${after}" "${needle}" duplicate)
    if(NOT duplicate LESS 0)
        message(FATAL_ERROR "Ambiguous example adapter anchor: ${needle}")
    endif()
    set(${variable} "${before}${replacement}${after}" PARENT_SCOPE)
endfunction()
function(error_at variable needle message)
    string(FIND "${${variable}}" "${needle}" at)
    if(at LESS 0)
        message(FATAL_ERROR "Missing error anchor: ${needle}")
    endif()
    string(SUBSTRING "${${variable}}" ${at} -1 tail)
    string(FIND "${tail}" "\n" end)
    if(end LESS 0)
        string(LENGTH "${tail}" end)
    endif()
    string(SUBSTRING "${tail}" 0 ${end} line)
    replace_once(${variable} "${line}" "${line} // error: ${message}")
    set(${variable} "${${variable}}" PARENT_SCOPE)
endfunction()

file(REMOVE_RECURSE "${TEST_DIR}/programs")
file(MAKE_DIRECTORY "${TEST_DIR}/programs")
function(example index name prefix code suffix)
    file(WRITE "${TEST_DIR}/programs/${name}.brf" "${prefix}\n${code}\n${suffix}\n")
    set_property(GLOBAL APPEND PROPERTY checked_fences "${index}")
    set_property(GLOBAL APPEND PROPERTY example_cases "${name}")
endfunction()
function(panic_example index name prefix code message needle)
    set(source "${prefix}\n${code}\n}\n")
    string(FIND "${source}" "${needle}" at)
    if(at LESS 0)
        message(FATAL_ERROR "Missing panic location anchor: ${needle}")
    endif()
    string(SUBSTRING "${source}" 0 ${at} before)
    string(REGEX MATCHALL "\n" lines "${before}")
    list(LENGTH lines line)
    # Count the source line and example()'s empty-prefix leading newline.
    math(EXPR line "${line} + 2")
    example(${index} "${name}" "" "${source}"
        "// panic: ${message}\n// panic: ${name}.brf:${line}")
endfunction()

# These are local nominal structs and deterministic functions, not JSON or I/O.
set(json_type "Json { text: str }")
set(parse_signature "parse(text: str): Sym<:ok(Json), :syntax(int)>")
set(parse_body [=[{ if text == "syntax" { ret :syntax(7) }
  Json { text: text } }]=])
set(parse_definition "${parse_signature} ${parse_body}")
set(read_signature "readFile(path: str): Sym<:ok(str), :notFound(str), :permissionDenied(str)>")
set(read_body [=[{ if path == "missing" { ret :notFound(path) }
  if path == "denied" { ret :permissionDenied(path) }
  path }]=])
set(api "${json_type}\n${read_signature} ${read_body}\n${parse_definition}")
set(simple_observer [=[
observe(text: str) {
  load(text) |> { :ok(json) => print(json.text)
    :syntax(line) => print(str(line)) }
}
main() { observe("ok")
  observe("syntax") }
// expect: ok
// expect: 7
]=])
set(full_observer [=[
observe(path: str) {
  loadJson(path) |> { :ok(json) => print(json.text)
    :notFound(_) => print("missing")
    :permissionDenied(_) => print("denied")
    :syntax(line) => print(str(line)) }
}
main() { observe("ok")
  observe("missing")
  observe("denied")
  observe("syntax") }
// expect: ok
// expect: missing
// expect: denied
// expect: 7
]=])
set(code "${block_1}")
replace_once(code "${parse_signature}" "${parse_definition}")
example(1 explicit-success "${json_type}" "${code}" "${simple_observer}")
example(2 implicit-success "${api}" "${block_2}" "${simple_observer}")
example(3 expanded-try "${api}\nload(text: str) {" "${block_3}"
    "json\n}\n${simple_observer}")
example(4 panic-success "${api}\nmain() { let text = \"ok\"" "${block_4}"
    "print(json.text)\n}\n// expect: ok")
panic_example(4 panic-failure "${api}\nmain() { let text = \"syntax\""
    "${block_4}" "panic: :syntax(7)" "let json = parse(text)!")

set(file_type "File { fail: bool }")
set(close_signature "close(file: &File): Sym<:ok, :ioError(str)>")
set(close_body [=[{ if file.fail { ret :ioError("closed badly") }
  :ok }]=])
set(file_api "${file_type}\nimpl File { ${close_signature} ${close_body} }")
set(code "${block_6}")
replace_once(code "${close_signature}" "")
example(6 payload-free-ok "${file_api}\nmain() { let file = File { fail: false }"
    "${code}" "}")
set(user_type "User { name: str }\nimpl User { getName(user: User): str { user.name } }")
set(classifier [=[
impl User { getAgeRange(user: User): Sym<:adult, :senior> { :adult } }
]=])
set(code "${block_7}")
replace_once(code "getAgeRange(user: User): Sym<:adult, :senior>" "")
error_at(code "let range =" "? and ! require a symbol outcome with an :ok variant.")
example(7 no-success "${user_type}\n${classifier}\nmain() { let user = User { name: \"Bob\" }"
    "${code}" "}")
set(code "${block_8}")
replace_once(code "${read_signature}" "${read_signature} ${read_body}")
replace_once(code "${parse_signature}" "${parse_definition}")
example(8 declarations "${json_type}" "${code}" [=[
main() { readFile("missing") |> { :ok(_) => ()
  :notFound(path) => print(path)
  :permissionDenied(_) => () }
  parse("syntax") |> { :ok(_) => ()
    :syntax(line) => print(str(line)) } }
// expect: missing
// expect: 7
]=])
foreach(index IN ITEMS 9 10 12 13)
    example(${index} "accumulation-${index}" "${api}" "${block_${index}}" "${full_observer}")
endforeach()
set(code "${block_11}")
error_at(code "let text = readFile(path)?" "Propagated variants are not in the written return type.")
example(11 missing-errors "${api}" "${code}" "main() {}")
example(16 expanded-set "${user_type}" "type All = ${block_16}" "main() {}")

set(invalid_parse_signature "parse(text: str): Sym<:ok(Json), :invalid(int)>")
set(invalid_parse [=[{ if text == "parsebad" { ret :invalid(7) }
  Json { text: text } }]=])
set(validate_signature "validate(json: Json): Sym<:ok(User), :invalid(str)>")
set(validate_body [=[{ if json.text == "validatebad" { ret :invalid(json.text) }
  User { name: json.text } }]=])
set(validation_api "${json_type}\n${user_type}\n${invalid_parse_signature} ${invalid_parse}\n${validate_signature} ${validate_body}")
set(code "${block_18}")
replace_once(code "${invalid_parse_signature}" "")
replace_once(code "${validate_signature}" "")
error_at(code "load(text:" "Symbol tag has conflicting payload types.")
error_at(code "validate(json)?" "Symbol tag has conflicting payload types.")
example(18 payload-collision "${validation_api}" "${code}" "main() {}")
example(19 renamed-collision "${validation_api}" "${block_19}" [=[
observe(text: str) { load(text) |> { :ok(user) => print(user.name)
  :syntax(line) => print(str(line))
  :invalid(message) => print(message) } }
main() { observe("ok")
  observe("parsebad")
  observe("validatebad") }
// expect: ok
// expect: 7
// expect: validatebad
]=])
set(code "${block_20}")
replace_once(code "// avoid" "{ if path == \"bad\" { ret :err(path) }\nFile { fail: false } }")
example(20 generic-tag-is-legal "${file_type}" "${code}" [=[
main() { open("bad") |> { :ok(_) => ()
  :err(message) => print(message) } }
// expect: bad
]=])
set(code "${block_21}")
set(open_signature "open(path: str): Sym<:ok(File), :notFound(str), :permissionDenied(str)>")
replace_once(code "${open_signature}" "")
set(open_values ":ok(File { fail: false })" ":notFound(path)" ":permissionDenied(path)")
set(open_outputs "Opened" "No file at data.txt" "Cannot read data.txt")
foreach(i RANGE 0 2)
    list(GET open_values ${i} value)
    list(GET open_outputs ${i} output)
    set(open_api "${file_type}\nimpl File { ${open_signature} { ${value} } }")
    example(21 "specific-tags-${i}" "${open_api}\nmain() {" "${code}" "}\n// expect: ${output}")
endforeach()
foreach(index IN ITEMS 22 24)
    set(code "${block_${index}}")
    error_at(code "file.close()" "A call that can fail must be handled with ?, !, or a match.")
    example(${index} "unhandled-${index}" "${file_api}\nmain() { let file = File { fail: false }"
        "${code}" "}")
endforeach()
example(23 handled-success "${file_api}\nmain() { let file = File { fail: false }"
    "${block_23}" "}\n// expect: Closed")
string(FIND "${block_23}" "\n\n" split)
string(SUBSTRING "${block_23}" 0 ${split} bang)
math(EXPR split "${split} + 2")
string(SUBSTRING "${block_23}" ${split} -1 tail)
string(FIND "${tail}" "\n\n" split)
string(SUBSTRING "${tail}" 0 ${split} try)
math(EXPR split "${split} + 2")
string(SUBSTRING "${tail}" ${split} -1 match)
panic_example(23 handled-panic "${file_api}\nmain() { let file = File { fail: true }"
    "${bang}" "panic: :ioError(\"closed badly\")" "file.close()!")
example(23 handled-propagation "${file_api}\nmain() { let file = File { fail: true }"
    "${try}" "}\n// exit: 1\n// stderr: error: :ioError(\"closed badly\")")
example(23 handled-match "${file_api}\nmain() { let file = File { fail: true }"
    "${match}" "}\n// expect: closed badly")
example(25 infallible-print "main() {" "${block_25}" "}\n// expect: Hello")
set(code "${full_observer}")
replace_once(code "loadJson(path)" "loadOrEmpty(path)")
replace_once(code "    :notFound(_) => print(\"missing\")\n" "")
replace_once(code "// expect: missing" "// expect: {}")
example(26 removed-error "${api}" "${block_26}" "${code}")
set(constructor [=[
impl User { new(name: str, age: int): Sym<:ok(User), :invalidAge(int)> {
  if age < 0 { ret :invalidAge(age) }
  User { name: name }
} }
]=])
set(code "${block_28}")
error_at(code "User.new(\"Bob\", 20)?" "Propagated variants are not in the written return type.")
example(28 no-widening-errors "${user_type}\n${constructor}" "${code}" "main() {}")
example(29 plain-abi "" "${block_29}" "main() { print(str(double(3))) }\n// expect: 6")
example(30 success-as-symbol "${block_29}\nmain() {" "${block_30}"
    "print(str(y))\n}\n// expect: 6\n// expect: 7")
string(FIND "${block_32}" "\n\n" split)
string(SUBSTRING "${block_32}" 0 ${split} bad)
math(EXPR split "${split} + 2")
string(SUBSTRING "${block_32}" ${split} -1 good)
error_at(bad "path |> readFile()? |> parse()" "A call that can fail must be handled with ?, !, or a match.")
example(32 unhandled-tail "${api}" "${bad}" "main() {}")
example(32 handled-tail "${api}" "${good}" "${full_observer}")
example(34 ordinary-data "${user_type}\n${classifier}" "${block_34}" [=[
show(value: Sym<:adult, :senior>) { value |> { :adult => print("adult")
  :senior => print("senior") } }
main() { let user = User { name: "Bob" }
  describe(user)
  let result = describe(user)
  show(result)
  describe(user) }
// expect: adult
]=])

set(all_type "Sym<:ok(User), :notFound(str), :permissionDenied(str), :syntax(int), :missingKey(str), :wrongType(str), :invalidAge(int)>")
set(values ":ok(User { name: \"Bob\" })" ":notFound(\"bob.json\")" ":permissionDenied(\"bob.json\")" ":syntax(7)" ":missingKey(\"name\")" ":wrongType(\"age\")" ":invalidAge(20)")
set(outputs "Bob" "Missing file bob.json" "Cannot read bob.json" "The file is not valid JSON" "Missing field name" "Wrong type for field age" "Age is not allowed")
foreach(i RANGE 0 6)
    list(GET values ${i} value)
    list(GET outputs ${i} output)
    example(36 "seven-outcomes-${i}" "${user_type}\nloadUser(path: str): ${all_type} { ${value} }\nmain() {"
        "${block_36}" "}\n// expect: ${output}")
endforeach()
string(REGEX MATCHALL "  :[^\n]+\n" arms "${block_36}")
list(LENGTH arms arm_count)
if(NOT arm_count EQUAL 7)
    message(FATAL_ERROR "Update the seven-outcome match adapter")
endif()
set(i 0)
foreach(arm IN LISTS arms)
    set(code "${block_36}")
    replace_once(code "${arm}" "")
    error_at(code "loadUser(\"bob.json\") |> {" "Match is not exhaustive.")
    example(36 "missing-arm-${i}" "${user_type}\nloadUser(path: str): ${all_type} { :syntax(7) }\nmain() {"
        "${code}" "}")
    math(EXPR i "${i} + 1")
endforeach()
set(code "${block_36}")
error_at(code "loadUser(\"bob.json\") |> {" "Match is not exhaustive.")
example(36 added-variant "${user_type}\nloadUser(path: str): Sym<${all_type}, :extra> { :extra }\nmain() {"
    "${code}" "}")

# Every fence is explicitly checked or excluded; a future example cannot silently
# turn into an untested acceptance claim. Module/collection rules have separate
# first-version counterparts in test_symbols/test_errors and program fixtures.
set(deferred 5 14 15 17 27 31 33 35)
get_property(checked GLOBAL PROPERTY checked_fences)
list(REMOVE_DUPLICATES checked)
foreach(index RANGE 1 36)
    if(index IN_LIST checked AND index IN_LIST deferred)
        message(FATAL_ERROR "Fence ${index} is both tested and deferred")
    elseif(NOT index IN_LIST checked AND NOT index IN_LIST deferred)
        message(FATAL_ERROR "Fence ${index} has no scope classification")
    endif()
endforeach()
set(options)
if(DEBUG)
    list(APPEND options --debug)
endif()
execute_process(COMMAND "${RUNNER}" --brief "${BRIEF}" --cc "${CC}"
    --programs "${TEST_DIR}/programs" ${options}
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors)
if(NOT status EQUAL 0)
    message(FATAL_ERROR "Error-document acceptance failed (${status}):\n${output}\n${errors}")
endif()
get_property(cases GLOBAL PROPERTY example_cases)
list(LENGTH cases case_count)
list(LENGTH checked checked_count)
message(STATUS "${checked_count}/36 fences checked in ${case_count} cases; 8 later-version fences deferred\n${output}")
