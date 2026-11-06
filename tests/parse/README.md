# Parser example files

Source files for parser tests. `../test_parse_examples.cmake` runs them as the
CTest test `test_parse_examples`, part of the normal suite. It has the label
`parse-examples`, so `ctest -L parse-examples` runs only these files and
`ctest -LE parse-examples` skips them. The test reports every failing file in
one run.

## `pass/`

Every file must parse with no diagnostics: `brief dump-ast` exits with status 0
and prints a tree. Files use N1's `not`, `dbl`, `->` returns, keyword-free
aliases, `=` literal fields, `.()` indexing and `Name[Type]` arguments. Array
annotations use `Arr[Type]`; typed `Arr[Type] { items }`/`Vec[Type] { items }`
literals, indexing and generics are syntax-only, not supported runtime
operations. Structs require uppercase names; functions, fields, parameters,
bindings and loops require lowercase names. Symbol tags must start lowercase in
type entries, values and patterns; `_` is not a tag name. `trait` and `ovr` are
ordinary names; old declaration/modifier spellings still reject. Unqualified
lowercase pattern names bind; uppercase names reference symbol sets. Imports
select module paths at top level, with uppercase final module names; compilation
still reports them as unimplemented. Symbol/type/member grammar and broader
module casing remain pending. Error tags use adjacent `:!name` in the existing
type/value/pattern contexts. Payload lists use brackets, including empty lists,
trailing commas and several positional values/patterns. Native compilation still
supports at most one payload. Union types use `|` in type positions, including
payloads and type arguments, without changing bitwise or in expressions. `Sym`
is ordinary; old `Sym<...>` wrappers reject. A bare `_` parses as a type marker,
but checking restricts it to partial return unions with a written `:ok`. Typed
payload bindings (`:tag[name: Type]`) preserve annotations and require
lowercase, unqualified binding names. They parse only in payload patterns,
including grouped/nested forms; native type-directed matching remains
unsupported. `Self` is an ordinary uppercase name in all these contexts.
Implicit-owner shorthand is diagnosed by resolution, not by reserving the name
or making the parser guess whether a real `Self` type exists. Empty type bodies
now build `DECL_TRAIT`, while a unit field is enough to make a struct. Syntax
recovery for a broken field is not reclassified as a trait. Trait members and
signatures still await the member grammar; native trait support is not added.

Qualified symbols such as `models.Db.:!bad[str]` retain their module path in
types, values and patterns. The final module name must start uppercase; tags
still start lowercase. Shared `:ok` and `:none` never have a qualifier. Parsing
does not load modules or resolve external tag identities.

- `01`–`14`: one topic each, from declarations to comments. Comments in
  `03-precedence.brf` and `09-generic-calls.brf` give the grouping the tree
  must show.
- `15`–`20`: file-level edge cases: CRLF line endings, no final newline, only
  comments, an empty file, trailing blank lines, and UTF-8 text. `.gitattributes`
  preserves the deliberate CRLF bytes in `15` and permits them in diff checks.
- `21`: a realistic program that combines the features.
- `30`–`34`: generated stress files. They are deep (but under the 128-level
  limit) or wide (hundreds of arguments, fields, arms, and functions).
- `35`: postfix indexing, multiline delimiters, literal/match index expressions,
  control headers, chains and indexed assignment/update syntax.
- `36`: the import fixture promoted from `fail/later-import.brf`; syntax support
  does not implement module loading.
- `37`: typed array/vector literals, nesting, layout, chains and header escapes;
  these remain unimplemented at native compilation.
- `38`: error-tag kind spelling in types, values, patterns and postfix
  expressions.
- `39`: bracketed payload lists in types, values and patterns, including
  nesting, empty/unit payloads, multiple positions, layout and trailing commas.
- `40`: union types in aliases/fields/parameters/returns/type arguments, nested
  payload unions, bitwise-or separation and ordinary `Sym` names.
- `41`: typed payload binders, grouping, nested/union/generic annotations and
  multiple typed positions, without native matching claims.
- `42`: ordinary `Self` names in declarations, annotations, type arguments,
  literals, calls and named-set pattern roles.
- `43`: empty/comment-only traits distinguished from a struct with a unit field.
- `44`: qualified neutral/error symbols, payload/type nesting, named-set roles,
  multiline paths and ordinary bitwise-or separation.
- `underscore-type-syntax`: promoted from the old parser rejection; semantic
  checking still rejects its bare partial return marker.

## `fail/`

There are 106 rejection fixtures, including `qualified-shared-symbol.brf`,
`qualified-symbol-missing-name.brf` and `pub-trait.brf`,
`typed-pattern-outside-payload.brf`, `typed-payload-missing-name.brf`,
`typed-payload-missing-type.brf`, `old-symbol-union.brf`,
`union-missing-member.brf`, `union-missing-separator.brf`,
`old-payload-parentheses.brf`, `payload-missing-close.brf`,
`payload-missing-comma.brf`, `error-tag-spacing.brf`, `collection-header.brf`,
`collection-missing-comma.brf`, `bare-collection-literal.brf`,
`import-missing-name.brf`, `import-in-body.brf`, `tag-casing.brf`,
`tag-wildcard.brf`, `declaration-casing.brf`, `declaration-wildcard.brf`,
`old-type-arguments.brf`, `old-array-type.brf`, `old-index.brf`,
`index-missing-close.brf`, `pub-struct.brf`, `old-return-colon.brf`,
`old-alias-keyword.brf`, `pub-alias.brf` and `struct-literal-old-colon.brf`.
`struct-literal-missing-equals.brf` retains the old missing-delimiter rule under
its new spelling/name. The converted function headers retain each existing
fixture's original error rule and line. The two old `sym-*` delimiter files now
check missing union members/separators on their original error lines: the old
angle-closing/newline rule no longer exists.

Every file must be rejected: `brief dump-ast` exits with status 1 and prints no
tree. Each file starts with a header:

```
// parse-error: line 5
// Why: the rule the file breaks, and where the docs state it.
```

- `// parse-error: line N` gives the line of an expected diagnostic, counted
  from the top of the file (header included). `// parse-error: end of file`
  means the error is reported at the end of the input. The header is not the
  `// error:` comment used by `tests/programs/`.
- In most files only the first diagnostic is checked against the first
  `// parse-error:` line. A file may produce follow-on errors after it.
- Files named `recovery-*` list every expected diagnostic in order. The parser
  must report exactly those lines, which checks that one error does not hide
  later ones or cause a cascade.
- `old-trait-keyword.brf` and `old-override-modifier.brf` (formerly `later-*`)
  test removed prefixes, not syntax that should eventually become valid. Their
  error lines are preserved; modern fieldless-trait detection is still pending.
- Other files named `later-*` use syntax from later versions (lambdas, traits,
  tuples). Move them to `pass/` as each feature's syntax is added.
- Files named `limit-*` exceed the parser's 128-level nesting limit
  (`docs/plans/first-version.txt`, "Implementation limits").
