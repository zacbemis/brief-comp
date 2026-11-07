#include "sema/flow.h"

// May-states join by union. Pending|handled cannot be consumed as though
// either incoming path were certain. An invalid obligation is absorbing so
// one bad transfer does not manufacture follow-up use/exit diagnostics.
enum { PENDING = 1, HANDLED = 2, INVALID = 7 };
typedef struct Scope Scope;
struct Scope {
  SemaExpr *block;
  Scope *outer;
};
typedef struct Loop Loop;
struct Loop {
  SemaExpr *body;
  Loop *outer;
};
typedef struct {
  Arena *arena;
  Diagnostics *errors;
  size_t count;
  SemaExpr **declarations, **scopes;
  Scope *scope;
  Loop *loop;
} Outcomes;
static void reject(SemaFlow *flow, unsigned char *state, size_t id, Span span,
                   const char *message) {
  Outcomes *o = flow->context;
  if (state[id] == INVALID || flow->probe)
    return;
  // Poison only the reporting pass. A poison bit in a persistent probe
  // header would hide the very violation the final loop visit must report.
  state[id] = INVALID;
  Diagnostic *d = diagnostics_error(o->errors, span, message);
  diagnostic_note(o->errors, d, o->declarations[id]->span,
                  "The outcome binding is declared here.");
}
static void pending(SemaFlow *flow, unsigned char *state, size_t id, Span at) {
  if (state[id] == PENDING)
    reject(flow, state, id, at,
           "Outcome binding is not handled before leaving its scope.");
  else if (state[id] == (PENDING | HANDLED))
    reject(flow, state, id, at,
           "Outcome binding is handled on some paths but not others.");
}
static void exits(SemaFlow *flow, unsigned char *state, SemaExpr *at,
                  SemaExpr *region) {
  Outcomes *o = flow->context;
  for (size_t i = 0; i < o->count; i++) {
    SemaExpr *decl = o->declarations[i];
    if (decl && (!region || (decl->span.start >= region->span.start &&
                             decl->span.end <= region->span.end)))
      pending(flow, state, i, at->span);
  }
}
static int merge(SemaFlow *flow, void *into, const void *from, SemaExpr *at) {
  Outcomes *o = flow->context;
  unsigned char *a = into;
  const unsigned char *b = from;
  int changed = 0;
  (void)at;
  for (size_t i = 0; i < o->count; i++) {
    unsigned char next = a[i] | b[i];
    changed |= a[i] != next;
    a[i] = next;
  }
  return changed;
}
static SemaFlowValue enter(SemaFlow *flow, SemaExpr *e, void *state) {
  Outcomes *o = flow->context;
  unsigned char *bits = state;
  if (e->kind == SEMA_EXPR_BLOCK) {
    Scope *scope = arena_alloc(o->arena, sizeof(*scope));
    *scope = (Scope){e, o->scope};
    o->scope = scope;
  } else if (e->kind == SEMA_EXPR_WHILE || e->kind == SEMA_EXPR_FOR) {
    Loop *loop = arena_alloc(o->arena, sizeof(*loop));
    SemaExpr *body = e->kind == SEMA_EXPR_WHILE ? e->as.while_loop.body
                                                : e->as.for_loop.body;
    *loop = (Loop){body, o->loop};
    o->loop = loop;
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
    if (bits[id] == INVALID || !bits[id])
      return 0;
    if (bits[id] == (PENDING | HANDLED))
      reject(flow, bits, id, e->span,
             "Outcome binding is handled on some paths but not others.");
    else if (bits[id] == HANDLED)
      reject(flow, bits, id, e->span,
             "Outcome binding has already been handled.");
    else if (!e->handles_outcome)
      reject(flow, bits, id, e->span,
             "Outcome binding must be handled with ?, !, a match, or return.");
    else
      bits[id] = HANDLED;
  } else if ((e->kind == SEMA_EXPR_ASSIGN || e->kind == SEMA_EXPR_UPDATE) &&
             e->as.binding.variable &&
             e->as.binding.variable->outcome_binding) {
    size_t id = e->as.binding.variable->id;
    reject(flow, bits, id, e->span, "Outcome binding cannot be reassigned.");
  }
  return 0;
}
static SemaFlowValue leave(SemaFlow *flow, SemaExpr *e, void *state,
                           SemaFlowValue result) {
  Outcomes *o = flow->context;
  unsigned char *bits = state;
  (void)result;
  if (e->kind == SEMA_EXPR_BIND && flow->active &&
      (e->as.binding.value->flow & SEMA_NEXT) &&
      e->as.binding.variable->outcome_binding)
    bits[e->as.binding.variable->id] = PENDING;
  if (flow->active) {
    if (e->kind == SEMA_EXPR_RETURN &&
        (!e->as.value || (e->as.value->flow & SEMA_NEXT)))
      exits(flow, bits, e, NULL);
    else if (e->kind == SEMA_EXPR_TRY && e->failures &&
             (e->as.value->flow & SEMA_NEXT))
      exits(flow, bits, e, NULL); // The error edge leaves the function too.
    else if ((e->kind == SEMA_EXPR_BREAK || e->kind == SEMA_EXPR_CONTINUE) &&
             o->loop)
      exits(flow, bits, e, o->loop->body);
  }
  if (e->kind == SEMA_EXPR_BLOCK) {
    for (size_t i = 0; i < o->count; i++)
      if (o->scopes[i] == e) {
        if (flow->active && (e->flow & SEMA_NEXT)) {
          Span end = e->span;
          if (end.end > end.start)
            end.start = end.end - 1;
          pending(flow, bits, i, end);
        }
        bits[i] = 0;
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
            arena_calloc(program->arena, f->variable_count, sizeof(*o.scopes))};
    unsigned char *entry = arena_calloc(program->arena, o.count, 1);
    SemaFlowAnalysis analysis = {
        .state_size = o.count, .join = merge, .enter = enter, .leave = leave};
    sema_flow_run(program->arena, &analysis, &o, f->body, entry);
  }
}
