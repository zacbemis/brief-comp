cmake_minimum_required(VERSION 3.16)
include("${CMAKE_CURRENT_LIST_DIR}/doc_native.cmake")

native_begin(lang-modules 19 95441d5a617eaf640fedc784f3ab1da6f0dba7357a4f5a61c46a9bea30db8723)
native_fences(run "Complete struct, aliases or constructor, unchanged, observed within one file only" "1;2;19")
native_fences(reject "Only the pub type error is checked; duplicate alternatives and cross-file private access are not accepted" "8")
native_fences(reject "Complete source rejects its file-level statement, without adding another main" "16")
native_fences(context "Missing User context and duplicate literal alternatives; no supplied type or splitting" "3")
native_fences(later "N5 interpolation, missing DateError/daysIn and mixed declaration/usage; no stripping or local stand-ins" "4")
native_fences(context "Missing Date/date; independent wrong-form cases do not complete these calls or invent a receiver" "5")
native_fences(later "Multi-item module example needs const, traits and export/name resolution; no extraction of User" "6")
native_fences(context "Missing User/version/HasUser and module-qualified tag resolution" "7")
native_fences(later "Trait conformance and cross-file APIs; scope rejection is not conformance acceptance" "9")
native_fences(context "Ambiguous or misspelled module lookup is unavailable; unknown-name errors are not the documented errors" "10;11")
native_fences(later "Import selection and filesystem/standard-library module resolution are unavailable" "12;13")
native_fences(fragment "Alternative conformances and ellipsis bodies; no completion or extraction" "14")
native_fences(context "File labels do not make two modules; no dependency/cycle simulation" "15")
native_fences(n2 "Missing File API and N2 qualified error-kind entry/formatting rules; no mocked I/O" "17")
native_fences(context "Missing User/getName API; no constructor supplied to the complete main" "18")

native_run(1 struct-fields [=[
  let user = User { name = "Bob", age = 20, userVer = 0.5 }
  print(user.name)
  print(str(user.age))
  print(str(user.userVer))
]=] [=[// expect: Bob
// expect: 20
// expect: 0.5]=])
native_run(2 type-aliases [=[
  let id: Id = Id(42)
  print(str(id))
  let error: DateError = :!badDay[7]
  error |> { :!badYear[n] => print("year " + str(n))
             :!badMonth[n] => print("month " + str(n))
             :!badDay[n] => print("day " + str(n)) }
]=] [=[// expect: 42
// expect: day 7]=])
native_reject(8 public-type "Struct types cannot be marked 'pub'." 22)
native_expect_fix(public-type 1 "Report {                // Compiler error: a type is always public")
native_reject_complete(16 file-statement "Statements are only allowed inside a function." 3)
native_run(19 fallible-constructor [=[
  User.new("Bob", 20) |> { :ok[user] => print(user.name + " " + str(user.age))
                          :!invalidAge[n] => print("invalid " + str(n)) }
  User.new("Ann", 18) |> { :ok[user] => print(user.name + " " + str(user.age))
                          :!invalidAge[n] => print("invalid " + str(n)) }
  User.new("Young", 17) |> { :ok[user] => print(user.name + " " + str(user.age))
                            :!invalidAge[n] => print("invalid " + str(n)) }
]=] [=[// expect: Bob 20
// expect: Ann 18
// expect: invalid 17]=])

# Independently authored one-file programs, never APIs supplied to a fence.
native_prose(prose-field-order [=[
Trace { value: int }
Pair { first: int
  second: int
}
mark(trace: &Trace, digit: int) { trace.value = trace.value * 10 + digit
  digit
}
main() {
  var trace = Trace { value = 0 }
  let pair = Pair { second = mark(trace, 2), first = mark(trace, 1) }
  print(str(trace.value))
  print(str(pair.first))
  print(str(pair.second))
}
// expect: 21
// expect: 1
// expect: 2
]=])
native_prose(prose-members [=[
Gauge {
  value: int
  pub new(value: int) { Gauge { value = value } }
  pub read() { self.value }
  bump(&self) { self.value++ }
  pub copy() { Gauge.new(self.read()) }
  pub advance(&self) { self.bump()
    Gauge.new(self.read())
  }
}
main() {
  var gauge = Gauge.new(2)
  print(str(gauge.copy().read()))
  print(str(gauge.advance().read()))
  print(str(gauge.read()))
}
// expect: 2
// expect: 3
// expect: 3
]=])
native_prose(prose-readonly-self [=[
Gauge { value: int
  change() { self.value = 2 } // error: Cannot mutate through an ordinary parameter; use &.
}
main() {}
]=])
native_prose(prose-call-forms [=[
Box {
  value: int
  make(value: int) { Box { value = value } }
  reads() { self.value }
  explicit(box: Box) { box.value }
  dormant() { ret 1
    self.value
  }
}
main() {
  let box = Box.make(1)
  Box.reads(box) // error: Methods are called on a value, not the type. // fix: box.reads()
  box.make(2) // error: Associated functions are called on the type, not a value. // fix: Box.make(2)
  box.explicit() // error: Associated functions are called on the type, not a value. // fix: Box.explicit(box)
  Box.dormant() // error: Methods are called on a value, not the type.
}
]=])
native_prose(prose-literal-errors [=[
Pair { first: int
  second: int
}
main() {
  Pair { first = 1 } // error: Missing struct field initializer.
  Pair { first = 1, second = 2, first = 3 } // error: Duplicate struct field initializer.
  Pair { first = 1, second = 2, extra = 3 } // error: Unknown struct field.
}
]=])
native_prose(prose-public-types [=[
pub Value { field: int } // error: Struct types cannot be marked 'pub'. // fix: Value { field: int }
pub Choice = :red | :blue // error: Type aliases cannot be marked 'pub'. // fix: Choice = :red | :blue
pub HasValue { read(self) -> int } // error: Trait types cannot be marked 'pub'. // fix: HasValue { read(self) -> int }
main() {}
]=])
native_prose(prose-module-name-syntax [=[
import models.user // error: Module names must start with an uppercase letter. // fix: import models.User
A = db.:!bad[int] // error: Module names must start with an uppercase letter. // fix: A = Db.:!bad[int]
B = Db.:Other[int] // error: Symbol tags must start with a lowercase letter. // fix: B = Db.:other[int]
main() {}
]=])
native_prose(prose-import-boundary [=[
import missing.User // error: Imports are not implemented yet.
main() { unknown() }
]=])
native_prose(prose-trait-boundary [=[
HasValue { read(self) -> Missing } // error: A struct needs at least one field; traits are not implemented yet.
main() { unknown() }
]=])
native_prose(prose-conformance-boundary [=[
Box: Missing { // error: Trait conformance is not implemented yet.
  value: Unknown
  read() { absent() }
}
main() { unknown() }
]=])
native_prose(prose-qualified-tag-boundary [=[
main() {
  Remote.:!bad[unknown] // error: Module-qualified symbols are not implemented yet.
}
]=])
native_prose(prose-plain-entry [=[
main() { print("entry")
  42
}
// expect: entry
// exit: 0
]=])
native_prose(prose-constructor-convention [=[
Maker {
  value: int
  make(value: int) { Maker { value = value } }
  new() { self.value }
}
main() {
  let maker = Maker.make(9)
  print(str(maker.new()))
}
// expect: 9
]=])

native_prose(prose-no-module-wrapper [=[
mod Container {} // error: Expected a declaration (function, struct, or named type).
main() {}
]=])
native_prose(prose-entry-parameters [=[
main(value: int) {} // error: main() cannot have parameters.
]=])

native_finish(18 2 14 15)
native_execute()
