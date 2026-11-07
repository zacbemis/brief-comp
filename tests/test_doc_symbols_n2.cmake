cmake_minimum_required(VERSION 3.16)
include("${CMAKE_CURRENT_LIST_DIR}/doc_native.cmake")

native_begin(lang-symbols 15 c558877977e6348d3c403ee4e8b62ab91b05f8674775d0070bf0bca4dd834bde)
native_fences(run "Complete local Error alias, unchanged, with entry observers; file comment does not implement a Json module" "14")
native_fences(context "Missing User type or user value; no stand-in declaration or split construction alternatives" "1;2;3")
native_fences(fragment "Bare type expression, not a complete declaration/body; classification prose is checked independently" "4")
native_fences(fragment "Interleaved type/struct declarations and local usage; no extraction or scope relocation" "5")
native_fences(context "Missing heron implementation or Op type; no supplied helper or removed declarations" "6;7")
native_fences(later "Qualified symbols, missing File/User APIs and N5 interpolation" "8;13")
native_fences(later "Merged symbols and module identity/lookup are bootstrap work" "9")
native_fences(fragment "Ellipsis body, missing User/Db context and typed merged-symbol matching; no completion" "10")
native_fences(context "Missing User.new API; unknown-name rejection would not establish postfix semantics" "11;12")
native_fences(context "Missing User and Json.Error; no local replacement for qualified composition" "15")

native_run(14 local-error-alias [=[
  let syntax: Error = :!syntax[7]
  let missing: Error = :!missingKey["owned " + "key"]
  let wrong: Error = :!wrongType["owned " + "type"]
  syntax |> { :!syntax[line] => print(str(line))
              :!missingKey[key] => print(key)
              :!wrongType[key] => print(key) }
  missing |> { :!syntax[line] => print(str(line))
               :!missingKey[key] => print(key)
               :!wrongType[key] => print(key) }
  wrong |> { :!syntax[line] => print(str(line))
             :!missingKey[key] => print(key)
             :!wrongType[key] => print(key) }
]=] [=[// expect: 7
// expect: owned key
// expect: owned type]=])

# Independently authored complete programs, NOT completed document fences.
native_prose(prose-neutral-classification [=[
Tier = :bronze | :silver | :gold
Account { tier: Tier }
classify(n: int) -> :negative | :adult | :senior {
  if n < 0 { ret :negative }
  if n < 65 { ret :adult }
  :senior
}
main() {
  classify(1)
  let account = Account { tier = :gold }
  account.tier |> { :bronze => print("bronze")
                    :silver => print("silver")
                    :gold => print("gold") }
  classify(-1) |> { :negative => print("negative")
                    :adult => print("adult")
                    :senior => print("senior") }
  classify(18) |> { :negative => print("negative")
                    :adult => print("adult")
                    :senior => print("senior") }
  classify(65) |> { :negative => print("negative")
                    :adult => print("adult")
                    :senior => print("senior") }
}
// expect: gold
// expect: negative
// expect: adult
// expect: senior
]=])
native_prose(prose-positional-payloads [=[
Shape = :point[dbl] | :rect[dbl, dbl] | :triple[dbl, dbl, dbl]
measure(s: Shape) -> dbl {
  s |> { :point[r] => r
         :rect[w, h] => w * h
         :triple[a, b, c] => a + b + c }
}
main() {
  print(str(measure(:point[2])))
  print(str(measure(:rect[3, 4])))
  print(str(measure(:triple[1, 2, 3])))
  let owned = :row["heap " + "text", 9]
  owned |> { :row[text, number] => { print(text)
                                    print(str(number)) } }
}
// expect: 2
// expect: 12
// expect: 6
// expect: heap text
// expect: 9
]=])
native_prose(prose-absent-and-unit [=[
effect() { print("effect") }
main() {
  let empty: :empty = :empty[]
  let unit: :unit[()] = :unit[effect()]
  empty |> { :empty => print("zero positions") }
  unit |> { :unit[value] => { let checked: () = value
                             print("one position") } }
  let absent: :some[str] | :none = :none[]
  absent |> { :some[text] => print(text)
              :none => print("none") }
}
// expect: effect
// expect: zero positions
// expect: one position
// expect: none
]=])
native_prose(prose-forward-and-recursive [=[
Tree = Later
Later = :leaf[str] | :branch[Branch]
Branch { child: Tree }
main() {
  let tree: Tree = :branch[Branch { child = :leaf["owned " + "leaf"] }]
  tree |> { :leaf[text] => print(text)
            :branch[node] => node.child |> { :leaf[text] => print(text)
                                              :branch[child] => print("branch") } }
}
// expect: owned leaf
]=])
native_prose(prose-contextual-nesting [=[
Inner = :left[str] | :right[int]
Outer = :box[Inner] | :none
choose(left: bool) -> Outer {
  if left { ret :box[:left["owned " + "left"]] }
  :box[:right[4]]
}
show(value: Outer) {
  value |> { :none => print("outer none")
             :box[inner] => inner |> { :left[text] => print(text)
                                       :right[n] => print(str(n)) } }
}
main() {
  show(choose(true))
  show(choose(false))
  let nested: :ok[:none | :item[str]] | :none = :ok[:none]
  nested |> { :none => print("outer none")
              :ok[inner] => inner |> { :none => print("inner none")
                                        :item[text] => print(text) } }
}
// expect: owned left
// expect: 4
// expect: inner none
]=])
native_prose(prose-kind-conflict [=[
Data = :collision
Error = :!collision // error: A symbol name cannot be both neutral and error in one module.
main() {}
]=])
native_prose(prose-payload-conflicts [=[
WrongType = :!bad[int] | :!bad[str] // error: Symbol tag has conflicting payload types.
WrongShape = :absent | :absent[()] // error: Symbol tag has conflicting payload types.
main() {}
]=])
native_prose(prose-plain-union-member [=[
Wrong = int | :text[str] // error: A symbol-set inclusion requires a symbol set.
main() {}
]=])
native_prose(prose-recursive-union [=[
Loop = :again[Loop] // error: Recursive named types require a struct indirection.
main() {}
]=])
native_prose(prose-none-payload [=[
Wrong = :none[()] // error: ':none' cannot carry a payload.
main() {}
]=])
native_prose(prose-payload-context-error [=[
main() {
  let typed: int = 1
  let value: :wide[lng] = :wide[typed] // error: Binding value does not match its annotation.
}
]=])

native_finish(12 0 14 11)
native_execute()
