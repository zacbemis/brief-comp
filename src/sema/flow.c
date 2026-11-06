#include "sema/flow.h"
#include <string.h>

typedef struct Loop Loop;
struct Loop {
  void *breaks, *continues;
  Loop *outer;
};
typedef struct Header Header;
struct Header {
  SemaExpr *body;
  void *state;
  Header *next;
};
typedef struct {
  SemaFlow flow;
  Arena *arena;
  const SemaFlowAnalysis *analysis;
  int changed;
  Loop *loop;
  Header *headers;
} Walk;

static void *copy(Walk *w, const void *state) {
  size_t size = w->analysis->state_size;
  void *result = arena_alloc(w->arena, size);
  if (size)
    memcpy(result, state, size);
  return result;
}
static void replace(Walk *w, void *into, const void *from) {
  if (w->analysis->state_size)
    memcpy(into, from, w->analysis->state_size);
}
static int join(Walk *w, void *into, const void *from, SemaExpr *at) {
  return w->analysis->join(&w->flow, into, from, at);
}
// The first live edge supplies a state, not an all-zero buffer: analyses with
// intersection joins need a different identity from permissions' union join.
static void incoming(Walk *w, void **into, const void *from, SemaExpr *at) {
  if (*into)
    join(w, *into, from, at);
  else
    *into = copy(w, from);
}
static SemaFlowValue visit(Walk *w, SemaExpr *e, void *state);
static SemaFlowValue child(Walk *w, SemaExpr *e, size_t index, void *state,
                           SemaFlowValue result, SemaFlowValue value) {
  return w->analysis->child
             ? w->analysis->child(&w->flow, e, index, state, result, value)
             : result;
}
static SemaFlowValue sequence(Walk *w, SemaExpr *e, SemaExpr *item,
                              size_t index, void *state, SemaFlowValue result) {
  SemaFlowValue value = visit(w, item, state);
  result = child(w, e, index, state, result, value);
  w->flow.active = w->flow.active && (item->flow & SEMA_NEXT);
  return result;
}
static void repeat(Walk *w, SemaExpr *e, SemaExpr *condition, SemaExpr *body,
                   void *state) {
  Header *header = w->headers;
  while (header && header->body != body)
    header = header->next;
  if (!header) {
    header = arena_alloc(w->arena, sizeof(*header));
    // A new header already holds this pass's entry state, so creating it is
    // not a change: only joins from back edges and continues can change it.
    *header = (Header){body, copy(w, state), w->headers};
    w->headers = header;
  } else
    w->changed |= join(w, header->state, state, e);
  // Iterate the whole function, rather than multiplying nested fixed points.
  void *exit = copy(w, header->state);
  int active = w->flow.active;
  if (condition)
    sequence(w, e, condition, 0, exit, 0);
  Loop loop = {.outer = w->loop};
  w->loop = &loop;
  void *body_state = copy(w, exit);
  sequence(w, e, body, condition ? 1 : 2, body_state, 0);
  w->loop = loop.outer;
  if (w->flow.active)
    w->changed |= join(w, header->state, body_state, e);
  if (loop.continues)
    w->changed |= join(w, header->state, loop.continues, e);
  w->flow.active = active;
  int has_exit = !condition ||
                 ((condition->flow & SEMA_NEXT) &&
                  !(condition->kind == SEMA_EXPR_BOOL && condition->integer));
  if (!has_exit && loop.breaks)
    replace(w, exit, loop.breaks); // Only breaks leave a while true loop.
  else if (loop.breaks)
    join(w, exit, loop.breaks, e);
  // With no exit, retain header/condition state for checking dead syntax.
  replace(w, state, exit);
}
static SemaFlowValue visit(Walk *w, SemaExpr *e, void *state) {
  int active = w->flow.active;
  SemaFlowValue result =
      w->analysis->enter ? w->analysis->enter(&w->flow, e, state) : 0;
  switch (e->kind) {
  case SEMA_EXPR_FIELD:
    result = sequence(w, e, e->as.field.object, 0, state, result);
    break;
  case SEMA_EXPR_BIND:
    if (e->as.binding.value)
      result = sequence(w, e, e->as.binding.value, 0, state, result);
    break;
  case SEMA_EXPR_ASSIGN:
  case SEMA_EXPR_UPDATE:
    if (e->as.binding.target) {
      SemaExpr *target = e->as.binding.target;
      result = sequence(w, e, target->as.field.object, 0, state, result);
    }
    if (e->as.binding.value)
      result = sequence(w, e, e->as.binding.value, 1, state, result);
    break;
  case SEMA_EXPR_CALL:
    for (size_t i = 0; i < e->as.call.count; i++)
      result = sequence(w, e, e->as.call.args[i], i, state, result);
    break;
  case SEMA_EXPR_SYMBOL:
    for (size_t i = 0; i < e->as.symbol.count; i++)
      result = sequence(w, e, e->as.symbol.payloads[i], i, state, result);
    break;
  case SEMA_EXPR_STRUCT:
    for (size_t i = 0; i < e->as.structure.count; i++)
      result = sequence(w, e, e->as.structure.values[i], i, state, result);
    break;
  case SEMA_EXPR_TRY:
  case SEMA_EXPR_PANIC:
  case SEMA_EXPR_RETURN:
    if (e->as.value)
      result = sequence(w, e, e->as.value, 0, state, result);
    break;
  case SEMA_EXPR_BLOCK:
    for (size_t i = 0; i < e->as.block.count; i++)
      result = sequence(w, e, e->as.block.items[i], i, state, result);
    break;
  case SEMA_EXPR_MATCH: {
    SemaExpr *input = e->as.match.input;
    SemaFlowValue value = visit(w, input, state);
    result = child(w, e, 0, state, result, value);
    void *joined = NULL;
    for (size_t i = 0; i < e->as.match.count; i++) {
      SemaArm *arm = &e->as.match.arms[i];
      void *arm_state = copy(w, state);
      w->flow.active = active && (input->flow & SEMA_NEXT) && arm->useful;
      if (w->analysis->arm)
        w->analysis->arm(&w->flow, arm, arm_state, value);
      result = sequence(w, e, arm->body, i + 1, arm_state, result);
      if (arm->useful && (arm->body->flow & SEMA_NEXT))
        incoming(w, &joined, arm_state, e);
    }
    if (joined)
      replace(w, state, joined);
    break;
  }
  case SEMA_EXPR_IF: {
    SemaExpr *condition = e->as.conditional.condition;
    SemaExpr *yes = e->as.conditional.yes, *no = e->as.conditional.no;
    result = sequence(w, e, condition, 0, state, result);
    void *yes_state = copy(w, state), *no_state = copy(w, state);
    w->flow.active = active && (condition->flow & SEMA_NEXT);
    result = sequence(w, e, yes, 1, yes_state, result);
    w->flow.active = active && (condition->flow & SEMA_NEXT);
    if (no)
      result = sequence(w, e, no, 2, no_state, result);
    int yf = (yes->flow & SEMA_NEXT) != 0, nf = !no || (no->flow & SEMA_NEXT);
    if (yf && nf) {
      replace(w, state, yes_state);
      join(w, state, no_state, e);
    } else if (yf)
      replace(w, state, yes_state);
    else if (nf)
      replace(w, state, no_state);
    break;
  }
  case SEMA_EXPR_BINARY: {
    SemaExpr *a = e->as.binary.left, *b = e->as.binary.right;
    result = sequence(w, e, a, 0, state, result);
    void *skipped = NULL;
    if (e->as.binary.op == BINARY_LOGICAL_AND ||
        e->as.binary.op == BINARY_LOGICAL_OR)
      skipped = copy(w, state);
    result = sequence(w, e, b, 1, state, result);
    if (skipped) {
      if (b->flow & SEMA_NEXT)
        join(w, state, skipped, e);
      else
        replace(w, state, skipped);
    }
    break;
  }
  case SEMA_EXPR_UNARY:
    result = sequence(w, e, e->as.unary.value, 0, state, result);
    break;
  case SEMA_EXPR_WHILE:
    repeat(w, e, e->as.while_loop.condition, e->as.while_loop.body, state);
    break;
  case SEMA_EXPR_FOR:
    result = sequence(w, e, e->as.for_loop.lower, 0, state, result);
    result = sequence(w, e, e->as.for_loop.upper, 1, state, result);
    repeat(w, e, NULL, e->as.for_loop.body, state);
    break;
  case SEMA_EXPR_BREAK:
  case SEMA_EXPR_CONTINUE:
    if (active && w->loop)
      incoming(w,
               e->kind == SEMA_EXPR_BREAK ? &w->loop->breaks
                                          : &w->loop->continues,
               state, e);
    break;
  case SEMA_EXPR_UNIT:
  case SEMA_EXPR_STRING:
  case SEMA_EXPR_ERROR:
  case SEMA_EXPR_INT:
  case SEMA_EXPR_FLOAT:
  case SEMA_EXPR_BOOL:
  case SEMA_EXPR_CHAR:
  case SEMA_EXPR_VAR:
    break;
  }
  w->flow.active = active;
  return w->analysis->leave ? w->analysis->leave(&w->flow, e, state, result)
                            : result;
}
void sema_flow_run(Arena *arena, const SemaFlowAnalysis *analysis,
                   void *context, SemaExpr *body, const void *entry) {
  Walk w = {.flow = {.context = context, .probe = 1, .active = 1},
            .arena = arena,
            .analysis = analysis};
  // Only changes to persistent loop headers require another pass. Branch
  // joins also report changes, but their temporary states are recomputed.
  do {
    w.changed = 0;
    visit(&w, body, copy(&w, entry));
  } while (w.changed);
  w.flow.probe = 0;
  visit(&w, body, copy(&w, entry));
}
