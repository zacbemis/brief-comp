cmake_minimum_required(VERSION 3.16)
include("${CMAKE_CURRENT_LIST_DIR}/doc_native.cmake")

native_begin(lang-functions 27 218e30918ab5dbed1e1d7e568705e81e9f9b7e54ba2ad8d553ac101dc4a9433a)
native_fences(run "Complete functions, unchanged, with entry observers" "1;2;10;11")
native_fences(reject "Complete functions reject their documented return contract on the original fence line" "15;16")
native_fences(later "Function values, Fn types, lambdas or capture rules; unsupported syntax is not proof of documented capture/type errors" "3;4;5;6;7")
native_fences(later "Collection iteration and unavailable users/getAge API" "8")
native_fences(fragment "Three alternative incomplete headers; no body completion or extraction" "9")
native_fences(context "Unavailable File/Json/User APIs and qualified error sets; no local stand-ins, including the intended success-type error in 14" "12;13;14")
native_fences(later "Trait requirements/default bodies and unavailable User type" "17")
native_fences(context "Missing User/UserCollection types, fields and methods; return/unit prose rules are checked independently, not by supplying APIs to these fences" "18;19;20;21")
native_fences(n2 "Alternative closeLog definitions need File API, kind-based outcomes and handling; no splitting, stubbing or N2 acceptance" "22")
native_fences(context "Missing users.len API; unused plain values are checked separately" "23")
native_fences(n2 "Missing file.close API plus kind-based outcome handling; unknown-name rejection is not the documented error" "24")
native_fences(context "Missing user/getName/save context; no receiver or module API invented" "25")
native_fences(context "Report/User/module dependencies; summary is not rebound to a local mock" "26")
native_fences(context "Mixed method/module alternatives need Report/User context; no extracted or local stand-in calls" "27")

native_run(1 written-return [=[
  let zero: int = add(0, 0)
  let sum: int = add(2, 4)
  print(str(zero))
  print(str(sum))
  print(str(add(-2, 4)))
]=] [=[// expect: 0
// expect: 6
// expect: 2]=])
native_run(2 explicit-return [=[
  print(str(validate(-1)))
  print(str(validate(17)))
  print(str(validate(18)))
  print(str(validate(19)))
]=] [=[// expect: false
// expect: false
// expect: true
// expect: true]=])
native_run(10 inferred-number [=[
  let negative: int = double(-3)
  let zero: int = double(0)
  let positive: int = double(3)
  print(str(negative))
  print(str(zero))
  print(str(positive))
]=] [=[// expect: -6
// expect: 0
// expect: 6]=])
native_run(11 inferred-data [=[
  describe(1)
  print(describe(17) |> { :minor => "minor"
                        :adult => "adult" })
  print(describe(18) |> { :minor => "minor"
                        :adult => "adult" })
]=] [=[// expect: minor
// expect: adult]=])
native_reject(15 wrong-written-return "Function body does not match the written return type." 1)
native_reject(16 undeclared-return "Return value does not match the written return type." 3)

# Independently authored prose regressions, NOT completed/rewritten fences.
native_prose(prose-inference [=[
pub choose(b: bool) { if b { ret 1 }
  2
}
count(n: int) -> int { if n == 0 { ret 0 }
  count(n - 1) + 1
}
discard() -> () { "owned " + "tail" }
log() { print("unit") }
main() {
  let a: int = choose(true)
  let b: int = choose(false)
  let done: () = discard()
  let inferred: () = log()
  print(str(a + b))
  print(str(count(5)))
}
// expect: unit
// expect: 3
// expect: 5
]=])
native_prose(prose-partial-return [=[
pub choose(b: bool) -> :ok[int] | _ {
  if b { ret :!bad["owned " + "reason"] }
  7
}
main() {
  choose(false) |> { :ok[n] => print(str(n))
                    :!bad[why] => print(why) }
  choose(true) |> { :ok[n] => print(str(n))
                   :!bad[why] => print(why) }
}
// expect: 7
// expect: owned reason
]=])
native_prose(prose-receivers [=[
Counter {
  value: int
  new(value: int) { Counter { value = value } }
  bump(&self) { self.value += 1 }
  read() { self.value }
}
twice(value: int) { value * 2 }
main() {
  var counter = Counter.new(1)
  let done: () = counter.bump()
  print(str(counter.read()))
  print(str(twice(counter.read())))
  print(str(counter.read() |> twice()))
}
// expect: 2
// expect: 4
// expect: 4
]=])
native_prose(prose-immutable-parameter [=[
Box { value: int }
replace(value: int) {
  value = 2 // error: Cannot reassign an immutable binding.
}
replaceBorrow(value: &Box) {
  value = Box { value = 2 } // error: Cannot reassign an immutable binding.
}
main() {}
]=])
native_prose(prose-readonly-parameter [=[
Box { value: int }
mutate(box: Box) {
  box.value = 2 // error: Cannot mutate through an ordinary parameter; use &.
}
main() {}
]=])
native_prose(prose-unanchored-recursion [=[
cycle() { cycle() } // error: Cannot infer recursive return type; write a return type.
main() {}
]=])
native_prose(prose-evaluation-order [=[
State { value: int }
step(state: &State, digit: int) -> int {
  state.value = state.value * 10 + digit
  digit
}
combine(a: int, b: int) { a + b }
flag(state: &State, digit: int) {
  step(state, digit)
  true
}
target(state: &State) { step(state, 1)
  state
}
right(state: &State) { step(state, 2)
  state.value = 100
  3
}
main() {
  var state = State { value = 0 }
  print(str(combine(step(state, 1), step(state, 2))))
  print(str(state.value))
  print(str(step(state, 3) + step(state, 4)))
  print(str(state.value))
  let skipAnd = false and flag(state, 5)
  let skipOr = true or flag(state, 6)
  print(str(state.value))
  state.value = 0
  target(state).value += right(state)
  print(str(state.value))
}
// expect: 3
// expect: 12
// expect: 7
// expect: 1234
// expect: 1234
// expect: 4
]=])

native_prose(prose-wrong-way-calls [=[
Counter {
  value: int
  new() { Counter { value = 0 } }
  read() { self.value }
}
main() {
  let counter = Counter.new()
  Counter.read(counter) // error: Methods are called on a value, not the type. // fix: counter.read()
  counter.new() // error: Associated functions are called on the type, not a value. // fix: Counter.new()
}
]=])

native_finish(12 2 21 8)
native_execute()
