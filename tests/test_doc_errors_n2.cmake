cmake_minimum_required(VERSION 3.16)
include("${CMAKE_CURRENT_LIST_DIR}/doc_native.cmake")

native_begin(lang-errors 38 923a522e9545d757273600d34145ee5472a1a98e43b6e3aac5a64bfb45557913)
native_fences(run "Complete Health alias is ordinary data; entry observers do not add an API" "5")
native_fences(fragment "Standalone signatures, alternatives or interleaved declaration/usage; no body completion, splitting or relocation" "1;4;6;7;9;15;20;21;22;23;31;33")
native_fences(context "Missing parse/readFile/Json inputs or APIs; no invented implementations, including the intended omitted-error rejection" "2;3;10;11;12;13;14")
native_fences(context "Missing module/type/API context; qualified identity and composition require the bootstrap loader" "16;17;18;19;32;37;38")
native_fences(later "Collection APIs and lambdas; 35 also needs the omitted users binding and N4 loops" "8;34;35")
native_fences(later "Section 6 is N3, explicitly outside this audit's acceptance scope, including File-dependent and complete print fences" "24;25;26;27;28;29")
native_fences(context "Section 7 needs readFile/parse/Json and N5 brace escaping; no completed fence or premature escape support" "30")
native_fences(later "Section 10 let-else is N4 and find is unavailable" "36")

native_run(5 health-data [=[
  let good: Health = :ok
  let degraded: Health = :degraded
  let down: Health = :down
  good |> { :ok => print("ok")
            :degraded => print("degraded")
            :down => print("down") }
  degraded |> { :ok => print("ok")
                :degraded => print("degraded")
                :down => print("down") }
  down |> { :ok => print("ok")
            :degraded => print("degraded")
            :down => print("down") }
]=] [=[// expect: ok
// expect: degraded
// expect: down]=])

# Independently authored complete programs, NOT supplied File/Json/User APIs.
# N3 returned calls and stored/linear outcomes are deliberately not exercised.
native_prose(prose-scalar-postfix [=[
source(n: int) -> :ok[int] | :!bad[str] {
  if n < 0 { ret :!bad["owned " + "failure"] }
  n
}
relay(n: int) -> :ok[int] | :!bad[str] {
  let value = source(n)?
  print("after success")
  value + 1
}
main() {
  print(str(source(4)!))
  relay(6) |> { :ok[n] => print(str(n))
                :!bad[text] => print(text) }
  relay(-1) |> { :ok[n] => print(str(n))
                 :!bad[text] => print(text) }
}
// expect: 4
// expect: after success
// expect: 7
// expect: owned failure
]=])
native_prose(prose-unit-postfix [=[
touch() -> :ok | :!io { print("touch") }
neutral() -> :done | :!io { :done }
main() {
  let a: () = touch()!
  let b: () = touch()?
  let c: () = neutral()!
  let d: () = neutral()?
  print("unit")
}
// expect: touch
// expect: touch
// expect: unit
]=])
native_prose(prose-tagged-remainder [=[
Change = :created[int] | :updated[int] | :!conflict[int]
save(n: int) -> Change {
  if n == 0 { ret :created[10] }
  if n == 1 { ret :updated[20] }
  :!conflict[n]
}
relay(n: int) -> Change {
  let change = save(n)?
  print("after success")
  change
}
main() {
  save(0)! |> { :created[id] => print(str(id))
                :updated[id] => print(str(id)) }
  relay(1) |> { :created[id] => print(str(id))
                :updated[id] => print(str(id))
                :!conflict[id] => print(str(id)) }
  relay(2) |> { :created[id] => print(str(id))
                :updated[id] => print(str(id))
                :!conflict[id] => print(str(id)) }
}
// expect: 10
// expect: after success
// expect: 20
// expect: 2
]=])
native_prose(prose-none-is-not-propagated [=[
Maybe = :ok[str] | :none | :!bad
source(n: int) -> Maybe {
  if n == 0 { ret :none }
  if n == 1 { ret "owned " + "value" }
  :!bad
}
relay(n: int) -> Maybe {
  let value = source(n)?
  print("after non-error")
  value
}
main() {
  relay(0) |> { :ok[text] => print(text)
                :none => print("none")
                :!bad => print("bad") }
  relay(1) |> { :ok[text] => print(text)
                :none => print("none")
                :!bad => print("bad") }
  relay(2) |> { :ok[text] => print(text)
                :none => print("none")
                :!bad => print("bad") }
  source(0)! |> { :ok[text] => print(text)
                  :none => print("panic also preserves none") }
}
// expect: after non-error
// expect: none
// expect: after non-error
// expect: owned value
// expect: bad
// expect: panic also preserves none
]=])
native_prose(prose-error-accumulation [=[
First = :!missing[int] | :!denied[str]
Second = :!syntax[int]
stageOne(n: int) -> :ok[int] | First {
  if n == 1 { ret :!missing[n] }
  if n == 2 { ret :!denied["owned " + "denial"] }
  n
}
stageTwo(n: int) -> :ok[int] | Second {
  if n == 3 { ret :!syntax[n] }
  n + 10
}
written(n: int) -> :ok[int] | First | Second {
  let value = stageOne(n)?
  stageTwo(value)?
}
inferred(n: int) { n |> stageOne()? |> stageTwo()? }
partial(n: int) -> :ok[int] | _ { n |> stageOne()? |> stageTwo()? }
show(n: int) {
  written(n) |> { :ok[value] => print(str(value))
                  :!missing[id] => print(str(id))
                  :!denied[text] => print(text)
                  :!syntax[line] => print(str(line)) }
  inferred(n) |> { :ok[value] => print(str(value))
                   :!missing[id] => print(str(id))
                   :!denied[text] => print(text)
                   :!syntax[line] => print(str(line)) }
  partial(n) |> { :ok[value] => print(str(value))
                  :!missing[id] => print(str(id))
                  :!denied[text] => print(text)
                  :!syntax[line] => print(str(line)) }
}
main() { show(0)
  show(1)
  show(2)
  show(3)
}
// expect: 10
// expect: 10
// expect: 10
// expect: 1
// expect: 1
// expect: 1
// expect: owned denial
// expect: owned denial
// expect: owned denial
// expect: 3
// expect: 3
// expect: 3
]=])
native_prose(prose-catchall-removes-error [=[
read(n: int) -> :ok[str] | :!missing | :!denied[str] {
  if n == 1 { ret :!missing }
  if n == 2 { ret :!denied["owned " + "denial"] }
  "owned " + "text"
}
recover(n: int) -> :ok[str] | :!denied[str] {
  let text = read(n) |> { :ok[text] => text
                         :!missing => "empty"
                         other => ret other }
  text
}
main() {
  recover(0) |> { :ok[text] => print(text)
                  :!denied[why] => print(why) }
  recover(1) |> { :ok[text] => print(text)
                  :!denied[why] => print(why) }
  recover(2) |> { :ok[text] => print(text)
                  :!denied[why] => print(why) }
}
// expect: owned text
// expect: empty
// expect: owned denial
]=])
native_prose(prose-subset-and-named-pattern [=[
Errors = :!missing[int] | :!denied[str]
Small = :ok[str] | :!missing[int]
Large = Small | :!denied[str]
small(n: int) -> Small {
  if n == 1 { ret :!missing[n] }
  "owned " + "success"
}
widen(n: int) -> Large {
  if n == 2 { ret :!denied["owned " + "denial"] }
  small(n)?
}
main() {
  widen(0) |> { :ok[text] => print(text)
                Errors => print("error group") }
  widen(1) |> { :ok[text] => print(text)
                Errors => print("error group") }
  widen(2) |> { :ok[text] => print(text)
                Errors => print("error group") }
}
// expect: owned success
// expect: error group
// expect: error group
]=])
native_prose(prose-exact-and-singleton [=[
Number = :ok[int]
Unit = :ok
Box { value: Number }
double(n: int) -> int { n * 2 }
only(n: Number) -> Number { :ok[n] }
empty() -> Unit { :ok }
optional() -> :ok[int] | :none { 7 }
main() {
  let n: int = only(double(3)) + 1
  let unit: () = empty()
  let box = Box { value = n }
  print(str(box.value))
  optional() |> { :ok[value] => print(str(value))
                  :none => print("none") }
}
// expect: 7
// expect: 7
]=])
native_prose(prose-valueless-endings [=[
written(b: bool) -> :ok[str] | :none {
  if b { ret "owned " + "value" }
  print("empty ending")
}
inferred(b: bool) {
  if b { ret :none }
  ret
}
partial(b: bool) -> :ok[int] | _ {
  if b { ret :none }
  print("partial ending")
}
ready() -> :ok | :!failed {}
plain() {}
main() {
  written(true) |> { :ok[text] => print(text)
                     :none => print("none") }
  written(false) |> { :ok[text] => print(text)
                      :none => print("none") }
  inferred(false) |> { :none => print("inferred none") }
  partial(false) |> { :ok[n] => print(str(n))
                      :none => print("partial none") }
  let success: () = ready()!
  let empty: () = plain()
}
// expect: owned value
// expect: empty ending
// expect: none
// expect: inferred none
// expect: partial ending
// expect: partial none
]=])
native_prose(prose-error-only-propagation [=[
stop() -> :!stopped[str] { :!stopped["owned " + "failure"] }
main() {
  print("before")
  stop()?
  print("unreachable")
}
// expect: before
// stderr: error: :!stopped["owned failure"]
// exit: 1
]=])
native_prose(prose-error-only-panic [=[
stop() -> :!stopped[str] { :!stopped["owned " + "failure"] }
main() {
  stop()!
}
// panic: panic: :!stopped["owned failure"]
// panic: prose-error-only-panic.brf:3
// exit: 1
]=])
native_prose(prose-mixed-panic [=[
source() -> :ok[int] | :!failed[int] { :!failed[9] }
main() {
  print("before")
  source()!
}
// expect: before
// panic: panic: :!failed[9]
// panic: prose-mixed-panic.brf:4
// exit: 1
]=])
native_prose(prose-entry-error-format [=[
Detail { text: str
  byte: chr
  nested: :inner[str, int]
}
main() {
  print("flushed")
  :!fatal[Detail { text = "owned " + "text\n\0", byte = '\n', nested = :inner["nested", 3] }]
}
// expect: flushed
// stderr: error: :!fatal[Detail { text = "owned text\\n\\0", byte = '\\n', nested = :inner["nested", 3] }]
// exit: 1
]=])
native_prose(prose-entry-neutral-owned [=[
main() {
  print("ordinary data")
  :holder[:!nested["owned " + "error"]]
}
// expect: ordinary data
]=])
native_prose(prose-no-error-postfix [=[
main() {
  :none? // error: ? and ! cannot handle this value: it has no error tags.
  :adult! // error: ? and ! cannot handle this value: it has no error tags.
  7? // error: ? and ! cannot handle this value: it has no error tags.
  :ok[7]! // error: ? and ! cannot handle this value: it has no error tags.
}
]=])
native_prose(prose-omitted-error [=[
source() -> :ok[int] | :!missing { :!missing }
wrong() -> int {
  source()? // error: Propagated variants are not in the written return type.
}
main() {}
]=])
native_prose(prose-no-plain-success-envelope [=[
double(n: int) -> int { n * 2 }
main() {
  double(3) |> { :ok[value] => print(str(value)) } // error: Symbol patterns require a symbol input.
}
]=])
native_prose(prose-ambiguous-ending [=[
Wrong = :ok | :none // error: A union cannot contain both payload-less ':ok' and ':none'.
main() {}
]=])
native_prose(prose-missing-named-arm [=[
Errors = :!first | :!second
Result = :ok[int] | Errors | :!third
source() -> Result { 1 }
main() {
  source() |> { :ok[value] => print(str(value)) // error: Match is not exhaustive.
                Errors => print("error group") }
}
]=])
native_prose(prose-partial-outside-return [=[
Wrong = :ok[int] | _ // error: Partial symbol sets are only allowed as function return types.
main() {}
]=])
native_prose(prose-unit-payload-is-not-ambiguous [=[
source() -> :ok[()] | :none { :ok[()] }
main() {
  source() |> { :ok[value] => print("unit payload")
                :none => print("none") }
}
// expect: unit payload
]=])

native_finish(22 0 37 21)
native_execute()
