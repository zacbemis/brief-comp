# Agent guide

- **Active plan:** `docs/plans/architecture.txt`. Work one milestone at a time;
  each ends with every test passing and clang-format clean.
- **Current language:** `docs/lang-*.txt`. The next language is decided in the
  plan's "Language decisions" and sketched in `docs/next/`; where they differ
  from `docs/lang-*.txt`, the plan wins for new work.
- **Ignore `docs/history/`** unless a task asks for it. It holds superseded
  versions, plans and reviews.
- **Build and test:** `./build.sh`, then `ctest --test-dir build --parallel "$(nproc)"`.
  Format with `clang-format -i` (version 23.1.3, `.clang-format`).
- **Structure:** each pass keeps private declarations in `src/<pass>/internal.h`;
  files stay under about 500 lines. Tests drive the compiler through
  `include/driver/compile.h` and share helpers in `tests/compile_support.h`.
- **No compatibility with removed syntax:** removed spellings get ordinary
  syntax errors, with no recovery, fix-its or tests for them.
- **Commits:** Conventional Commits, imperative mood, no first person, no AI
  attribution.
