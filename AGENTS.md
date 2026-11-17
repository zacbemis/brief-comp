# Agent guide

- **Active plan:** `docs/plans/architecture.txt`. Work one milestone at a time;
  each ends with every test passing and clang-format clean.
- **Current language:** `docs/lang-*.txt`. The next language is decided in the
  plan's "Language decisions" and sketched in `docs/next/`; where they differ
  from `docs/lang-*.txt`, the plan wins for new work.
- **Ignore `docs/history/`** unless a task asks for it. It holds superseded
  versions, plans and reviews.
- **Build and test:** `./build.sh` (about 4 s), then
  `ctest --test-dir build --parallel "$(nproc)"` (about 30–40 s; `./test.sh`
  does both). Format with `clang-format -i` (version 23.1.3, `.clang-format`).
- **Fast loop while working:** rebuild with `cmake --build build`, then run
  only the affected suites, for example `ctest --test-dir build -R 'parser'` or
  a unit binary directly (`build/tests/test_parser_types`). Skip the slow
  native and sanitized suites until the end with `-E 'sanitized|cli_native'`.
  Run the full suite once before each commit.
- **Program fixtures** run in eight CTest shards (`test_programs_0`–`7`, and
  `test_programs_sanitized_*`); keep new long-running suites shardable so they
  parallelize.
- **Structure:** each pass keeps private declarations in `src/<pass>/internal.h`;
  files stay under about 500 lines. Tests drive the compiler through
  `include/driver/compile.h` and share helpers in `tests/compile_support.h`.
- **No compatibility with removed syntax:** removed spellings get ordinary
  syntax errors, with no recovery, fix-its or tests for them.
- **Commits:** Conventional Commits, imperative mood, no first person, no AI
  attribution.
