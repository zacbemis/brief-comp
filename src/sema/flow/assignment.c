#include "sema/flow/flow.h"

// Must-facts: both properties intersect at joins. Neither means that paths
// disagree. Declarations reset the facts for each lexical/loop generation;
// persistent loop headers only lose bits, so the shared walker converges.
enum { ASSIGNED = 1, UNASSIGNED = 2 };
typedef struct {
  Diagnostics *errors;
  size_t count;
  unsigned char *reported;
} Assignment;
static int tracked(SemaVar *v) {
  return v && v->deferred && v->type->kind >= SEMA_UNIT;
}
static void reject(SemaFlow *flow, SemaExpr *e, SemaVar *v, unsigned category,
                   const char *message) {
  Assignment *a = flow->context;
  if (flow->probe || !flow->active)
    return;
  // Suppress outcome follow-ups on this invalid read/write, not probe facts.
  e->assignment_invalid = 1;
  if (a->reported[v->id] & category)
    return;
  a->reported[v->id] |= (unsigned char)category;
  Diagnostic *d = diagnostics_error(a->errors, e->span, message);
  diagnostic_note(a->errors, d, v->span, "The binding is declared here.");
}
static int merge(SemaFlow *flow, void *into, const void *from, SemaExpr *at) {
  Assignment *a = flow->context;
  unsigned char *x = into;
  const unsigned char *y = from;
  int changed = 0;
  (void)at;
  for (size_t i = 0; i < a->count; i++) {
    unsigned char next = x[i] & y[i];
    changed |= next != x[i];
    x[i] = next;
  }
  return changed;
}
static SemaFlowValue enter(SemaFlow *flow, SemaExpr *e, void *state) {
  unsigned char *bits = state;
  if (e->kind == SEMA_EXPR_BIND && tracked(e->as.binding.variable))
    bits[e->as.binding.variable->id] = UNASSIGNED;
  if (!flow->active)
    return 0;
  if (e->kind == SEMA_EXPR_VAR && tracked(e->as.variable)) {
    SemaVar *v = e->as.variable;
    if (!(bits[v->id] & ASSIGNED))
      reject(flow, e, v, 1, "Binding must be assigned before it is read.");
  } else if ((e->kind == SEMA_EXPR_ASSIGN || e->kind == SEMA_EXPR_UPDATE) &&
             tracked(e->as.binding.variable) &&
             (e->kind == SEMA_EXPR_UPDATE || e->as.binding.op != ASSIGN_SET)) {
    SemaVar *v = e->as.binding.variable;
    // An immutable compound/update target already has the ordinary checker
    // diagnostic. A mutable one still needs an assigned value to read.
    if (!v->mutable) {
      if (!flow->probe)
        e->assignment_invalid = 1;
    } else if (!(bits[v->id] & ASSIGNED))
      reject(flow, e, v, 1, "Binding must be assigned before it is read.");
  }
  return 0;
}
static SemaFlowValue leave(SemaFlow *flow, SemaExpr *e, void *state,
                           SemaFlowValue result) {
  (void)result;
  if (!flow->active || e->kind != SEMA_EXPR_ASSIGN ||
      !tracked(e->as.binding.variable) ||
      !(e->as.binding.value->flow & SEMA_NEXT))
    return 0;
  SemaVar *v = e->as.binding.variable;
  unsigned char *bits = state;
  if (e->as.binding.op == ASSIGN_SET && !v->mutable &&
      !(bits[v->id] & UNASSIGNED))
    reject(flow, e, v, 2,
           "A declared let binding can only be assigned once on every path.");
  if (sema_assignable(e->as.binding.value->type, v->type))
    bits[v->id] = ASSIGNED;
  return 0;
}
void sema_check_assignment(SemaProgram *program, Diagnostics *errors) {
  for (size_t i = 0; i < program->count; i++) {
    SemaFunction *f = &program->functions[i];
    int has_deferred = 0;
    for (size_t j = 0; j < f->variable_count; j++)
      has_deferred |= tracked(f->variables[j]);
    if (!has_deferred)
      continue;
    Assignment a = {.errors = errors,
                    .count = f->variable_count,
                    .reported =
                        arena_calloc(program->arena, f->variable_count, 1)};
    unsigned char *entry = arena_calloc(program->arena, a.count, 1);
    SemaFlowAnalysis analysis = {
        .state_size = a.count, .join = merge, .enter = enter, .leave = leave};
    sema_flow_run(program->arena, &analysis, &a, f->body, entry);
  }
}
