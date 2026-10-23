# M6 memory-document acceptance. See doc-memory.md for scope and limitations.
cmake_minimum_required(VERSION 3.16)
include("${CMAKE_CURRENT_LIST_DIR}/doc_examples.cmake")
doc_examples_begin(lang-memory 17)

set(user_type [=[
User { name: str
  age: int }
]=])
set(user_ctor [=[
impl User { new(name: str, age: int): Sym<:ok(User), :invalidAge(int)> {
  if age < 0 { ret :invalidAge(age) }
  User { name: name + "", age: age }
} }
]=])
set(user_api "${user_type}\n${user_ctor}")
example(1 shared-reference "${user_api}\nmain() {" "${block_1}" [=[
other.age = 21
print(str(user.age))
user.name += "!"
print(other.name)
}
// expect: 21
// expect: Bob!
]=])
example(2 primitive-copy "main() {" "${block_2}" [=[
var copy = b
copy++
print(str(a))
print(str(b))
print(str(copy))
}
// expect: 10
// expect: 10
// expect: 11
]=])
# Fence 4 mixes a definition and local statements. The no-count-change comment
# describes an optimization; callers currently pin reads. Do not assert it here.
set(code "${block_4}")
replace_once(code "\nlet user = User.new(" "\nmain() {\nlet user = User.new(")
example(4 borrowed-call "${user_api}" "${code}" "print(str(age))\n}\n// expect: 20")
set(slot_api [=[
UserSlot { user: User }
UserCollection { userVec: UserSlot }
impl UserSlot { add(slot: &UserSlot, user: &User) { slot.user = user } }
]=])
example(5 retained-argument "${user_api}\n${slot_api}" "${block_5}" [=[
fill(collection: &UserCollection) {
  let user = User.new("B" + "ob", 20)!
  add(collection, user)
}
main() {
  let slot = UserSlot { user: User.new("old", 1)! }
  let collection = UserCollection { userVec: slot }
  fill(collection)
  slot.user.age = 21
  print(slot.user.name + ":" + str(slot.user.age))
}
// expect: Bob:21
]=])
example(6 nonrecursive-type "" "${block_6}" [=[
main() { let user = User { name: "B" + "ob", age: 20 }
  let other = user
  other.age = 21
  print(user.name + ":" + str(user.age)) }
// expect: Bob:21
]=])
# Only the declaration and acyclic ARC behavior are in scope, not the comments'
# future collector classification/reclamation promise.
example(7 recursive-type "" "${block_7}" [=[
observe(node: Node) { node.next |> {
  :ok(child) => print(str(child.value))
  :none => print("none")
} }
main() {
  let leaf = Node { value: 2, next: :none }
  let root = Node { value: 1, next: :ok(leaf) }
  observe(root)
  observe(leaf)
}
// expect: 2
// expect: none
]=])
example(8 immutable-borrow "${user_api}" "${block_8}" [=[
main() { let user = User.new("Bob", 20)!
  print(str(getAge(user))) }
// expect: 20
]=])
set(code "${block_8}")
replace_once(code "  user.age" "  user.age = 21\n  user.age")
error_at(code "user.age =" "Cannot mutate through an ordinary parameter; use &.")
example(8 immutable-borrow-write "${user_api}" "${code}" "main() {}")
example(9 mutable-borrow "${user_api}" "${block_9}" [=[
main() { let user = User.new("Bob", 20)!
  let other = user
  setAge(user, 21)
  print(str(other.age)) }
// expect: 21
]=])
set(code "${block_9}")
replace_once(code "  user.age = age" "  user = User.new(\"new\", 1)!\n  user.age = age")
error_at(code "user = User.new" "Cannot reassign an immutable binding.")
example(9 immutable-parameter-binding "${user_api}" "${code}" "main() {}")

set(report_api [=[
Report { owner: User }
same(user: User): User { user }
impl Report {
  new(user: User): Self { Self { owner: user } }
  get(report: Report): User { report.owner }
}
]=])
set(store_error "A read-only struct cannot be stored in a field; take the parameter it comes from as &.")
set(mutate_error "Cannot mutate through an ordinary parameter; use &.")
set(forward_error "A mutable argument cannot be borrowed through an ordinary parameter.")
string(STRIP "${block_10}" alternatives)
# Validate one blank-line boundary without making a CMake list: the original
# error comment contains a semicolon that must stay part of its source line.
replace_once(alternatives "\n\n" "\n\n")
string(FIND "${alternatives}" "\n\n" split)
string(SUBSTRING "${alternatives}" 0 ${split} readonly_attach)
math(EXPR split "${split} + 2")
string(SUBSTRING "${alternatives}" ${split} -1 writable_attach)
set(code "${readonly_attach}")
error_at(code "report.owner =" "${store_error}")
example(10 readonly-store "${user_api}\n${report_api}" "${code}" "main() {}")
example(10 writable-store "${user_api}\n${report_api}" "${writable_attach}" [=[
fill(report: &Report) {
  let user = User.new("stored" + " user", 22)!
  attach(report, user)
}
main() {
  let report = Report { owner: User.new("old", 1)! }
  let user = User.new("B" + "ob", 20)!
  attach(report, user)
  user.age = 21
  print(str(report.owner.age))
  fill(report)
  print(report.owner.name + ":" + str(report.owner.age))
}
// expect: 21
// expect: stored user:22
]=])
foreach(path IN ITEMS alias call literal)
    set(code "${readonly_attach}")
    set(extra "")
    if(path STREQUAL "alias")
        replace_once(code "report.owner = user" "let copy = user\n  report.owner = copy")
        set(needle "report.owner =")
    elseif(path STREQUAL "call")
        replace_once(code "report.owner = user" "report.owner = same(user)")
        set(needle "report.owner =")
    else()
        set(extra "Box { report: Report }")
        replace_once(code "report: &Report" "report: &Box")
        replace_once(code "report.owner = user" "report.report = Report { owner: user }")
        set(needle "report.report =")
    endif()
    error_at(code "${needle}" "${store_error}")
    example(10 "readonly-store-${path}" "${user_api}\n${report_api}\n${extra}" "${code}" "main() {}")
endforeach()
set(code "forward(report: &Report, user: User) { attach(report, user) }")
error_at(code "forward(" "${forward_error}")
example(10 readonly-caller "${user_api}\n${report_api}\n${writable_attach}" "${code}" "main() {}")
example(11 nonexclusive-aliases "${user_api}\n${block_9}\nobserve(user: User, writable: &User) {\nprint(str(user.age))\nsetAge(writable, 21)\nprint(str(user.age))\n}\nmain() {"
    "${block_11}" "observe(user, other)\n}\n// expect: 20\n// expect: 21")

# Guard prose obligations against the actual chapter; these are constructed
# first-version contexts, not additional fences or implementations of const.
file(READ "${ROOT}/docs/lang-memory.txt" document)
string(REGEX REPLACE "[\r\n ]+" " " prose "${document}")
function(require_prose text)
    string(FIND "${prose}" "${text}" at)
    if(at LESS 0)
        message(FATAL_ERROR "Missing memory-rule anchor: ${text}")
    endif()
endfunction()
set(common "${user_api}\n${report_api}\n${block_9}")
function(permission_error name parameters body needle message)
    set(code "probe(${parameters}) {\n${body}\n}")
    error_at(code "${needle}" "${message}")
    example(0 "${name}" "${common}" "${code}" "main() {}")
endfunction()
require_prose("Field paths rooted in an ordinary parameter cannot be written or forwarded to an `&` parameter.")
permission_error(field-write "report: Report" "report.owner.age = 21" "report.owner.age" "${mutate_error}")
permission_error(field-forward "report: Report" "setAge(report.owner, 21)" "setAge(report.owner" "${forward_error}")
require_prose("preserves read-only access when bound to `let` or `var`, copied again, selected by a branch, or assigned to a local.")
permission_error(copied-alias "report: Report" [=[
let first = report.owner
var second = first
let third = second
third.age = 21
]=] "third.age" "${mutate_error}")
permission_error(reassigned-alias "report: Report" [=[
var copy = User.new("fresh", 1)!
copy = report.owner
copy.age = 21
]=] "copy.age" "${mutate_error}")
permission_error(branch-value "report: Report, flag: bool" [=[
let copy = if flag { report.owner } else { User.new("fresh", 1)! }
copy.age = 21
]=] "copy.age" "${mutate_error}")
require_prose("At control-flow joins, access is read-only if any continuing path is read-only.")
permission_error(branch-assignment "report: Report, flag: bool" [=[
var copy = User.new("fresh", 1)!
if flag { copy = report.owner }
copy.age = 21
]=] "copy.age" "${mutate_error}")
require_prose("Loops conservatively include zero iterations and backedges; `break`/`continue` states are included")
permission_error(loop-zero "report: Report, flag: bool" [=[
var copy = report.owner
while flag { copy = User.new("fresh", 1)! }
copy.age = 21
]=] "copy.age" "${mutate_error}")
permission_error(loop-continue "report: Report, flag: bool" [=[
var copy = User.new("fresh", 1)!
while flag {
  setAge(copy, 21)
  copy = report.owner
  continue
}
]=] "setAge(copy" "${forward_error}")
permission_error(loop-break "report: Report" [=[
var copy = User.new("fresh", 1)!
while true { copy = report.owner
  break }
copy.age = 21
]=] "copy.age" "${mutate_error}")
require_prose("Rebinding restores writable access only when the new value has no propagated read-only restriction.")
require_prose("returning paths do not restrict continuing paths.")
example(0 fresh-rebinding "${common}" [=[
probe(report: Report, flag: bool) {
  var copy = report.owner
  copy = User.new("fresh", 1)!
  if flag { copy = report.owner
    ret }
  copy.age = 21
  print(copy.name + ":" + str(copy.age))
}
main() {
  let report = Report { owner: User.new("original", 20)! }
  probe(report, true)
  probe(report, false)
  print(str(report.owner.age))
}
// expect: fresh:21
// expect: 20
]=] "")
require_prose("An identity call `same(p)` and a getter returning `q.p` stay read-only.")
permission_error(identity-result "report: Report" "same(report.owner).age = 21" "same(report.owner).age" "${mutate_error}")
permission_error(receiver-result "report: Report" "report.get().age = 21" "report.get().age" "${mutate_error}")
require_prose("`Report.new(q.user)` stays read-only if a `Report` can contain a `User`")
permission_error(overlapping-result "report: Report" [=[
let result = Report.new(report.owner)
result.owner.age = 21
]=] "result.owner.age" "${mutate_error}")
require_prose("and is writable if it holds only strings and numbers.")
example(0 disjoint-result "${common}" [=[
Summary { name: str
  age: int }
impl Summary { new(user: User): Self { Self { name: user.name, age: user.age } } }
probe(report: Report) {
  let summary = Summary.new(report.owner)
  summary.name += "!"
  summary.age++
  print(summary.name + ":" + str(summary.age))
  print(report.owner.name + ":" + str(report.owner.age))
}
main() { probe(Report { owner: User.new("Bob", 20)! }) }
// expect: Bob!:21
// expect: Bob:20
]=] "")
require_prose("A struct literal with any read-only struct initializer is read-only as a whole, including its other fields.")
permission_error(literal-other-field "report: Report" [=[
let wrapper = Report { owner: report.owner }
wrapper.owner.age = 21
]=] "wrapper.owner.age" "${mutate_error}")
# Even a field unrelated to the read-only initializer is restricted.
set(common_saved "${common}")
set(common "${common}\nEnvelope { owner: User\n  note: str }")
permission_error(literal-whole-value "report: Report" [=[
let wrapper = Envelope { owner: report.owner, note: "fresh" }
wrapper.note = "changed"
]=] "wrapper.note" "${mutate_error}")
set(common "${common_saved}")
require_prose("Primitive results and copied primitive fields do not carry the restriction.")
example(0 copied-fields "${common}" [=[
copy(target: &Report, source: User) {
  target.owner.name = source.name
  target.owner.age = source.age
}
main() {
  let source = User.new("B" + "ob", 20)!
  let target = Report { owner: User.new("old", 1)! }
  copy(target, source)
  target.owner.name += "!"
  target.owner.age++
  print(target.owner.name + ":" + str(target.owner.age))
  print(source.name + ":" + str(source.age))
}
// expect: Bob!:21
// expect: Bob:20
]=] "")

set(choice_api [=[
type Choice = Sym<:some(User), :none>
Holder { value: Choice }
write(choice: &Choice) { choice |> {
  :some(user) => user.age = 21
  :none => ()
} }
maybe(user: User): Sym<:ok(User), :missing> { user }
fail(user: User): Sym<:ok, :bad(User)> { :bad(user) }
relay(user: User) { fail(user)? }
]=])
set(common "${common_saved}\n${choice_api}")
require_prose("including objects in symbol payloads.")
example(0 mutable-symbol "${common}" [=[
main() { let user = User.new("Bob", 20)!
  let choice: Choice = :some(user)
  write(choice)
  write(:none)
  print(str(user.age)) }
// expect: 21
]=] "")
require_prose("Symbol copies, payload bindings, narrowed catch-alls, implicit success projections and `?`/`!` payload extraction preserve the same access rule.")
permission_error(symbol-copy "choice: Choice" [=[
let copy = choice
copy |> { :some(user) => user.age = 21
  :none => () }
]=] "user.age" "${mutate_error}")
permission_error(symbol-residual "choice: Choice" [=[
choice |> { :none => ()
  remaining => remaining |> { :some(user) => user.age = 21 } }
]=] "user.age" "${mutate_error}")
foreach(handler IN ITEMS "?" "!")
    if(handler STREQUAL "?")
        set(name propagate)
    else()
        set(name panic)
    endif()
    permission_error("readonly-${name}-payload" "report: Report"
        "let user = maybe(report.owner)${handler}\nuser.age = 21" "user.age" "${mutate_error}")
endforeach()
require_prose("An ordinary symbol parameter containing an object cannot be forwarded to an `&` parameter or stored in a writable field.")
permission_error(symbol-forward "choice: Choice" "write(choice)" "write(choice)" "${forward_error}")
permission_error(symbol-store "holder: &Holder, choice: Choice" "holder.value = choice" "holder.value" "${store_error}")
require_prose("The parameter binding itself remains immutable.")
permission_error(symbol-binding "choice: &Choice" "choice = :none" "choice =" "Cannot reassign an immutable binding.")
require_prose("Error propagation retains the active payload before cleaning up the returning scope; it never upgrades permission.")
permission_error(propagated-error-permission "report: Report" [=[
relay(report.owner) |> {
  :ok => ()
  :bad(user) => user.age = 21
}
]=] "user.age" "${mutate_error}")
example(0 propagated-error-owner "${common}" [=[
make(): Sym<:ok, :bad(User)> {
  let user = User.new("B" + "ob", 20)!
  relay(user)?
  print("not reached")
}
main() { make() |> {
  :ok => print("unexpected")
  :bad(user) => { user.age = 21
    print(user.name + ":" + str(user.age)) }
} }
// expect: Bob:21
]=] "")
require_prose("Copied primitive values and symbols containing only immutable/value payloads do not carry this restriction.")
example(0 immutable-symbol-payloads "${user_api}" [=[
type Data = Sym<:count(int), :text(str)>
DataHolder { value: Data }
store(holder: &DataHolder, data: Data) { holder.value = data }
forward(holder: &DataHolder, data: Data) { store(holder, data) }
main() {
  let holder = DataHolder { value: :count(1) }
  forward(holder, :text("h" + "i"))
  holder.value |> { :count(n) => print(str(n))
    :text(text) => print(text) }
}
// expect: hi
]=] "")

set(common "${common_saved}")
require_prose("This pins field arguments even if a later argument or the callee overwrites their owner's field.")
example(0 pinned-arguments "${common}" [=[
replace(report: &Report): int { report.owner = User.new("new", 2)!
  0 }
read(user: User, ignored: int) { print(user.name) }
readAndReplace(user: User, report: &Report) {
  report.owner = User.new("newer", 3)!
  print(user.name)
}
main() {
  let report = Report { owner: User.new("old", 1)! }
  read(report.owner, replace(report))
  readAndReplace(report.owner, report)
  print(report.owner.name)
}
// expect: old
// expect: new
// expect: newer
]=] "")
require_prose("returned references are owned.")
require_prose("Stores retain the new value before releasing the old one, including self-assignment.")
example(0 owned-return-self-store "${common}" [=[
make(): User {
  let report = Report { owner: User.new("B" + "ob", 20)! }
  report.owner = report.owner
  report.owner.name = report.owner.name
  report.get()
}
main() { var user = make()
  user = user
  user.age = 21
  print(user.name + ":" + str(user.age)) }
// expect: Bob:21
]=] "")
require_prose("Locals and temporaries are released on lexical scope exits, returns, break, and continue; reused loop slots release their previous values.")
example(0 scope-and-jump-cleanup "${common}" [=[
make(n: int): User {
  for i in 0..n {
    let user = User.new("user" + str(i), i)!
    let report = Report { owner: user }
    if i == 0 { continue }
    if i == 1 { ret report.owner }
  }
  User.new("fallback", 20)!
}
scan() {
  for i in 0..3 {
    let report = Report { owner: User.new("scan" + str(i), i)! }
    if i == 0 { continue }
    print(report.owner.name)
    break
  }
}
main() { print(make(3).name)
  print(make(1).name)
  scan() }
// expect: user1
// expect: fallback
// expect: scan1
]=] "")

# Clone; const (including its intended errors); and real external-resource APIs
# remain deferred. Fences 4, 6 and 7 have explicit partial coverage in the audit.
doc_examples_finish("3;12;13;14;15;16;17" "" 48)
