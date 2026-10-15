# Parser example files

Source files for parser tests. Nothing runs them yet.

## `pass/`

Every file must parse with no diagnostics: `brief dump-ast` exits with status 0
and prints a tree. Files use only first-version syntax.

- `01`–`14`: one topic each, from declarations to comments. Comments in
  `03-precedence.brf` and `09-generic-calls.brf` give the grouping the tree
  must show.
- `15`–`20`: file-level edge cases: CRLF line endings, no final newline, only
  comments, an empty file, trailing blank lines, and UTF-8 text.
- `21`: a realistic program that combines the features.
- `30`–`34`: generated stress files. They are deep (but under the 128-level
  limit) or wide (hundreds of arguments, fields, arms, and functions).

## `fail/`

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
- Files named `later-*` use syntax from later versions (lambdas, traits,
  tuples, imports). Move them to `pass/` as each feature is added.
- Files named `limit-*` exceed the parser's 128-level nesting limit
  (`docs/plans/first-version.txt`, "Implementation limits").
