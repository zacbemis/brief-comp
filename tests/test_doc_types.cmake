# M6 type-document acceptance. See doc-types.md for scope/adapters.
cmake_minimum_required(VERSION 3.16)
include("${CMAKE_CURRENT_LIST_DIR}/doc_examples.cmake")
doc_examples_begin(lang-types 24)

# The document's setter takes shrt, so this nominal stand-in's age is shrt too.
set(user_api [=[
User { name: str
  age: shrt }
impl User { new(name: str, age: shrt): Sym<:ok(User), :invalidAge(shrt)> {
  if age < 0 { ret :invalidAge(age) }
  User { name: name + "", age: age }
} }
]=])
set(set_age [=[
setAge(user: &User, age: shrt) {
  user.age = age
}
]=])
set(code "${block_1}")
replace_once(code "${set_age}\n" "")
example(1 literal-context "${user_api}\n${set_age}\nmain() { let user = User.new(\"Bob\", 1)!"
    "${code}" "print(str(count))\nprint(str(ratio))\nprint(str(user.age))\nprint(str(total))\n}\n// expect: 10\n// expect: 0.5\n// expect: 20\n// expect: 11")
example(2 literal-defaults "acceptInt(n: int) { n }\nacceptDub(n: dub) { n }\nmain() {" "${block_2}"
    "print(str(acceptInt(count)))\nprint(str(acceptDub(ratio)))\n}\n// expect: 10\n// expect: 0.5")
set(code "${block_4}")
error_at(code "let big:" "Binding value does not match its annotation.")
error_at(code "let sum" "Operator operands must have the same type; numeric conversions are explicit.")
error_at(code "let whole:" "Binding value does not match its annotation.")
example(4 implicit-conversions "main() {" "${code}" "}")
set(code "${block_5}")
# These are alternative spellings of label. Only rename the second binding so
# both original expressions can run in one scope.
replace_once(code "let label = count |> str()" "let pipedLabel = count |> str()")
example(5 infallible-conversions "main() { let small: int = 10\nlet count: lng = 20\nlet ratio: flt = 0.5"
    "${code}" "print(str(big))\nprint(str(total))\nprint(label)\nprint(pipedLabel)\nprint(str(precise))\n}\n// expect: 10\n// expect: 10\n// expect: 20\n// expect: 20\n// expect: 0.5")
example(6 fallible-conversions "convert(text: str, big: lng) { let count = 5" "${block_6}" [=[
print(str(age) + ":" + str(small))
}
observe(text: str, big: lng) { convert(text, big) |> {
  :ok => print("done")
  :invalid(original) => print("invalid:" + original)
  :overflow => print("overflow")
} }
main() { observe("2" + "0", 10)
  observe("bad" + " text", 10)
  observe("20", 4294967296) }
// expect: 5
// expect: 20:10
// expect: done
// expect: invalid:bad text
// expect: overflow
]=])
string(STRIP "${block_7}" alternatives)
replace_once(alternatives "\n\n" "\n\n")
string(FIND "${alternatives}" "\n\n" split)
string(SUBSTRING "${alternatives}" 0 ${split} first)
math(EXPR split "${split} + 2")
string(SUBSTRING "${alternatives}" ${split} -1 second)
panic_example(7 signed-overflow "main() {" "${first}" "integer overflow" "count += 1")
panic_example(7 unsigned-underflow "main() {" "${second}" "integer overflow" "index -= 1")
string(STRIP "${block_9}" divisions)
string(REPLACE "\n" ";" divisions "${divisions}")
list(LENGTH divisions division_count)
if(NOT division_count EQUAL 3)
    message(FATAL_ERROR "Update the three division/remainder expressions")
endif()
set(code "")
foreach(line IN LISTS divisions)
    string(FIND "${line}" "//" at)
    if(at LESS 0)
        message(FATAL_ERROR "Missing division example comment")
    endif()
    string(SUBSTRING "${line}" 0 ${at} expr)
    string(SUBSTRING "${line}" ${at} -1 comment)
    string(STRIP "${expr}" expr)
    string(APPEND code "print(str(${expr})) ${comment}\n")
endforeach()
example(9 signed-division "main() {" "${code}" "}\n// expect: 3\n// expect: -3\n// expect: -1")
panic_example(10 division-zero "main() { let total = 7\nlet count = 0" "${block_10}" "division by zero" "total / count")
set(code "${block_10}")
replace_once(code "total / count" "total % count")
panic_example(10 remainder-zero "main() { let total = 7\nlet count = 0" "${code}" "division by zero" "total % count")
example(11 string-concatenation "main() {" "${block_11}" [=[
print(greeting)
print(name)
var changed = greeting
changed += "!"
print(greeting)
print(changed)
}
// expect: Hello Bob
// expect: Bob
// expect: Hello Bob
// expect: Hello Bob!
]=])
example(12 shared-string "main() {" "${block_12}" [=[
var changed = other
changed += "!"
print(name)
print(other)
print(changed)
}
// expect: Bob
// expect: Bob
// expect: Bob!
]=])
example(13 escaped-newline "main() {" "${block_13}" "print(message)\n}\n// expect: first\n// expect: second")
set(code "${block_14}")
replace_once(code "\"abc\"     // 3 bytes" "print(\"abc\")     // 3 bytes")
replace_once(code "\"é\"       // 2 bytes" "print(\"é\")       // 2 bytes")
example(14 character-and-utf8 "main() {" "${code}"
    "print(str(letter == 'a'))\n}\n// expect: abc\n// expect: é\n// expect: true")
set(slot_api [=[
UserSlot { user: User }
UserCollection { userVec: UserSlot }
impl UserSlot { add(slot: &UserSlot, user: &User): str {
  slot.user = user
  "stored:" + user.name
} }
]=])
example(15 written-unit "${user_api}\n${slot_api}" "${block_15}" [=[
main() { let user = User.new("B" + "ob", 20)!
  let slot = UserSlot { user: User.new("old", 1)! }
  let collection = UserCollection { userVec: slot }
  add(collection, user) |> { :ok => print("unit") }
  slot.user.age = 21
  print(user.name + ":" + str(user.age)) }
// expect: unit
// expect: Bob:21
]=])
set(code "${block_20}")
replace_once(code "same(a, b)        // false: different objects\n" "")
replace_once(code "same(a, c)        // true: the same object\n" "")
replace_once(code "a == b            // true: equal contents" "print(str(a == b)) // true: equal contents")
replace_once(code "a == c            // true" "print(str(a == c)) // true")
example(20 content-equality "${user_api}\nmain() {" "${code}" [=[
b.age = 21
print(str(a == b))
print(str(a == c))
}
// expect: true
// expect: true
// expect: false
// expect: true
]=])
set(code "${block_21}")
replace_once(code "same(nodeA, nodeB)   // allowed\n" "")
replace_once(code "\nnodeA == nodeB" "\ncompare(nodeA: Node, nodeB: Node) {\nnodeA == nodeB")
error_at(code "nodeA == nodeB" "Types that can contain a reference cycle have no == or !=.")
example(21 recursive-equality "" "${code}" "}\nmain() {}")

# Guard selected first-version prose rules in the actual chapter. Existing
# exhaustive/random stage and runtime suites stay independent of these examples.
file(READ "${ROOT}/docs/lang-types.txt" document)
set(primitive_table [=[
| Type | Size | Meaning |
|---|---|---|
| `bool` | | `true` or `false` |
| `shrt` / `ushrt` | 16 bits | Short signed / unsigned integer |
| `int` / `uint` | 32 bits | Signed / unsigned integer |
| `lng` / `ulng` | 64 bits | Long signed / unsigned integer |
| `flt` | 32 bits | Floating-point number |
| `dub` | 64 bits | Double-precision floating-point number |
| `chr` | 8 bits | One byte of text |
| `str` | | Immutable text |
| `()` | | The empty type |
]=])
set(escape_table [=[
| Escape | Meaning |
|---|---|
| `\n` | Newline |
| `\t` | Tab |
| `\\` | Backslash |
| `\"` | Double quote |
| `\'` | Single quote |
| `\0` | Zero byte |
]=])
replace_once(document "${primitive_table}" "${primitive_table}")
replace_once(document "${escape_table}" "${escape_table}")
string(REGEX REPLACE "[\r\n ]+" " " prose "${document}")
function(require_prose text)
    string(FIND "${prose}" "${text}" at)
    if(at LESS 0)
        message(FATAL_ERROR "Missing type-rule anchor: ${text}")
    endif()
endfunction()
require_prose("The sizes are the same on every platform.")
example(0 primitive-domains "" [=[
main() {
  let s: shrt = -32768
  let us: ushrt = 65535
  let i: int = -2147483648
  let ui: uint = 4294967295
  let l: lng = -9223372036854775808
  let ul: ulng = 18446744073709551615
  let f: flt = 1
  let d: dub = 1
  let c: chr = '\0'
  let b: bool = true
  let unit: () = ()
  print(str(s))
  print(str(us))
  print(str(i))
  print(str(ui))
  print(str(l))
  print(str(ul))
  print(str(f == 1.0 and d == 1.0 and c == '\0' and b and unit == ()))
}
// expect: -32768
// expect: 65535
// expect: -2147483648
// expect: 4294967295
// expect: -9223372036854775808
// expect: 18446744073709551615
// expect: true
]=] "")
require_prose("Floating literals round directly to their destination precision")
require_prose("Subnormal results and underflow to signed zero are allowed")
require_prose("A literal slightly larger than the largest finite value is allowed if it still rounds to that value.")
example(0 floating-literal-rounding "" [=[
main() {
  let direct: flt = 1.000000059604644776257986737988403547205962240695953369140625
  let tie: flt = 1.000000059604644775390625
  let beyond: flt = 340282356779733661637539395458142568447.0
  let tiny: flt = 0.0000000000000000000000000000000000000000000014
  let zero: flt = -0.000000000000000000000000000000000000000000000000000000000000000001
  print(str(direct == 1.00000011920928955078125))
  print(str(tie == 1.0))
  print(str(beyond == 340282346638528859811704183484516925440.0))
  print(str(tiny > 0.0))
  print(str(zero))
}
// expect: true
// expect: true
// expect: true
// expect: true
// expect: -0
]=] "")
require_prose("An integer literal outside its contextual integer type's range is a compile error")
set(code [=[
let s: shrt = 32768
let us: ushrt = -1
let i: int = 2147483648
let ui: uint = 4294967296
let l: lng = 9223372036854775808
let ul: ulng = 18446744073709551616
]=])
foreach(name IN ITEMS s us i ui l ul)
    error_at(code "let ${name}:" "Number literal is outside the range of its type.")
endforeach()
example(0 out-of-range-literals "main() {" "${code}" "}")
require_prose("A literal with a decimal point takes `flt` or `dub`, not an integer type.")
set(code "let value: int = 1.0")
error_at(code "let value" "Binding value does not match its annotation.")
example(0 decimal-integer-binding "main() {" "${code}" "}")
set(code "let value: flt = 340282356779733661637539395458142568448.0")
error_at(code "let value" "Number literal is outside the range of its type.")
example(0 floating-literal-overflow "main() {" "${code}" "}")
require_prose("This applies to every integer type and to every build of a program.")
foreach(type IN ITEMS ushrt int lng ulng)
    if(type STREQUAL "ushrt")
        set(maximum 65535)
    elseif(type STREQUAL "int")
        set(maximum 2147483647)
    elseif(type STREQUAL "lng")
        set(maximum 9223372036854775807)
    else()
        set(maximum 18446744073709551615)
    endif()
    panic_example(0 "overflow-${type}" "main() { var n: ${type} = ${maximum}"
        "n += 1" "integer overflow" "n += 1")
endforeach()
require_prose("Dividing a signed minimum by `-1` panics")
panic_example(0 signed-minimum-division "main() { let n: lng = -9223372036854775808"
    "n / -1" "integer overflow" "n / -1")
example(0 signed-minimum-remainder "" [=[
main() { let n: lng = -9223372036854775808
  print(str(n % -1)) }
// expect: 0
]=] "")
require_prose("a negative exponent panics")
panic_example(0 negative-integer-exponent "main() { let n = -1" "2 ** n" "negative integer exponent" "2 ** n")
require_prose("otherwise it panics, even when the value being shifted is zero.")
foreach(kind IN ITEMS negative width zero)
    if(kind STREQUAL "negative")
        set(expr "1 << -1")
    elseif(kind STREQUAL "width")
        set(expr "1 >> 32")
    else()
        set(expr "0 << 32")
    endif()
    panic_example(0 "shift-${kind}" "main() {" "${expr}" "invalid shift count" "${expr}")
endforeach()
require_prose("Signed right shift is arithmetic: it preserves the sign")
require_prose("Floating-point power accepts floating-point exponents, including negative ones.")
example(0 powers-and-shifts "" [=[
main() { print(str(0 ** 0))
  print(str(-3 >> 1))
  print(str(-3 << 1))
  let u: uint = 4294967295
  print(str(u >> 1))
  let f: flt = 2.0
  print(str(f ** -1.0)) }
// expect: 1
// expect: -2
// expect: -6
// expect: 2147483647
// expect: 0.5
]=] "")
require_prose("NaN compares unequal to every value, including itself.")
example(0 ieee-and-field-equality "" [=[
Box { value: dub }
main() {
  let inf: dub = 1.0 / 0.0
  let nan: dub = 0.0 / 0.0
  let zero: dub = -0.0
  let box = Box { value: nan }
  let maximum: flt = 340282346638528859811704183484516925440.0
  print(str(inf))
  print(str(1.0 / zero))
  print(str(nan))
  print(str(nan == nan))
  print(str(box == box))
  print(str(maximum * 2.0))
}
// expect: inf
// expect: -inf
// expect: nan
// expect: false
// expect: false
// expect: inf
]=] "")
require_prose("Every integer-to-floating conversion always succeeds")
require_prose("fails only if a finite input would round to infinity.")
example(0 numeric-domains "" [=[
narrow(n: dub) { flt(n) |> {
  :ok(value) => print("ok:" + str(value == 340282346638528859811704183484516925440.0 or value == -340282346638528859811704183484516925440.0))
  :overflow => print("overflow")
} }
integer(n: dub) { int(n) |> {
  :ok(value) => print("int:" + str(value))
  :overflow => print("overflow")
} }
main() {
  let signed: shrt = -1
  let unsigned: uint = 4294967295
  print(str(int(signed)))
  print(str(lng(unsigned)))
  let source: ulng = 16777217
  let rounded: flt = flt(source)
  print(str(rounded == 16777216.0))
  let f: flt = 0.5
  let d: dub = dub(f)
  print(str(d))
  narrow(340282350000000000000000000000000000000.0)
  narrow(340282356779733661637539395458142568448.0)
  narrow(-340282350000000000000000000000000000000.0)
  narrow(-340282356779733661637539395458142568448.0)
  print(str(flt(dub("-1e-100")!)!))
  print(str(flt(dub("inf")!)!))
  print(str(flt(dub("nan")!)!))
  integer(-0.0)
  print(str(uint(-0.0)!))
  integer(2.0)
  integer(2.5)
  integer(dub("inf")!)
  integer(dub("nan")!)
  let n: int = -1
  uint(n) |> { :ok(_) => print("unexpected")
    :overflow => print("overflow") }
}
// expect: -1
// expect: 4294967295
// expect: true
// expect: 0.5
// expect: ok:true
// expect: overflow
// expect: ok:true
// expect: overflow
// expect: -0
// expect: inf
// expect: nan
// expect: int:0
// expect: 0
// expect: int:2
// expect: overflow
// expect: overflow
// expect: overflow
// expect: overflow
]=] "")
require_prose("String-to-number conversions return `Sym<:ok(Target), :invalid(str)>`")
foreach(type IN ITEMS shrt ushrt int uint lng ulng flt dub)
    if(type STREQUAL "shrt")
        set(text -32768)
    elseif(type STREQUAL "ushrt")
        set(text 65535)
    elseif(type STREQUAL "int")
        set(text -2147483648)
    elseif(type STREQUAL "uint")
        set(text 4294967295)
    elseif(type STREQUAL "lng")
        set(text -9223372036854775808)
    elseif(type STREQUAL "ulng")
        set(text 18446744073709551615)
    else()
        set(text 0.5)
    endif()
    example(0 "text-${type}" "main() {" "${type}(\"${text}\") |> {\n:ok(value) => print(str(value))\n:invalid(_) => print(\"unexpected\")\n}"
        "}\n// expect: ${text}")
endforeach()
require_prose("Parsing consumes the whole byte string, with no whitespace trimming.")
require_prose("unsigned targets accept `-0` but not negative nonzero values.")
example(0 integer-text-grammar "" [=[
read(text: str) { int(text) |> {
  :ok(n) => print("int:" + str(n))
  :invalid(original) => print("invalid:" + str(original == text))
} }
unsigned(text: str) { uint(text) |> {
  :ok(n) => print("uint:" + str(n))
  :invalid(original) => print("invalid:" + str(original == text))
} }
main() {
  read("+0002")
  read(" 1")
  read("1 ")
  read("1\0" + "x")
  read("0x10")
  read("1_0")
  read("")
  read("2147483648")
  unsigned("-0")
  unsigned("-1")
}
// expect: int:2
// expect: invalid:true
// expect: invalid:true
// expect: invalid:true
// expect: invalid:true
// expect: invalid:true
// expect: invalid:true
// expect: invalid:true
// expect: uint:0
// expect: invalid:true
]=] "")
require_prose("Lowercase `inf` and `nan`, with an optional sign, are also accepted.")
example(0 floating-text-grammar "" [=[
read(text: str) { dub(text) |> {
  :ok(n) => print(str(n))
  :invalid(original) => print("invalid:" + str(original == text))
} }
main() { read("1e2")
  read(".5")
  read("1.")
  read("-0")
  read("+inf")
  read("-nan")
  read("1e")
  read("1e9999")
  read("0x1p2")
  read("1\0" + "0")
  read(" 1") }
// expect: 100
// expect: 0.5
// expect: 1
// expect: -0
// expect: inf
// expect: nan
// expect: invalid:true
// expect: invalid:true
// expect: invalid:true
// expect: invalid:true
// expect: invalid:true
]=] "")
require_prose("Decimal text rounds directly to the target precision")
example(0 floating-text-rounding "" [=[
main() {
  let direct: flt = flt("1.000000059604644776257986737988403547205962240695953369140625")!
  let tiny: flt = flt("1e-45")!
  let zero: flt = flt("-1e-100")!
  print(str(direct == 1.00000011920928955078125))
  print(str(tiny > 0.0))
  print(str(zero))
  flt("1e100") |> { :ok(_) => print("unexpected")
    :invalid(original) => print(str(original == "1e100")) }
}
// expect: true
// expect: true
// expect: -0
// expect: true
]=] "")
require_prose("This text grammar does not add exponent syntax to source number literals.")
set(code "let value = 1e2")
error_at(code "let value" "Exponent notation is not supported in source number literals.")
example(0 source-exponent "main() {" "${code}" "}")
require_prose("A direct numeric literal argument (including parentheses or a leading minus) takes the converter's target type")
example(0 conversion-literals "" [=[
main() {
  let a: shrt = shrt(1)
  let b: shrt = shrt((1))
  let c: shrt = shrt(-1)
  let d: shrt = 1 |> shrt()
  let wide: lng = lng(4294967296)
  print(str(a + b + c + d))
  print(str(wide))
  int(2.5) |> { :ok(_) => print("unexpected")
    :overflow => print("overflow") }
}
// expect: 2
// expect: 4294967296
// expect: overflow
]=] "")
require_prose("Typed values and computed expressions keep their source type")
foreach(kind IN ITEMS typed computed)
    if(kind STREQUAL "typed")
        set(code "shrt(n)")
    else()
        set(code "shrt(1 + 0)")
    endif()
    error_at(code "shrt(" "A call that can fail must be handled with ?, !, or a match.")
    example(0 "unhandled-${kind}" "main() { let n = 1" "${code}" "}")
endforeach()
require_prose("accept exactly one number, `chr`, or string.")
set(code [=[
lng()
dub(1, 2)
int(true)
int(user)
str()
str(1, 2)
str(user)
print(1)
]=])
error_at(code "lng()" "Numeric conversion expects exactly one number, chr or str argument.")
error_at(code "dub(1, 2)" "Numeric conversion expects exactly one number, chr or str argument.")
error_at(code "int(true)" "Numeric conversion expects a number, chr or str argument.")
error_at(code "int(user)" "Numeric conversion expects a number, chr or str argument.")
error_at(code "str()" "str expects exactly one number or bool argument.")
error_at(code "str(1, 2)" "str expects exactly one number or bool argument.")
error_at(code "str(user)" "str expects a number or bool argument.")
error_at(code "print(1)" "print expects a str argument.")
example(0 invalid-builtin-arguments "${user_api}\nmain() { let user = User.new(\"Bob\", 20)!" "${code}" "}")
require_prose("No `.0` is appended; signed zero becomes `-0`")
example(0 formatting "" [=[
main() { print(str(true))
  print(str(false))
  let f: flt = 0.1
  let d: dub = 0.1
  print(str(f))
  print(str(d))
  print(str(1.5))
  print(str(1.0))
  print(str(-0.0)) }
// expect: true
// expect: false
// expect: 0.1
// expect: 0.1
// expect: 1.5
// expect: 1
// expect: -0
]=] "")

require_prose("compare unsigned bytes lexicographically, including embedded zero bytes")
require_prose("No Unicode normalization or locale-dependent comparison is performed.")
example(0 byte-comparisons "" [=[
main() { print(str("a\0b" < "a\0c"))
  print(str("a" < "a\0"))
  print(str("é" > "z"))
  print(str("é" == "é"))
  print(str('a' > '\0')) }
// expect: true
// expect: true
// expect: true
// expect: false
// expect: true
]=] "")
require_prose("String and character literals accept these escapes:")
example(0 supported-escapes "" [=[
main() { print("line\n\t\\\"\'\0end")
  print(str('\n' > '\t' and '\\' != '\'' and '\"' != '\0')) }
// expect: line\n\t\\"'\0end
// expect: true
]=] "")
require_prose("Any other character after a backslash is a compile error.")
set(code [=[
let a = "\r"
let b = "\a"
let c = "\x"
let d = "\u"
let e = "\q"
let f = '\q'
]=])
foreach(name IN ITEMS a b c d e)
    error_at(code "let ${name}" "Unknown escape in string literal.")
endforeach()
error_at(code "let f" "Unknown escape in character literal.")
example(0 unsupported-escapes "main() {" "${code}" "}")
require_prose("A `chr` is one byte.")
set(code "let value: chr = 'é'")
error_at(code "let value" "A character literal must contain exactly one byte.")
example(0 multibyte-character "main() {" "${code}" "}")
require_prose("String and character literals must begin and end on the same source line.")
foreach(kind IN ITEMS string character)
    foreach(ending IN ITEMS lf crlf)
        if(kind STREQUAL "string")
            set(code "let value = \"unclosed\nlet good = 1")
            set(message "Unterminated string.")
        else()
            set(code "let value = 'x\nlet good = 1")
            set(message "Unterminated character.")
        endif()
        error_at(code "let value" "${message}")
        if(ending STREQUAL "crlf")
            string(REPLACE "\n" "\r\n" code "${code}")
        endif()
        example(0 "unterminated-${kind}-${ending}" "main() {" "${code}" "}")
    endforeach()
endforeach()
require_prose("`chr` and `bool` are not numeric types.")
set(code "'a' + 'b'")
error_at(code "'a'" "Arithmetic requires numbers.")
example(0 character-arithmetic "main() {" "${code}" "}")
require_prose("A `chr` converts to its unsigned byte value, 0 to 255, which every numeric type holds")
require_prose("arithmetic on characters requires an explicit numeric conversion, as in `int(letter) - int('a')`.")
example(0 character-conversions "" [=[
main() { let letter: chr = 'c'
  let nul: chr = '\0'
  let tilde: chr = '~'
  print(str(int(letter) - int('a')))
  print(str(int('a')))
  print(str(shrt(nul)) + ":" + str(ushrt(tilde)) + ":" + str(uint(tilde)))
  print(str(lng(tilde)) + ":" + str(ulng(tilde)))
  print(str(flt(tilde)) + ":" + str(dub(tilde)))
  print(str('a' |> int())) }
// expect: 2
// expect: 97
// expect: 0:126:126
// expect: 126:126
// expect: 126:126
// expect: 97
]=] "")
require_prose("It has exactly one value, also written `()`.")
example(0 unit-value "" [=[
UnitBox { value: () }
effect() { print("effect") }
ignore(): () { "owned" + " tail" }
main() {
  let box = UnitBox { value: effect() }
  let value: () = ignore()
  print(str(box.value == value and value == ()))
}
// expect: effect
// expect: true
]=] "")
require_prose("two structs with identical fields are still different types.")
set(code "let other: Other = user")
error_at(code "let other" "Binding value does not match its annotation.")
example(0 nominal-structs "${user_api}\nOther { name: str\n  age: shrt }\nmain() { let user = User.new(\"Bob\", 20)!" "${code}" "}")

# const, provisional checked/wrapping library APIs, named tuples, Fn, maps and
# collections are later-version work. Equality fences 20/21 defer only identity.
doc_examples_finish("3;8;16;17;18;19;22;23;24" "" 64)
