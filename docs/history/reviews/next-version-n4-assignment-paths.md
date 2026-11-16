# N4 slice: definite-assignment paths, loops, nesting and recovery

Base: `d39c2ac`, committed unchanged. This completes the N4 checklist item that
hardens definite assignment across joins, exits, loop back edges, nesting and
recovery. Let-else, inclusive ranges, typed loop variables and the N4 chapter
audits remain open.

## Change

One production line changes. `mutable()` in `src/sema/check.c` now reports
"Cannot reassign an immutable binding." for a deferred `let` even when it holds
an outcome. Before this, a compound or update write to a deferred outcome `let`
(`r += :!bad`, `r++`, `r--`) was handed to the outcome analysis, and the
assignment analysis's recovery then suppressed that later check, so the write
was accepted. Plain first assignments to a deferred `let` still bypass the check
and are decided by flow. The shared walker, the assignment and outcome analyses,
lowering, RC, runtime and the language chapters are unchanged.

## Changed existing expectations

None. All old assertions, fixture and marker bytes, goldens, `.expect` files,
parser and audit classifications and generated audit cases remain. The new
probes show 21 formerly accepted bad shapes: five compound operators and both
updates, each in the unassigned, pending and handled states. Each now rejects
once before C emission. None replaces an old negative case.

## New tests

`test_assignment_paths.c` adds nine groups: must-fact intersections across
nested if/match, returning paths, break/continue joins, normal back edges,
repeated loop conditions, once-only range bounds, RHS evaluation order, loop
generations and deep nesting. They also check that independent type, read and
write errors stay visible, that dead code keeps ordinary type checks, and that
read-only permissions are unchanged.

Three native fixtures:

- `assignment-paths-n4.brf` initializes owned values in branches, loop breaks,
  range bounds and RHS expressions that return early.
- `main-assignment-paths-n4.brf` propagates an owned error out of a loop before
  the loop's deferred bindings are stored.
- `errors/assignment-paths-n4.brf` pins seventeen rejection sites with ten
  declaration notes. They cover missing branch, match, loop and break
  assignments; let writes in back edges, nested loops, conditions and bounds;
  argument order; the immutable compound and update writes; and invalid RHS
  recovery.

## Validation

Default GCC Debug and a fresh `-Werror` Clang Debug ASan/UBSan build pass 54/54
each. Against a fresh HEAD baseline, all 3,189 retained sources match: 6,378
AST and emit observations, 1,397 successful C outputs and 294 prior sanitized
runs. Eight strict native runs and two CLI debug runs pass. Every operator
probe stops emit/build/run before a nonexistent C compiler. Formatting, diff,
prose and protected-plan checks pass. The full four-configuration matrix is
left for N4 closure.
