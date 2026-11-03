#ifndef BRIEF_SEMA_FLOW_H
#define BRIEF_SEMA_FLOW_H

#include "sema/sema.h"

// Forward data flow over the checked tree. State is a fixed-size, trivially
// copyable byte buffer supplied by the analysis. Expression values are opaque
// to the walker; hooks can use them to capture a child's result before later
// children change the state (for example, an argument's access permission).
typedef size_t SemaFlowValue;
typedef struct {
  void *context;
  int probe;  // Fixed-point pass: hooks must suppress diagnostics/side effects.
  int active; // This path is live; dead syntax is still visited for checking.
} SemaFlow;

typedef struct {
  size_t state_size;
  // Join into the first incoming state, returning nonzero iff it changes.
  // Must be monotone and converge. 'at' is the branch/loop/jump being joined.
  int (*join)(SemaFlow *flow, void *into, const void *from, SemaExpr *at);
  // Optional hooks. enter/leave see the node's incoming active flag; child
  // runs just after evaluation, before subsequent children, with that child's
  // incoming flag. Hook state mutations feed subsequent evaluation and joins.
  SemaFlowValue (*enter)(SemaFlow *flow, SemaExpr *expr, void *state);
  SemaFlowValue (*child)(SemaFlow *flow, SemaExpr *expr, size_t index,
                         void *state, SemaFlowValue result,
                         SemaFlowValue value);
  SemaFlowValue (*leave)(SemaFlow *flow, SemaExpr *expr, void *state,
                         SemaFlowValue result);
  // Initialize each arm's bindings from the already captured match input.
  void (*arm)(SemaFlow *flow, SemaArm *arm, void *state, SemaFlowValue input);
} SemaFlowAnalysis;

// Child indices follow evaluation order, except assignment always reserves 0
// for the target object and 1 for the RHS (a local target is not read). if uses
// condition/yes/no = 0/1/2; match uses input = 0, arm bodies = 1 onwards;
// while uses condition/body = 0/1; for uses lower/upper/body = 0/1/2.
// Pattern endpoints are compile-time checks, not runtime evaluations.
//
// Loop headers persist across whole-tree probe passes, not recursive loop
// iteration. After convergence, hooks run once with probe == 0. Entry state is
// copied on every pass and never modified. No state persists between runs.
void sema_flow_run(Arena *arena, const SemaFlowAnalysis *analysis,
                   void *context, SemaExpr *body, const void *entry);

#endif
