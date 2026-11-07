#include "sema/flow.h"

// May-states join by union. Each successful assignment creates a new pending
// value; it does not discharge an older one. Poison is reporting-only, never
// a probe fact that could hide a loop's first inconsistent join.
enum { PENDING = 1, HANDLED = 2, INVALID = 7 };
typedef enum { PATH, FALSE_PATH, TRUE_PATH, LOOP_ENTRY, BACK_EDGE } Path;
typedef struct {
  unsigned bits;
  Span pending;
  Path path;
  SemaExpr *join; // First inconsistent join, including through loop headers.
} State;
typedef struct Scope Scope;
struct Scope {
  SemaExpr *block;
  Scope *outer;
};
typedef struct Loop Loop;
struct Loop {
  SemaExpr *body, *join;
  Loop *outer;
};
typedef struct Report Report;
struct Report {
  SemaExpr *join;
  Report *next;
};
typedef struct {
  Arena *arena;
  Diagnostics *errors;
  size_t count;
  SemaExpr **declarations, **scopes;
  Report **reported;
  Scope *scope;
  Loop *loop;
} Outcomes;
static void declaration(Outcomes *o, Diagnostic *d, size_t id) {
  diagnostic_note(o->errors, d, o->declarations[id]->span,
                  "The outcome binding is declared here.");
}
static void reject(SemaFlow *flow, State *state, size_t id, Span span,
                   const char *message) {
  Outcomes *o = flow->context;
  if (state[id].bits == INVALID || flow->probe || !flow->active)
    return;
  state[id].bits = INVALID;
  Diagnostic *d = diagnostics_error(o->errors, span, message);
  declaration(o, d, id);
}
static void mixed(SemaFlow *flow, State *state, size_t id) {
  Outcomes *o = flow->context;
  State *s = &state[id];
  if (flow->probe || !flow->active)
    return;
  s->bits = INVALID;
  for (Report *r = o->reported[id]; r; r = r->next)
    if (r->join == s->join)
      return;
  Report *r = arena_alloc(o->arena, sizeof(*r));
  *r = (Report){s->join, o->reported[id]};
  o->reported[id] = r;
  Diagnostic *d = diagnostics_error(
      o->errors, s->join->span,
      "Outcome binding is handled on some paths but not others.");
  const char *note =
      s->path == FALSE_PATH
          ? "The outcome is still pending when this condition is false."
      : s->path == TRUE_PATH
          ? "The outcome is still pending when this condition is true."
      : s->path == LOOP_ENTRY
          ? "The outcome is still pending on entry to this loop."
      : s->path == BACK_EDGE
          ? "The outcome is still pending on this loop back edge."
          : "The outcome is still pending on this path.";
  diagnostic_note(o->errors, d, s->pending, note);
  declaration(o, d, id);
}
static void pending(SemaFlow *flow, State *state, size_t id, Span at) {
  if (state[id].bits == PENDING)
    reject(flow, state, id, at,
           "Outcome binding is not handled before leaving its scope.");
  else if (state[id].bits == (PENDING | HANDLED))
    mixed(flow, state, id);
}
static void exits(SemaFlow *flow, State *state, SemaExpr *at, SemaExpr *region,
                  int clear) {
  Outcomes *o = flow->context;
  for (size_t i = 0; i < o->count; i++) {
    SemaExpr *decl = o->declarations[i];
    if (decl && (!region || (decl->span.start >= region->span.start &&
                             decl->span.end <= region->span.end))) {
      pending(flow, state, i, at->span);
      if (clear)
        state[i] = (State){0};
    }
  }
}
static int merge(SemaFlow *flow, void *into, const void *from, SemaExpr *at) {
  Outcomes *o = flow->context;
  State *a = into;
  const State *b = from;
  int changed = 0;
  // Break/continue snapshots meet at the enclosing loop's exit/header,
  // not at the source location of whichever jump happened to arrive last.
  if ((at->kind == SEMA_EXPR_BREAK || at->kind == SEMA_EXPR_CONTINUE) &&
      o->loop)
    at = o->loop->join;
  for (size_t i = 0; i < o->count; i++) {
    unsigned old = a[i].bits, next = old | b[i].bits;
    if (next == (PENDING | HANDLED)) {
      // Preserve the witness of the first real disagreement. A persistent
      // loop header can already be mixed before the reporting walk begins.
      State witness = a[i].join        ? a[i]
                      : b[i].join      ? b[i]
                      : old == PENDING ? a[i]
                                       : b[i];
      a[i] = witness;
      a[i].bits = next;
      if (!a[i].join)
        a[i].join = at;
      mixed(flow, a, i);
    } else {
      if (!old && b[i].bits)
        a[i] = b[i];
      a[i].bits = next;
    }
    changed |= a[i].bits != old;
  }
  return changed;
}
static void path(SemaFlow *flow, State *state, Span at, Path kind) {
  Outcomes *o = flow->context;
  if (!flow->active)
    return;
  for (size_t i = 0; i < o->count; i++)
    if (state[i].bits == PENDING) {
      state[i].pending = at;
      state[i].path = kind;
    }
}
static Span end(SemaExpr *e) {
  Span span = e->span;
  if (e->kind == SEMA_EXPR_BLOCK && span.end > span.start)
    span.start = span.end - 1;
  return span;
}
static SemaFlowValue enter(SemaFlow *flow, SemaExpr *e, void *state) {
  Outcomes *o = flow->context;
  State *facts = state;
  if (e->kind == SEMA_EXPR_BLOCK) {
    Scope *scope = arena_alloc(o->arena, sizeof(*scope));
    *scope = (Scope){e, o->scope};
    o->scope = scope;
  } else if (e->kind == SEMA_EXPR_WHILE || e->kind == SEMA_EXPR_FOR) {
    Loop *loop = arena_alloc(o->arena, sizeof(*loop));
    SemaExpr *body = e->kind == SEMA_EXPR_WHILE ? e->as.while_loop.body
                                                : e->as.for_loop.body;
    *loop = (Loop){body, e, o->loop};
    o->loop = loop;
    path(flow, facts,
         e->kind == SEMA_EXPR_WHILE ? e->as.while_loop.condition->span
                                    : e->as.for_loop.lower->span,
         LOOP_ENTRY);
  } else if (e->kind == SEMA_EXPR_BIND) {
    SemaVar *v = e->as.binding.variable;
    if (v->outcome_binding) {
      o->declarations[v->id] = e;
      o->scopes[v->id] = o->scope ? o->scope->block : NULL;
    }
  }
  if (!flow->active)
    return 0;
  if (e->kind == SEMA_EXPR_VAR && e->as.variable &&
      e->as.variable->outcome_binding) {
    size_t id = e->as.variable->id;
    if (facts[id].bits == INVALID || !facts[id].bits)
      return 0;
    if (facts[id].bits == (PENDING | HANDLED))
      mixed(flow, facts, id);
    else if (facts[id].bits == HANDLED)
      reject(flow, facts, id, e->span,
             "Outcome binding has already been handled.");
    else if (!e->handles_outcome)
      reject(flow, facts, id, e->span,
             "Outcome binding must be handled with ?, !, a match, or return.");
    else
      facts[id].bits = HANDLED;
  } else if ((e->kind == SEMA_EXPR_ASSIGN || e->kind == SEMA_EXPR_UPDATE) &&
             e->as.binding.variable &&
             e->as.binding.variable->outcome_binding) {
    SemaVar *v = e->as.binding.variable;
    size_t id = v->id;
    if (facts[id].bits == (PENDING | HANDLED))
      mixed(flow, facts, id);
    else if (!v->mutable || e->kind != SEMA_EXPR_ASSIGN ||
             e->as.binding.op != ASSIGN_SET)
      reject(flow, facts, id, e->span, "Outcome binding cannot be reassigned.");
    // Evaluate the RHS with the old slot's state. It may handle that value
    // or leave the function; only an actual write can create a replacement.
  } else if ((e->kind == SEMA_EXPR_BREAK || e->kind == SEMA_EXPR_CONTINUE) &&
             o->loop) {
    // The walker snapshots jumps before leave(). Discharge departing locals
    // here so their facts cannot leak into a loop header or an outer scope.
    exits(flow, facts, e, o->loop->body, 1);
    path(flow, facts, e->span,
         e->kind == SEMA_EXPR_CONTINUE ? BACK_EDGE : PATH);
  }
  return 0;
}
static SemaFlowValue child(SemaFlow *flow, SemaExpr *e, size_t index,
                           void *state, SemaFlowValue result,
                           SemaFlowValue value) {
  (void)result;
  (void)value;
  if (e->kind == SEMA_EXPR_IF) {
    SemaExpr *branch =
        index == 1 ? e->as.conditional.yes : e->as.conditional.no;
    if (!index)
      path(flow, state, e->as.conditional.condition->span, FALSE_PATH);
    else if (branch->flow & SEMA_NEXT)
      path(flow, state, end(branch), PATH);
  } else if (e->kind == SEMA_EXPR_MATCH && index) {
    SemaArm *arm = &e->as.match.arms[index - 1];
    if (arm->useful && (arm->body->flow & SEMA_NEXT))
      path(flow, state, end(arm->body), PATH);
  } else if (e->kind == SEMA_EXPR_BINARY &&
             (e->as.binary.op == BINARY_LOGICAL_AND ||
              e->as.binary.op == BINARY_LOGICAL_OR)) {
    if (!index)
      path(flow, state, e->as.binary.left->span,
           e->as.binary.op == BINARY_LOGICAL_AND ? FALSE_PATH : TRUE_PATH);
    else
      path(flow, state, end(e->as.binary.right), PATH);
  } else if ((e->kind == SEMA_EXPR_WHILE && index == 1) ||
             (e->kind == SEMA_EXPR_FOR && index == 2)) {
    SemaExpr *body = e->kind == SEMA_EXPR_WHILE ? e->as.while_loop.body
                                                : e->as.for_loop.body;
    if (body->flow & SEMA_NEXT)
      path(flow, state, end(body), BACK_EDGE);
  }
  return 0;
}
static SemaFlowValue leave(SemaFlow *flow, SemaExpr *e, void *state,
                           SemaFlowValue result) {
  Outcomes *o = flow->context;
  State *facts = state;
  (void)result;
  if (e->kind == SEMA_EXPR_BIND && flow->active &&
      (e->as.binding.value->flow & SEMA_NEXT) &&
      e->as.binding.variable->outcome_binding)
    facts[e->as.binding.variable->id] =
        (State){.bits = PENDING, .pending = e->span};
  if (e->kind == SEMA_EXPR_ASSIGN && flow->active && e->as.binding.variable &&
      e->as.binding.variable->outcome_binding) {
    SemaVar *v = e->as.binding.variable;
    if (v->mutable && e->as.binding.op == ASSIGN_SET &&
        (e->as.binding.value->flow & SEMA_NEXT)) {
      if (facts[v->id].bits == (PENDING | HANDLED))
        mixed(flow, facts, v->id);
      else if (facts[v->id].bits == PENDING)
        reject(flow, facts, v->id, e->span,
               "Outcome binding cannot be reassigned.");
      else if (facts[v->id].bits == HANDLED &&
               sema_assignable(e->as.binding.value->type, v->type))
        facts[v->id] = (State){.bits = PENDING, .pending = e->span};
    }
  }
  if (flow->active) {
    if (e->kind == SEMA_EXPR_RETURN &&
        (!e->as.value || (e->as.value->flow & SEMA_NEXT)))
      exits(flow, facts, e, NULL, 0);
    else if (e->kind == SEMA_EXPR_TRY && e->failures &&
             (e->as.value->flow & SEMA_NEXT))
      exits(flow, facts, e, NULL, 0);
  }
  if (e->kind == SEMA_EXPR_BLOCK) {
    for (size_t i = 0; i < o->count; i++)
      if (o->scopes[i] == e) {
        if (flow->active && (e->flow & SEMA_NEXT))
          pending(flow, facts, i, end(e));
        facts[i] = (State){0};
      }
    o->scope = o->scope->outer;
  } else if (e->kind == SEMA_EXPR_WHILE || e->kind == SEMA_EXPR_FOR)
    o->loop = o->loop->outer;
  return 0;
}
void sema_check_outcomes(SemaProgram *program, Diagnostics *errors) {
  for (size_t i = 0; i < program->count; i++) {
    SemaFunction *f = &program->functions[i];
    int has_outcomes = 0;
    for (size_t j = 0; j < f->variable_count; j++)
      has_outcomes |= f->variables[j]->outcome_binding;
    if (!has_outcomes)
      continue;
    Outcomes o = {
        .arena = program->arena,
        .errors = errors,
        .count = f->variable_count,
        .declarations = arena_calloc(program->arena, f->variable_count,
                                     sizeof(*o.declarations)),
        .scopes =
            arena_calloc(program->arena, f->variable_count, sizeof(*o.scopes)),
        .reported = arena_calloc(program->arena, f->variable_count,
                                 sizeof(*o.reported))};
    State *entry = arena_calloc(program->arena, o.count, sizeof(*entry));
    SemaFlowAnalysis analysis = {.state_size = o.count * sizeof(*entry),
                                 .join = merge,
                                 .enter = enter,
                                 .child = child,
                                 .leave = leave};
    sema_flow_run(program->arena, &analysis, &o, f->body, entry);
  }
}
