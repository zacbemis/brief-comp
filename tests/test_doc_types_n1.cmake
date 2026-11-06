cmake_minimum_required(VERSION 3.16)
include("${CMAKE_CURRENT_LIST_DIR}/doc_native.cmake")

native_begin(lang-types 28 e69d9ea64fadd75715b4a5de14441cde7106ca0b11c9debc1566216441de7bb1)
native_fences(run "Whole statement fences with only main/observer context; bare expressions are smoke checks, not rewritten value assertions" "2;9;11;12;14;15")
native_fences(run "Whole overflow fence: shrt panic is reached first; later uint panic is checked independently, not extracted" "7")
native_fences(reject "All three explicit numeric conversion errors, on their original fence lines plus the enclosing main offset" "4")
native_fences(context "Mixed declarations/usage need User and user; no helper type or body extraction" "1")
native_fences(later "Const and alternative duplicate declarations; not accepted as numeric literal inference" "3")
native_fences(context "Missing inputs and duplicate label alternatives; no invented values or splitting" "5")
native_fences(context "Missing text/big/count inputs; no stand-ins or N2 outcome-kind acceptance" "6")
native_fences(context "Unspecified library checkedAdd/wrappingMul APIs and missing operands; no local mocks" "8")
native_fences(context "Missing total/count and zero condition; standalone division cases are prose, not fence completion" "10")
native_fences(later "N5 interpolation/brace escape and missing user context; not treated as ordinary strings" "13")
native_fences(context "Missing UserCollection/User API; no unit-return stand-in" "16")
native_fences(later "Named tuples are on hold" "17;18;19")
native_fences(later "Fn types and unavailable User" "20")
native_fences(context "Missing User.new and planned same API; no invented constructors or identity helper" "21")
native_fences(context "Mixed Node declaration and missing nodeA/nodeB/same usage; cyclic equality is checked by separate prose" "22")
native_fences(later "Maps, arrays, vectors, indexing or collection iteration; unknown APIs do not prove unfilled-array or index errors; 28 also needs N5" "23;24;25;26;27;28")

native_run_body(2 default-literals [=[
  let integer: int = count
  let decimal: dbl = ratio
  print(str(integer))
  print(str(decimal))
]=] [=[// expect: 10
// expect: 0.5]=])
native_reject_body(4 no-implicit-conversions
    "Binding value does not match its annotation." 2
    "Operator operands must have the same type; numeric conversions are explicit." 4
    "Binding value does not match its annotation." 7)
native_run_body(7 first-overflow "" [=[// panic: panic: integer overflow
// panic: first-overflow.brf:3
// exit: 1]=])
native_run_body(9 division-statements "" "")
native_run_body(11 concatenation [=[
  print(name)
  print(greeting)
]=] [=[// expect: Bob
// expect: Hello Bob]=])
native_run_body(12 sharing [=[
  print(name)
  print(other)
]=] [=[// expect: Bob
// expect: Bob]=])
native_run_body(14 escaped-newline "print(message)" [=[// expect: first
// expect: second]=])
native_run_body(15 character-and-utf8 [=[
  let byte: chr = letter
  print(str(byte == 'a'))
  print(str(int(byte)))
]=] [=[// expect: true
// expect: 97]=])

# Complete independently authored programs, not filled-in document fragments.
native_prose(prose-context-and-conversions [=[
main() {
  let wide: lng = 4294967296
  let minimum: shrt = -32768
  let half: flt = 0.5
  let direct: shrt = shrt(1)
  let piped: shrt = 1 |> shrt()
  let target: lng = lng(4294967296)
  let typed: int = 9
  let widened: lng = typed |> lng()
  let large: lng = 9223372036854775807
  let rounded: flt = flt(large)
  print(str(wide))
  print(str(minimum))
  print(str(half))
  print(str(direct + piped))
  print(str(target))
  print(str(widened))
  print(str(rounded > 0))
}
// expect: 4294967296
// expect: -32768
// expect: 0.5
// expect: 2
// expect: 4294967296
// expect: 9
// expect: true
]=])
native_prose(prose-conversion-failures [=[
main() {
  int("42") |> { :ok[n] => print(str(n))
                 :!invalid[text] => print(text) }
  int("bad\0" + "text") |> { :ok[n] => print(str(n))
                            :!invalid[text] => print(text) }
  let tooBig: lng = 2147483648
  int(tooBig) |> { :ok[n] => print(str(n))
                  :!overflow => print("overflow") }
  int(2.5) |> { :ok[n] => print(str(n))
               :!overflow => print("fraction") }
}
// expect: 42
// expect: bad\0text
// expect: overflow
// expect: fraction
]=])
native_prose(prose-floating [=[
main() {
  let zero: dbl = -0.0
  print(str(zero))
  print(str(1.0 / zero))
  let nan: dbl = 0.0 / 0.0
  print(str(nan != nan))
  flt("-0") |> { :ok[n] => print(str(n))
                 :!invalid[text] => print(text) }
  let wide: dbl = 0.5
  flt(wide) |> { :ok[n] => print(str(n))
                :!overflow => print("overflow") }
}
// expect: -0
// expect: -inf
// expect: true
// expect: -0
// expect: 0.5
]=])
native_prose(prose-integer-arithmetic [=[
main() {
  print(str(7 / 2))
  print(str(-7 / 2))
  print(str(-7 % 2))
  let minimum: int = -2147483648
  print(str(minimum % -1))
  print(str(0 ** 0))
  print(str(2 ** 5))
  print(str(-3 >> 1))
}
// expect: 3
// expect: -3
// expect: -1
// expect: 0
// expect: 1
// expect: 32
// expect: -2
]=])
native_prose(prose-owned-strings [=[
main() {
  var name = "owned " + "text"
  let other = name
  name += "!"
  print(other)
  print(name)
  print(str("a\0b" < "a\0c"))
  print(str("a" < "a\0"))
  print(str("é" > "z"))
  print(str('a' < 'b'))
}
// expect: owned text
// expect: owned text!
// expect: true
// expect: true
// expect: true
// expect: true
]=])
native_prose(prose-struct-equality [=[
Record { number: int
  text: str
}
main() {
  let a = Record { number = 1, text = "own" + "ed" }
  var b = Record { number = 1, text = "owned" }
  print(str(a == b))
  let alias = a
  print(str(alias == a))
  b.number = 2
  print(str(a != b))
}
// expect: true
// expect: true
// expect: true
]=])
native_prose(prose-cyclic-equality [=[
Node { value: int
  next: :none | :more[Node]
}
main() {
  let a = Node { value = 1, next = :none }
  let b = Node { value = 1, next = :none }
  a == b // error: Types that can contain a reference cycle have no == or !=.
}
]=])
native_prose(prose-literal-rejections [=[
main() {
  shrt(70000) // error: Number literal is outside the range of its type.
  uint(-1) // error: Number literal is outside the range of its type.
  let tiny: shrt = 32768 // error: Number literal is outside the range of its type.
}
]=])
native_prose(prose-uint-underflow [=[
main() {
  var count: uint = 0
  count -= 1
}
// panic: panic: integer overflow
// panic: prose-uint-underflow.brf:3
// exit: 1
]=])
native_prose(prose-minimum-division [=[
main() {
  let minimum: int = -2147483648
  print(str(minimum / -1))
}
// panic: panic: integer overflow
// panic: prose-minimum-division.brf:3
// exit: 1
]=])
native_prose(prose-division-zero [=[
main() {
  let zero = 0
  print(str(1 / zero))
}
// panic: panic: division by zero
// panic: prose-division-zero.brf:3
// exit: 1
]=])
native_prose(prose-remainder-zero [=[
main() {
  let zero = 0
  print(str(1 % zero))
}
// panic: panic: division by zero
// panic: prose-remainder-zero.brf:3
// exit: 1
]=])

native_finish(19 1 20 12)
native_execute()
