#include "sema/flow.h"
#include <string.h>

// Access permission is separate from ownership: retaining a reference does not
// upgrade it. State bits mean "may reach read-only objects". Direct
// copies propagate the bit, as do struct/symbol literals with read-only
// children and call results whose type can reach a read-only argument's
// objects. A read-only value cannot be stored in a field. These are local
// access rules, not deep const or a function purity guarantee.
typedef struct {
  Diagnostics *errors;
  // Rows are structs followed by symbol sets. reach[a * structs + b] means
  // type a can reach struct b through fields/payloads (including identity).
  // Strings are immutable and ignored.
  const unsigned char *reach;
  size_t structs, count;
} Permissions;

static const unsigned char *row(const Permissions *p, const SemaType *type) {
  if (!type || (type->kind != SEMA_STRUCT && type->kind != SEMA_SYM))
    return NULL;
  size_t id = type->kind == SEMA_STRUCT ? type->structure->id
                                        : p->structs + type->symbols->id;
  return p->reach + id * p->structs;
}
static int structure(const Permissions *p, const SemaType *type) {
  const unsigned char *r = row(p, type);
  for (size_t i = 0; r && i < p->structs; i++)
    if (r[i])
      return 1;
  return 0;
}
static void error(SemaFlow *flow, Span span, const char *message) {
  Permissions *p = flow->context;
  if (!flow->probe)
    diagnostics_error(p->errors, span, message);
}
// A call result can only hold objects from a read-only argument when some
// struct type is reachable from both: identity calls and getters returning a
// field are caught, while a constructor building a User from a Json is not.
static int may_share(const Permissions *p, const SemaType *a,
                     const SemaType *b) {
  const unsigned char *x = row(p, a), *y = row(p, b);
  if (!x || !y)
    return 0;
  for (size_t i = 0; i < p->structs; i++)
    if (x[i] && y[i])
      return 1;
  return 0;
}
static const unsigned char *reachability(SemaProgram *program) {
  size_t n = program->struct_count;
  size_t total = n + program->symbol_set_count;
  unsigned char *reach = arena_calloc(program->arena, total, n);
  size_t *work = arena_calloc(program->arena, total, sizeof(*work));
  unsigned char *seen = arena_calloc(program->arena, total, 1);
  const SemaType **types = arena_calloc(program->arena, total, sizeof(*types));
  for (size_t i = 0; i < n; i++)
    types[i] = &program->structs[i].type;
  for (SemaSymSet *s = program->symbol_sets; s; s = s->next)
    types[n + s->id] = &s->type;
  for (size_t from = 0; from < total; from++) {
    unsigned char *r = reach + from * n;
    memset(seen, 0, total);
    size_t top = 0;
    seen[from] = 1;
    work[top++] = from;
    while (top) {
      const SemaType *current = types[work[--top]];
      size_t count = current->kind == SEMA_STRUCT ? current->structure->count
                                                  : current->symbols->count;
      if (current->kind == SEMA_STRUCT)
        r[current->structure->id] = 1;
      for (size_t f = 0; f < count; f++) {
        size_t positions = current->kind == SEMA_STRUCT
                               ? 1
                               : current->symbols->variants[f].payload_count;
        for (size_t position = 0; position < positions; position++) {
          const SemaType *type =
              current->kind == SEMA_STRUCT
                  ? current->structure->fields[f].type
                  : current->symbols->variants[f].payloads[position];
          if (!type || (type->kind != SEMA_STRUCT && type->kind != SEMA_SYM))
            continue;
          size_t id = type->kind == SEMA_STRUCT ? type->structure->id
                                                : n + type->symbols->id;
          if (!seen[id]) {
            seen[id] = 1;
            work[top++] = id;
          }
        }
      }
    }
  }
  return reach;
}
static int merge(SemaFlow *flow, void *into, const void *from, SemaExpr *at) {
  Permissions *p = flow->context;
  unsigned char *a = into;
  const unsigned char *b = from;
  int changed = 0;
  (void)at;
  for (size_t i = 0; i < p->count; i++) {
    unsigned char bit = a[i] | b[i];
    changed |= bit != a[i];
    a[i] = bit;
  }
  return changed;
}
static SemaFlowValue enter(SemaFlow *flow, SemaExpr *e, void *state) {
  (void)flow;
  unsigned char *bits = state;
  return e->kind == SEMA_EXPR_VAR && e->as.variable ? bits[e->as.variable->id]
                                                    : 0;
}
static SemaFlowValue child(SemaFlow *flow, SemaExpr *e, size_t index,
                           void *state, SemaFlowValue result,
                           SemaFlowValue value) {
  Permissions *p = flow->context;
  unsigned char *bits = state;
  switch (e->kind) {
  case SEMA_EXPR_FIELD:
  case SEMA_EXPR_TRY:
  case SEMA_EXPR_PANIC:
    return value;
  case SEMA_EXPR_BIND:
    bits[e->as.binding.variable->id] = (unsigned char)value;
    break;
  case SEMA_EXPR_ASSIGN:
  case SEMA_EXPR_UPDATE: {
    SemaExpr *target = e->as.binding.target;
    if (index == 0) {
      if (value && target->as.field.field && target->type &&
          target->type->kind >= SEMA_UNIT)
        error(flow, target->span,
              "Cannot mutate through an ordinary parameter; use &.");
      return value; // Capture the target's permission before RHS effects.
    }
    if (!target && e->as.binding.variable)
      bits[e->as.binding.variable->id] = (unsigned char)value;
    // Keeping a read-only reference would upgrade access through its holder.
    // A read-only target was already reported above.
    if (target && target->as.field.field && value && !result)
      error(flow, e->as.binding.value->span,
            "A read-only value cannot be stored in a field; take the "
            "parameter it comes from as &.");
    break;
  }
  case SEMA_EXPR_CALL: {
    SemaExpr *arg = e->as.call.args[index];
    SemaSymbol *s = e->as.call.wrong_form ? NULL : e->as.call.target;
    if (value && s && index < s->arity && s->params[index]->data_mutable &&
        sema_assignable(arg->type, s->params[index]->type))
      error(flow, arg->span,
            "A read-only value cannot be passed to an & parameter; take the "
            "parameter it comes from as &.");
    return result | (value && may_share(p, e->type, arg->type));
  }
  case SEMA_EXPR_STRUCT:
  case SEMA_EXPR_SYMBOL:
    return result | value;
  case SEMA_EXPR_BLOCK:
    return flow->active ? value : result;
  case SEMA_EXPR_IF:
    if (index &&
        ((index == 1 ? e->as.conditional.yes : e->as.conditional.no)->flow &
         SEMA_NEXT))
      return result | value;
    return result;
  case SEMA_EXPR_MATCH:
    if (index) {
      SemaArm *arm = &e->as.match.arms[index - 1];
      if (arm->useful && (arm->body->flow & SEMA_NEXT))
        return result | value;
    }
    return result;
  default:
    break;
  }
  return 0;
}
static SemaFlowValue leave(SemaFlow *flow, SemaExpr *e, void *state,
                           SemaFlowValue result) {
  Permissions *p = flow->context;
  (void)state;
  return structure(p, e->type) && result;
}
static void arm(SemaFlow *flow, SemaArm *a, void *state, SemaFlowValue input) {
  (void)flow;
  unsigned char *bits = state;
  if (a->binding)
    bits[a->binding->id] = (unsigned char)input;
  for (size_t i = 0; a->payload_bindings && i < a->payload_count; i++)
    if (a->payload_bindings[i])
      bits[a->payload_bindings[i]->id] = (unsigned char)input;
}
void sema_check_permissions(SemaProgram *program, Diagnostics *errors) {
  if (!program->struct_count)
    return; // Symbols containing only scalar/immutable data carry no access.
  const unsigned char *reach = reachability(program);
  for (size_t i = 0; i < program->count; i++) {
    SemaFunction *f = &program->functions[i];
    Permissions p = {.errors = errors,
                     .reach = reach,
                     .structs = program->struct_count,
                     .count = f->variable_count};
    unsigned char *entry = arena_calloc(program->arena, p.count, 1);
    int has_readonly = 0;
    for (size_t j = 0; j < f->symbol.arity; j++) {
      SemaVar *v = f->symbol.params[j];
      entry[v->id] = structure(&p, v->type) && !v->data_mutable;
      has_readonly |= entry[v->id];
    }
    if (!has_readonly)
      continue;
    SemaFlowAnalysis analysis = {.state_size = p.count,
                                 .join = merge,
                                 .enter = enter,
                                 .child = child,
                                 .leave = leave,
                                 .arm = arm};
    sema_flow_run(program->arena, &analysis, &p, f->body, entry);
  }
}
