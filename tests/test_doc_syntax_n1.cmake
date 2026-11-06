cmake_minimum_required(VERSION 3.16)
include("${CMAKE_CURRENT_LIST_DIR}/doc_native.cmake")

native_begin(lang-syntax 35 52c72b9f8efc492e3426186b6bb7265453516d281337714480bff4bd0d54a816)
native_fences(run "Complete comments or while body, unchanged, with only entry/observer context" "1;27")
native_fences(reject "Written annotation error, before any unavailable API" "10")
native_fences(reject "Documented syntax error before lookup; other alternative expressions are not accepted natively by this rejection" "18;22;34")
native_fences(reject "Complete main with out-of-loop break, no appended duplicate entry point" "31")
native_fences(fragment "Mixed local binding/constant/struct contexts; no extraction or scope rewriting; casing tested separately" "2")
native_fences(fragment "Interleaved usage, missing APIs and ellipsis function body; no extraction or completion" "4")
native_fences(context "Missing User APIs or input context; const/collection dependencies also remain outside native scope" "3;5;6;8;9")
native_fences(context "Missing scalar operands; no invented bindings to make reference operator listings execute" "11;12;13;14;17;23")
native_fences(context "Missing age/user/method context; scalar logic and precedence are independent prose programs" "15;16")
native_fences(context "Missing count/functions/operands; no sum/double/f/g stand-ins" "19;20")
native_fences(context "Missing age and mixed comparison/match alternatives; no extracted grouped expression" "21")
native_fences(context "Missing age, or duplicate label alternatives; no inputs/splitting; prose checks branches and missing else" "24;25;26")
native_fences(later "N4 declared bindings, inclusive/typed loops, collection iteration or N5 interpolation; no partial extraction of supported lines" "7;28;29")
native_fences(later "Collection iteration and missing user/row APIs; break/continue semantics checked independently" "30;32;33")
native_fences(later "Generics, collections and indexing; no reinterpretation as native array access" "35")

native_run(1 comments "" "")
native_reject_body(10 annotation-mismatch "Binding value does not match its annotation." 1)
native_reject_body(18 comparison-chain "Comparison operators do not chain; use 'and' or parentheses." 1)
native_reject_body(22 pipeline-stage "A pipeline stage must be a call to a name or dotted path, or a match block." 1)
native_run_body(27 while-loop "print(str(count))" "// expect: 10")
native_reject_complete(31 outside-loop "break and continue are only allowed inside loops." 2)
native_reject_body(34 literal-header "Wrap struct literals in conditions or iterables in parentheses." 1)

# Independently authored complete programs, not substituted fence contexts.
native_prose(prose-precedence [=[
scale(value: int) { value * 2 }
main() {
  let a = 10
  let b = 3
  let c = 2
  print(str(a + b * c))
  print(str(a - b - c))
  print(str(2 ** 3 ** 2))
  print(str(-2 ** 2))
  print(str(7 & 1 == 0))
  print(str(1 | 2 ^ 3 & 4))
  print(str(3 |> scale() > 5))
  print(str((3 |> scale()) + 1))
  print(str(2 + 1 |> scale()))
  print(str(true or false and false))
  print(str(not true == false))
}
// expect: 16
// expect: 5
// expect: 512
// expect: -4
// expect: false
// expect: 3
// expect: true
// expect: 7
// expect: 6
// expect: true
// expect: true
]=])
native_prose(prose-continuations-control [=[
Point { value: int
  read() { self.value }
}
scale(value: int) { value * 2 }
combine(a: int, b: int) { a + b }
main() {
  let total = 1 +
    2 +
    3
  let point = Point { value = total }
  let value = point
    .read()
  let scaled = value
    |> scale()
  let pair = combine(
    total,
    scaled
  )
  let label = if pair > 10 { "big" } else { "small" }
  var count = 0
  while count < 5 {
    count += 1
  }
  var sum = 0
  for i in 0..5 {
    if i == 1 { continue }
    if i == 4 { break }
    sum += i
  }
  print(str(pair))
  print(label)
  print(str(count))
  print(str(sum))
}
// expect: 18
// expect: big
// expect: 5
// expect: 5
]=])
native_prose(prose-scope-and-ranges [=[
Tick { value: int }
bound(tick: &Tick, value: int) { tick.value = tick.value * 10 + value
  value
}
main() {
  let value = 4
  if true {
    let value = value + 1
    print(str(value))
  }
  print(str(value))
  var tick = Tick { value = 0 }
  var sum = 0
  for i in bound(tick, 1)..bound(tick, 4) { sum += i }
  print(str(sum))
  print(str(tick.value))
  for i in 4..1 { print("never") }
  var visits = 0
  for row in 0..3 {
    for column in 0..4 {
      if column == 1 { break }
      visits += 1
    }
  }
  print(str(visits))
}
// expect: 5
// expect: 4
// expect: 6
// expect: 14
// expect: 3
]=])
native_prose(prose-update-statements [=[
Cell { value: int }
main() {
  var x = 6
  var y = 4
  x++
  y--
  x += y
  x -= y
  x *= y
  x /= y
  x %= y
  var cell = Cell { value = 1 }
  cell.value++
  cell.value += x
  print(str(x))
  print(str(cell.value))
}
// expect: 1
// expect: 3
]=])
native_prose(prose-header-escapes [=[
Gate { allowed: bool
  allows() { self.allowed }
}
ready(gate: Gate) { gate.allowed }
main() {
  if (Gate { allowed = true }).allows() { print("grouped") }
  if ready(Gate { allowed = true }) { print("argument") }
}
// expect: grouped
// expect: argument
]=])
native_prose(prose-ordinary-retired-names [=[
Sym = int
Self {
  value: Sym
  new(value: Sym) -> Self { Self { value = value } }
  int() -> Sym { self.value }
}
type(value: Sym) { value }
impl(value: Sym) { type(value) }
trait(value: Sym) { impl(value) }
ovr(value: Sym) { trait(value) }
main() {
  let item = Self.new(ovr(3))
  print(str(item.int()))
  print(str(int('a')))
}
// expect: 3
// expect: 97
]=])
native_prose(prose-declaration-casing [=[
packet { value: int } // error: Type names must start with an uppercase letter. // fix: Packet { value: int }
ReadValue() { 0 } // error: Function names must start with a lowercase letter. // fix: readValue() { 0 }
Holder { Total: int } // error: Field names must start with a lowercase letter. // fix: Holder { total: int }
take(Input: int) { 0 } // error: Parameter names must start with a lowercase letter. // fix: take(input: int) { 0 }
choice = :red // error: Statements are only allowed inside a function.
const MAX_COUNT = 2 // error: Binding names must start with a lowercase letter. // fix: const maxCount = 2
ChoiceSet = :Bright // error: Symbol tags must start with a lowercase letter. // fix: ChoiceSet = :bright
main() {
  let TotalCount = 2 // error: Binding names must start with a lowercase letter. // fix: let totalCount = 2
  for Item in 0..2 {} // error: Loop variables must start with a lowercase letter. // fix: for item in 0..2 {}
  :Wrong // error: Symbol tags must start with a lowercase letter. // fix: :wrong
  color |> {
    :Other => () // error: Symbol tags must start with a lowercase letter. // fix: :other => ()
    _ => ()
  }
}
]=])
native_prose(prose-reserved-names [=[
main() {
  let int = 1 // error: Expected a binding name.
  let self = 1 // error: Expected a binding name.
  let _value = 1 // error: Names must start with a letter; '_' alone is a wildcard.
}
]=])
native_prose(prose-binding-rejections [=[
main() {
  let fixed = 1
  fixed = 2 // error: Cannot reassign an immutable binding.
  for index in 0..2 {
    index = 0 // error: Cannot reassign an immutable binding.
  }
  let copied = 1
  let copied = 2 // error: Duplicate binding in this scope.
  let missing = missing // error: Unknown binding.
}
]=])
native_prose(prose-statement-only [=[
main() {
  var count = 0
  let old = count++ // error: Increment and decrement are only allowed as standalone statements.
  print(count = 1) // error: Assignment is only allowed as a standalone, non-chaining statement, with a name, field, or index on the left.
}
]=])
native_prose(prose-old-spellings [=[
main() {
  true && false // error: Use 'and' instead of '&&'. // fix: true and false
  false || true // error: Use 'or' instead of '||'. // fix: false or true
  !true // error: Logical not is written 'not'. // fix: not true
  let number: dub = 0.5 // error: Double precision is written 'dbl', not 'dub'. // fix: let number: dbl = 0.5
}
]=])
native_prose(prose-missing-else [=[
main() {
  let age = 20
  let label = if age < 18 { "Minor" } // error: An if without else cannot be used as a value.
}
]=])

native_prose(prose-not-precedence [=[
main() {
  not 1 == true // error: Logical not requires bool.
}
]=])

native_finish(15 5 28 13)
native_execute()
