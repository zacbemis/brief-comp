cmake_minimum_required(VERSION 3.16)
include("${CMAKE_CURRENT_LIST_DIR}/doc_native.cmake")

native_begin(lang-pipelines 17 13457b99db06d2f6ab9c3b4e8968f042284039f6c56c56fbd28f728b73c96cbf)
native_fences(run "Complete scalar transformation body; only an entry and following observer are added" "13")
native_fences(fragment "Operator token, mixed precedence examples or isolated pattern arms; no splitting/completion" "1;11;16")
native_fences(context "Unbound inputs/functions or illustrative File/Json/User APIs; no replacements" "2;3;7;8;9;10;12;17")
native_fences(later "Collection APIs, lambdas, module lookup or missing users/predicate context" "4;5;6;15")
native_fences(later "Missing age/digit inputs plus N4 inclusive ranges; keep the entire fence" "14")

native_run_body(13 scalar-transformation "print(\"after transform\")" "// expect: after transform")

# Independent complete prose programs, not completions of listed examples.
native_prose(prose-order-and-arguments [=[
State { log: str }
start(state: &State) -> int { state.log = state.log + "L"
  3
}
argument(state: &State) -> int { state.log = state.log + "A"
  4
}
combine(n: int, extra: int, state: &State) -> int {
  state.log = state.log + "C"
  n * 10 + extra
}
finish(n: int, state: &State) -> int { state.log = state.log + "F"
  n + 1
}
main() {
  let state = State { log = "" }
  let value = start(state) |> combine(argument(state), state) |> finish(state)
  print(str(value))
  print(state.log)
  print(str(10 |> { x => x * 2 }))
}
// expect: 35
// expect: LACF
// expect: 20
]=])
native_prose(prose-method-lookup [=[
Box { value: int
  choose() -> int { self.value + 1 }
  plus(&self, n: int) { self.value += n }
  fixed(box: Box) -> int { box.value + 100 }
}
choose(box: Box) -> int { box.value + 10 }
summary(box: Box) -> int { box.value * 2 }
main() {
  let box = Box { value = 4 }
  print(str(box |> choose()))
  print(str(box |> summary()))
  print(str(box |> Box.fixed()))
  box |> plus(3)
  print(str(box.value))
}
// expect: 5
// expect: 8
// expect: 104
// expect: 7
]=])
native_prose(prose-fallible-stages [=[
First = :ok[str] | :!first[int]
Second = :ok[str] | :!second[str]
read(n: int) -> First {
  if n < 0 { ret :!first[n] }
  "owned " + str(n)
}
change(text: str, fail: bool) -> Second {
  if fail { ret :!second[text] }
  text + " changed"
}
written(n: int, fail: bool) -> :ok[str] | :!first[int] | :!second[str] {
  n |> read()? |> change(fail)
}
inferred(n: int, fail: bool) { n |> read()? |> change(fail) }
partial(n: int, fail: bool) -> :ok[str] | _ { n |> read()? |> change(fail) }
show(n: int, fail: bool) {
  let saved = written(n, fail)
  saved |> { :ok[text] => print(text)
             :!first[id] => print(str(id))
             :!second[text] => print(text) }
  inferred(n, fail) |> { :ok[text] => print(text)
                         :!first[id] => print(str(id))
                         :!second[text] => print(text) }
  partial(n, fail) |> { :ok[text] => print(text)
                        :!first[id] => print(str(id))
                        :!second[text] => print(text) }
}
main() { show(1, false)
  show(-1, false)
  show(2, true)
}
// expect: owned 1 changed
// expect: owned 1 changed
// expect: owned 1 changed
// expect: -1
// expect: -1
// expect: -1
// expect: owned 2
// expect: owned 2
// expect: owned 2
]=])
native_prose(prose-postfix-and-match [=[
Box { text: str }
create(n: int) -> :ok[Box] | :!bad[str] {
  if n < 0 { ret :!bad["bad"] }
  Box { text = "box " + str(n) }
}
text(box: Box) -> str { box.text }
show(n: int) -> str {
  n |> create() |> { :ok[box] => box |> text()
                     :!bad[why] => why }
}
main() {
  print(1 |> create()! |> text())
  print(2 |> create()? |> text())
  print(show(-1))
}
// expect: box 1
// expect: box 2
// expect: bad
]=])
native_prose(prose-stage-panic [=[
source(n: int) -> :ok[int] | :!bad[int] { :!bad[n] }
main() {
  9 |> source()!
}
// panic: panic: :!bad[9]
// panic: prose-stage-panic.brf:3
// exit: 1
]=])
native_prose(prose-precedence-and-short-circuit [=[
State { calls: int }
double(n: int) -> int { n * 2 }
flag(state: &State, value: bool) -> bool { state.calls += 1
  value
}
main() {
  print(str(3 + 1 |> double()))
  print(str(4 |> double() > 7))
  print((4 >= 3) |> { true => "yes"
                      false => "no" })
  let state = State { calls = 0 }
  let a = state |> flag(false) and state |> flag(true)
  let b = state |> flag(true) or state |> flag(false)
  print(str(a))
  print(str(b))
  print(str(state.calls))
}
// expect: 8
// expect: true
// expect: yes
// expect: false
// expect: true
// expect: 2
]=])
native_prose(prose-pattern-values [=[
Data = :pair[int, int] | :empty
area(data: Data) -> int { data |> { :pair[w, h] => w * h
                                  :empty => 0 } }
label(text: str) -> str { text |> { "a\0b" => "zero"
                                   "café" => "utf8"
                                   _ => "other" } }
age(n: int) -> str { n |> { < 18 => "minor"
                            18..100 => "adult"
                            >= 100 => "senior" } }
main() {
  print(str(area(:pair[3, 4])))
  print(str(area(:empty)))
  print(label("a\0" + "b"))
  print(label("a\0b!"))
  print(label("ca" + "fé"))
  print(age(17))
  print(age(18))
  print(age(100))
}
// expect: 12
// expect: 0
// expect: zero
// expect: other
// expect: utf8
// expect: minor
// expect: adult
// expect: senior
]=])
native_prose(prose-arm-jumps [=[
main() {
  var total = 0
  for n in 0..5 {
    n |> { 0 => continue
           3 => break
           _ => total += n }
  }
  print(str(total))
  let value = 7 |> { x => { let doubled = x * 2
                            doubled + 1 } }
  print(str(value))
}
// expect: 3
// expect: 15
]=])
native_prose(prose-missing-and-unreachable [=[
missing(n: int) { n |> { 18..100 => () // error: Match is not exhaustive.
                         >= 100 => () } }
shadowed(n: int) { n |> { 0..10 => ()
                          5 => () // error: Unreachable match arm.
                          _ => () } }
empty(n: int) { n |> { 5..5 => () // error: Unreachable match arm.
                       _ => () } }
strings(s: str) { s |> { "same" => () // error: Match is not exhaustive.
                         "other" => () } }
bools(b: bool) { b |> { true => () // error: Match is not exhaustive.
                        } }
main() {}
]=])
native_prose(prose-outcomes-not-automatic [=[
Result = :ok[int] | :!bad
source(n: int) -> Result { n }
take(r: Result) -> int { 0 }
wrong() {
  let value = 1 |> source() |> take() // error: A call that can fail must be handled with ?, !, or a match.
}
contract(n: int) -> int { // error: Function body does not match the written return type.
  n |> source()
}
main() {}
]=])

native_prose(prose-float-and-character-matches [=[
classify(n: dbl) -> str { n |> { < 0 => "negative"
                                0 => "zero"
                                > 0 => "positive"
                                _ => "nan" } }
letter(c: chr) -> str { c |> { 'a'..'z' => "lower"
                              'z' => "last"
                              _ => "other" } }
main() {
  print(classify(-1.0))
  print(classify(-0.0))
  print(classify(1.0))
  print(classify(0.0 / 0.0))
  print(letter('a'))
  print(letter('z'))
  print(letter('A'))
}
// expect: negative
// expect: zero
// expect: positive
// expect: nan
// expect: lower
// expect: last
// expect: other
]=])
native_prose(prose-coverage-corner-cases [=[
floating(n: dbl) { n |> { < 0 => () // error: Match is not exhaustive; add a catch-all for NaN.
                          >= 0 => () } }
zeros(n: dbl) { n |> { -0.0 => ()
                       0.0 => () // error: Unreachable match arm.
                       _ => () } }
decoded(s: str) { s |> { "'" => ()
                         "\'" => () // error: Unreachable match arm.
                         _ => () } }
Result = :first[int] | :second[int]
symbols(r: Result) { r |> { :first[v] => print(str(v)) // error: Match is not exhaustive.
                           } }
main() {}
]=])

native_finish(13 0 16 12)
native_execute()
