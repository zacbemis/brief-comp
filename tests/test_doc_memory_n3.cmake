cmake_minimum_required(VERSION 3.16)
include("${CMAKE_CURRENT_LIST_DIR}/doc_native.cmake")

native_begin(lang-memory 18 3d33551ba68e56c2461228a4de5c7034d2bced20f7f67db8d43666abf0f165d5)
native_fences(run "Complete scalar body or struct definition; observers allocate only acyclic objects and use existing fields/methods" "2;6;7;10")
native_fences(context "Missing User constructor/type, collection storage API or File resource API; no invented implementations" "1;5;8;9;12;18")
native_fences(fragment "Interleaved definitions/usage or alternative duplicate attach declarations; no splitting/relocation" "4;11")
native_fences(later "Cloning or const rules are outside this compiler's scope; missing APIs are not acceptance" "3;13;14;15;16;17")

native_run_body(2 scalar-copy [=[
  print(str(a))
  print(str(b))
]=] [=[// expect: 10
// expect: 10]=])
native_run(6 acyclic-user [=[
  let user = User { name = "owned " + "name", age = 20 }
  let alias = user
  alias.age = 21
  print(user.name)
  print(str(user.age))
]=] [=[// expect: owned name
// expect: 21]=])
native_run(7 acyclic-nodes [=[
  let tail = Node { value = 2, next = :none }
  let head = Node { value = 1, next = :more[tail] }
  head.next |> { :none => print("none")
                 :more[node] => print(str(node.value)) }
]=] [=[// expect: 2]=])
native_run(10 mutable-self [=[
  let user = User { name = "owned " + "name", age = 20 }
  let alias = user
  user.setAge(22)
  print(str(alias.age))
]=] [=[// expect: 22]=])

# Independent prose programs, not replacement User/File/collection APIs.
native_prose(prose-shared-snapshots [=[
Box { text: str }
Holder { box: Box }
replace(holder: &Holder) -> int {
  holder.box = Box { text = "replacement" }
  1
}
observe(box: Box, ignored: int) { print(box.text) }
main() {
  let holder = Holder { box = Box { text = "owned " + "old" } }
  let alias = holder.box
  observe(holder.box, replace(holder))
  holder.box = holder.box
  print(alias.text)
  print(holder.box.text)
  alias.text = "shared alias"
  print(alias.text)
}
// expect: owned old
// expect: owned old
// expect: replacement
// expect: shared alias
]=])
native_prose(prose-outcome-owners [=[
Box { text: str }
Result = :ok[Box] | :!bad[Box]
source(box: Box, fail: bool) -> Result {
  if fail { ret :!bad[box] }
  box
}
relay(box: Box, fail: bool) -> Result {
  let pending = source(box, fail)
  let alias = pending?
  alias
}
show(fail: bool) {
  let local = Box { text = "owned " + "payload" }
  let pending = relay(local, fail)
  pending |> { :ok[box] => print(box.text)
               :!bad[box] => print(box.text) }
}
main() { show(false)
  show(true)
}
// expect: owned payload
// expect: owned payload
]=])
native_prose(prose-loop-and-exit-owners [=[
Box { text: str }
Result = :ok[Box] | :!bad
fresh(n: int) -> Result { Box { text = "box " + str(n) } }
exit(n: int) -> Result {
  for i in 0..3 {
    let pending = fresh(i)
    if i == n { ret pending }
    pending!
    continue
  }
  :!bad
}
main() {
  var pending = fresh(0)
  let original = pending!
  for i in 1..3 { pending = fresh(i)
    let alias = pending!
    print(alias.text)
  }
  print(original.text)
  exit(1) |> { :ok[box] => print(box.text)
               :!bad => print("none") }
  exit(9) |> { :ok[box] => print(box.text)
               :!bad => print("none") }
}
// expect: box 1
// expect: box 2
// expect: box 0
// expect: box 1
// expect: none
]=])
native_prose(prose-writable-reachable-values [=[
Box { text: str }
Holder { box: Box }
Result = :ok[Box] | :!bad[Box]
source(box: Box, fail: bool) -> Result {
  if fail { ret :!bad[box] }
  box
}
attach(holder: &Holder, box: &Box) { holder.box = box }
mutate(box: &Box) {
  let saved = source(box, false)
  let alias = saved!
  alias.text = "success"
  let error = source(box, true)
  error |> { :ok[alias] => alias.text = "unexpected"
             other => other |> { :!bad[alias] => alias.text = "error" } }
}
main() {
  let box = Box { text = "old" }
  let holder = Holder { box = Box { text = "temporary" } }
  attach(holder, box)
  mutate(box)
  print(holder.box.text)
}
// expect: error
]=])
native_prose(prose-value-and-disjoint-results [=[
Box { text: str }
Report { text: str }
number(box: Box) -> int { 3 }
report(box: Box) -> Report { Report { text = box.text } }
inspect(box: Box) {
  var n = number(box)
  n += 1
  let value = :number[n]
  let copy = value
  copy |> { :number[v] => print(str(v)) }
  let own = report(box)
  own.text = "writable disjoint result"
  print(own.text)
}
main() { inspect(Box { text = "borrowed" }) }
// expect: 4
// expect: writable disjoint result
]=])
native_prose(prose-readonly-derived-values [=[
Box { text: str }
Result = :ok[Box] | :!bad[Box]
source(box: Box, fail: bool) -> Result {
  if fail { ret :!bad[box] }
  box
}
postfix(box: Box) { let pending = source(box, false)
  let alias = pending!
  alias.text = "bad" // error: Cannot mutate through an ordinary parameter; use &.
}
propagation(box: Box) { let pending = source(box, false)
  let alias = pending?
  alias.text = "bad" // error: Cannot mutate through an ordinary parameter; use &.
}
matched(box: Box) { let pending = source(box, false)
  pending |> { :ok[alias] => alias.text = "bad" // error: Cannot mutate through an ordinary parameter; use &.
               other => other |> { :!bad[alias] => alias.text = "bad" } } // error: Cannot mutate through an ordinary parameter; use &.
}
rebound(box: Box) { var pending = source(box, false)
  pending!
  pending = source(box, true)
  pending |> { :ok[alias] => print(alias.text)
               :!bad[alias] => alias.text = "bad" } // error: Cannot mutate through an ordinary parameter; use &.
}
main() {}
]=])
native_prose(prose-readonly-store-and-forward [=[
Box { text: str }
Holder { box: Box }
Symbols = :some[Box] | :empty
SymbolHolder { value: Symbols }
same(box: Box) -> Box { box }
change(box: &Box) { box.text = "changed" }
symbols(value: &Symbols) {}
wrong(holder: &Holder, values: &SymbolHolder, box: Box, value: Symbols) {
  let alias = box
  holder.box = alias // error: A read-only value cannot be stored in a field; take the parameter it comes from as &.
  holder.box = same(box) // error: A read-only value cannot be stored in a field; take the parameter it comes from as &.
  let literal = Holder { box = box }
  holder.box = literal.box // error: A read-only value cannot be stored in a field; take the parameter it comes from as &.
  change(alias) // error: A read-only value cannot be passed to an & parameter; take the parameter it comes from as &.
  values.value = value // error: A read-only value cannot be stored in a field; take the parameter it comes from as &.
  symbols(value) // error: A read-only value cannot be passed to an & parameter; take the parameter it comes from as &.
}
main() {}
]=])
native_prose(prose-parameter-bindings [=[
Box { text: str }
plain(box: Box) {
  box = Box { text = "new" } // error: Cannot reassign an immutable binding.
}
mutable(box: &Box) {
  box = Box { text = "new" } // error: Cannot reassign an immutable binding.
}
main() {}
]=])
native_prose(prose-loop-and-branch-permissions [=[
Box { text: str }
branch(box: Box, flag: bool) {
  let alias = if flag { box } else { Box { text = "fresh" } }
  alias.text = "bad" // error: Cannot mutate through an ordinary parameter; use &.
}
zero(box: Box) {
  var alias = box
  for i in 0..2 { alias = Box { text = "new" } }
  alias.text = "bad" // error: Cannot mutate through an ordinary parameter; use &.
}
back(box: Box) {
  var alias = Box { text = "fresh" }
  for i in 0..2 { alias.text = "bad" // error: Cannot mutate through an ordinary parameter; use &.
    alias = box
    continue
  }
}
returned(box: Box, flag: bool) {
  var alias = Box { text = "fresh" }
  if flag { alias = box
            ret }
  alias.text = "allowed"
}
main() {}
]=])
native_prose(prose-call-and-literal-permissions [=[
Box { text: str }
Holder { box: Box }
BoxResult = :ok[Box] | :!bad
same(box: Box) -> Box { box }
wrap(box: Box) -> Holder { Holder { box = box } }
source(box: Box) -> BoxResult { box }
wrong(box: Box) {
  let alias = box |> same()
  alias.text = "bad" // error: Cannot mutate through an ordinary parameter; use &.
  let holder = wrap(box)
  holder.box.text = "bad" // error: Cannot mutate through an ordinary parameter; use &.
  let literal = Holder { box = box }
  literal.box.text = "bad" // error: Cannot mutate through an ordinary parameter; use &.
  let pending = source(box)
  let payload = pending!
  payload.text = "bad" // error: Cannot mutate through an ordinary parameter; use &.
}
main() {}
]=])

native_finish(14 0 14 10)
native_execute()
