#include "sema/sema.h"
#include <string.h>

// Access permission is separate from ownership: retaining a reference does not
// upgrade it. State bits mean "may hold a read-only struct reference". Direct
// copies propagate the bit, as do struct literals with read-only children and
// struct call results whose type can reach a read-only argument's objects.
// A read-only struct cannot be stored in a field. These are local access
// rules, not deep const or a function purity guarantee.
typedef unsigned char *State;
typedef struct Loop Loop;
struct Loop {
  State breaks, continues;
  int has_break, has_continue;
  Loop *outer;
};
typedef struct Header Header;
struct Header {
  SemaExpr *body;
  State state;
  Header *next;
};
typedef struct {
  Arena *arena;
  Diagnostics *errors;
  // reach[a * structs + b]: a value of struct a can lead, through fields, to a
  // value of struct b (including a == b). Strings are immutable and ignored.
  const unsigned char *reach;
  size_t structs;
  size_t count;
  int probe, active, changed;
  Loop *loop;
  Header *headers;
} Permissions;
static State copy(Permissions *p, State state) {
  State result = arena_calloc(p->arena, p->count, 1);
  if (state && p->count)
    memcpy(result, state, p->count);
  return result;
}
static void merge(Permissions *p, State into, State from) {
  for (size_t i = 0; i < p->count; i++)
    into[i] |= from[i];
}
static void replace(Permissions *p, State into, State from) {
  if (p->count)
    memcpy(into, from, p->count);
}
static int structure(const SemaExpr *e) {
  return e->type && e->type->kind == SEMA_STRUCT;
}
static void error(Permissions *p, Span span, const char *message) {
  if (!p->probe)
    diagnostics_error(p->errors, span, message);
}
static int visit(Permissions *p, SemaExpr *e, State state);
// A call result can only hold objects from a read-only argument when some
// struct type is reachable from both: identity calls and getters returning a
// field are caught, while a constructor building a User from a Json is not.
static int may_share(const Permissions *p, const SemaType *a,
                     const SemaType *b) {
  if (!a || !b || a->kind != SEMA_STRUCT || b->kind != SEMA_STRUCT)
    return 0;
  const unsigned char *x = p->reach + a->structure->id * p->structs;
  const unsigned char *y = p->reach + b->structure->id * p->structs;
  for (size_t i = 0; i < p->structs; i++)
    if (x[i] && y[i])
      return 1;
  return 0;
}
static const unsigned char *reachability(SemaProgram *program) {
  size_t n = program->struct_count;
  unsigned char *reach = arena_calloc(program->arena, n * n, 1);
  size_t *work = arena_calloc(program->arena, n, sizeof(*work));
  for (size_t from = 0; from < n; from++) {
    unsigned char *row = reach + from * n;
    size_t top = 0;
    row[from] = 1;
    work[top++] = from;
    while (top) {
      const SemaStruct *s = &program->structs[work[--top]];
      for (size_t f = 0; f < s->count; f++) {
        const SemaType *type = s->fields[f].type;
        if (type && type->kind == SEMA_STRUCT && !row[type->structure->id]) {
          row[type->structure->id] = 1;
          work[top++] = type->structure->id;
        }
      }
    }
  }
  return reach;
}
static Loop loop(Permissions *p) {
  return (Loop){
      .breaks = copy(p, NULL), .continues = copy(p, NULL), .outer = p->loop};
}
static void grow(Permissions *p, State header, State state) {
  for (size_t i = 0; i < p->count; i++)
    if (state[i] && !header[i]) {
      header[i] = 1;
      p->changed = 1;
    }
}
static void repeat(Permissions *p, SemaExpr *condition, SemaExpr *body,
                   State state) {
  Header *header = p->headers;
  while (header && header->body != body)
    header = header->next;
  if (!header) {
    header = arena_alloc(p->arena, sizeof(*header));
    *header = (Header){body, copy(p, NULL), p->headers};
    p->headers = header;
  }
  // Header bits only grow. Iterate the whole function, rather than recursively
  // iterating nested loops: even deeply nested loops take one visit per pass.
  grow(p, header->state, state);
  State exit = copy(p, header->state);
  int active = p->active;
  if (condition) {
    visit(p, condition, exit);
    p->active = active && (condition->flow & SEMA_NEXT);
  }
  Loop l = loop(p);
  p->loop = &l;
  State body_state = copy(p, exit);
  visit(p, body, body_state);
  p->loop = l.outer;
  if (p->active && (body->flow & SEMA_NEXT))
    grow(p, header->state, body_state);
  if (l.has_continue)
    grow(p, header->state, l.continues);
  p->active = active;
  int has_exit = !condition ||
                 ((condition->flow & SEMA_NEXT) &&
                  !(condition->kind == SEMA_EXPR_BOOL && condition->integer));
  if (!has_exit && l.has_break) {
    // Only breaks reach the following statement in a while true loop.
    exit = copy(p, NULL);
  }
  // With no exit at all, retain the condition/header state for checking dead
  // syntax; clearing it would incorrectly grant writable access to parameters.
  if (l.has_break)
    merge(p, exit, l.breaks);
  replace(p, state, exit);
}
static int visit(Permissions *p, SemaExpr *e, State state) {
  int readonly = 0, active = p->active;
  switch (e->kind) {
  case SEMA_EXPR_VAR:
    if (e->as.variable)
      readonly = state[e->as.variable->id];
    break;
  case SEMA_EXPR_FIELD:
    readonly = visit(p, e->as.field.object, state);
    break;
  case SEMA_EXPR_BIND: {
    int value = visit(p, e->as.binding.value, state);
    state[e->as.binding.variable->id] = (unsigned char)value;
    break;
  }
  case SEMA_EXPR_ASSIGN:
  case SEMA_EXPR_UPDATE: {
    SemaExpr *target = e->as.binding.target;
    int parent = 0;
    if (target) {
      parent = visit(p, target->as.field.object, state);
      if (parent && target->as.field.field && target->type &&
          target->type->kind >= SEMA_UNIT)
        error(p, target->span,
              "Cannot mutate through an ordinary parameter; use &.");
      p->active = active && (target->flow & SEMA_NEXT);
    }
    if (e->as.binding.value) {
      int value = visit(p, e->as.binding.value, state);
      if (!target && e->as.binding.variable)
        state[e->as.binding.variable->id] = (unsigned char)value;
      // A stored reference outlives this access path: whoever can write the
      // holder could later write through it. Keeping an argument therefore
      // needs an & parameter. A read-only holder was already reported above.
      if (target && target->as.field.field && value && !parent)
        error(p, e->as.binding.value->span,
              "A read-only struct cannot be stored in a field; take the "
              "parameter it comes from as &.");
    }
    break;
  }
  case SEMA_EXPR_CALL:
    for (size_t i = 0; i < e->as.call.count; i++) {
      SemaExpr *arg = e->as.call.args[i];
      int value =
          visit(p, arg, state); // Capture before later arguments rebind names.
      readonly |= value && may_share(p, e->type, arg->type);
      SemaSymbol *s = e->as.call.target;
      if (value && s && i < s->arity && s->params[i]->data_mutable &&
          arg->type == s->params[i]->type)
        error(p, arg->span,
              "A mutable argument cannot be borrowed through an ordinary "
              "parameter.");
      p->active = p->active && (arg->flow & SEMA_NEXT);
    }
    break; // Ownership does not upgrade access; primitive results stay values.
  case SEMA_EXPR_STRUCT:
    for (size_t i = 0; i < e->as.structure.count; i++) {
      SemaExpr *value = e->as.structure.values[i];
      readonly |= visit(p, value, state);
      p->active = p->active && (value->flow & SEMA_NEXT);
    }
    break; // Conservatively restrict the whole object, not individual fields.
  case SEMA_EXPR_RETURN:
    if (e->as.value)
      visit(p, e->as.value, state);
    break;
  case SEMA_EXPR_BLOCK:
    for (size_t i = 0; i < e->as.block.count; i++) {
      SemaExpr *item = e->as.block.items[i];
      int value = visit(p, item, state);
      if (p->active)
        readonly = value;
      p->active = p->active && (item->flow & SEMA_NEXT);
    }
    break;
  case SEMA_EXPR_MATCH: {
    SemaExpr *input = e->as.match.input;
    int value = visit(p, input, state), has_next = 0;
    State joined = copy(p, NULL);
    for (size_t i = 0; i < e->as.match.count; i++) {
      SemaArm *a = &e->as.match.arms[i];
      State arm_state = copy(p, state);
      if (a->binding)
        arm_state[a->binding->id] = (unsigned char)value;
      p->active = active && (input->flow & SEMA_NEXT) && a->useful;
      int result = visit(p, a->body, arm_state);
      if (a->useful && (a->body->flow & SEMA_NEXT)) {
        merge(p, joined, arm_state);
        readonly |= result;
        has_next = 1;
      }
    }
    if (has_next)
      replace(p, state, joined);
    break;
  }
  case SEMA_EXPR_IF: {
    SemaExpr *condition = e->as.conditional.condition;
    SemaExpr *yes = e->as.conditional.yes, *no = e->as.conditional.no;
    visit(p, condition, state);
    State yes_state = copy(p, state), no_state = copy(p, state);
    p->active = active && (condition->flow & SEMA_NEXT);
    int yr = visit(p, yes, yes_state);
    p->active = active && (condition->flow & SEMA_NEXT);
    int nr = no ? visit(p, no, no_state) : 0;
    int yf = (yes->flow & SEMA_NEXT) != 0, nf = !no || (no->flow & SEMA_NEXT);
    if (yf && nf) {
      replace(p, state, yes_state);
      merge(p, state, no_state);
    } else if (yf)
      replace(p, state, yes_state);
    else if (nf)
      replace(p, state, no_state);
    readonly = (yf && yr) || (nf && nr);
    break;
  }
  case SEMA_EXPR_BINARY: {
    SemaExpr *a = e->as.binary.left, *b = e->as.binary.right;
    visit(p, a, state);
    p->active = active && (a->flow & SEMA_NEXT);
    State skipped = NULL;
    if (e->as.binary.op == BINARY_LOGICAL_AND ||
        e->as.binary.op == BINARY_LOGICAL_OR)
      skipped = copy(p, state);
    visit(p, b, state);
    if (skipped) {
      if (b->flow & SEMA_NEXT)
        merge(p, state, skipped);
      else
        replace(p, state, skipped);
    }
    break;
  }
  case SEMA_EXPR_UNARY:
    visit(p, e->as.unary.value, state);
    break;
  case SEMA_EXPR_WHILE:
    repeat(p, e->as.while_loop.condition, e->as.while_loop.body, state);
    break;
  case SEMA_EXPR_FOR:
    visit(p, e->as.for_loop.lower, state);
    p->active = active && (e->as.for_loop.lower->flow & SEMA_NEXT);
    visit(p, e->as.for_loop.upper, state);
    p->active = p->active && (e->as.for_loop.upper->flow & SEMA_NEXT);
    repeat(p, NULL, e->as.for_loop.body, state);
    break;
  case SEMA_EXPR_BREAK:
  case SEMA_EXPR_CONTINUE:
    if (active && p->loop) {
      if (e->kind == SEMA_EXPR_BREAK) {
        merge(p, p->loop->breaks, state);
        p->loop->has_break = 1;
      } else {
        merge(p, p->loop->continues, state);
        p->loop->has_continue = 1;
      }
    }
    break;
  default:
    break;
  }
  p->active = active;
  return structure(e) && readonly;
}
void sema_check_permissions(SemaProgram *program, Diagnostics *errors) {
  const unsigned char *reach = reachability(program);
  for (size_t i = 0; i < program->count; i++) {
    SemaFunction *f = &program->functions[i];
    Permissions p = {.arena = program->arena,
                     .errors = errors,
                     .reach = reach,
                     .structs = program->struct_count,
                     .count = f->variable_count,
                     .active = 1};
    State entry = copy(&p, NULL);
    int has_readonly = 0;
    for (size_t j = 0; j < f->symbol.arity; j++) {
      SemaVar *v = f->symbol.params[j];
      entry[v->id] = v->type->kind == SEMA_STRUCT && !v->data_mutable;
      has_readonly |= entry[v->id];
    }
    if (!has_readonly)
      continue;
    p.probe = 1;
    do {
      p.changed = 0;
      visit(&p, f->body, copy(&p, entry));
    } while (p.changed);
    // Emit diagnostics only at the fixed point, once per source expression.
    p.probe = 0;
    visit(&p, f->body, copy(&p, entry));
  }
}
