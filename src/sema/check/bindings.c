#include "sema/check/internal.h"

static void mutable(Checker *c, SemaVar *v, Span span) {
  // Deferred plain stores bypass this check below. Compound/update writes
  // must still diagnose before assignment recovery suppresses outcome errors.
  if (v && !v->mutable && (v->deferred || !v->outcome_binding || !c->reachable))
    error(c, span, "Cannot reassign an immutable binding.");
}
static const SemaType *lvalue(Checker *c, SemaExpr *e) {
  if (e->as.binding.target) {
    SemaExpr *target = e->as.binding.target;
    return check_expr(c, target, NULL, VALUE);
  }
  SemaVar *v = e->as.binding.variable;
  if (!v || !v->deferred || e->kind != SEMA_EXPR_ASSIGN ||
      e->as.binding.op != ASSIGN_SET)
    mutable(c, v, e->span);
  // Flow decides whether the old outcome was handled. A potential var rearm
  // still needs ordinary assignment typing; it cannot change the slot's type.
  int rearm = v && (v->mutable || v->deferred) && e->kind == SEMA_EXPR_ASSIGN &&
              e->as.binding.op == ASSIGN_SET;
  return v && (!v->outcome_binding || !c->reachable || rearm)
             ? v->type
             : t(c, SEMA_ERROR);
}

const SemaType *check_bind(Checker *c, SemaExpr *e, unsigned *flow) {
  SemaVar *v = e->as.binding.variable;
  SemaExpr *value = e->as.binding.value;
  if (!value) {
    v->type = v->annotation;
    v->outcome_binding = sema_has_errors(v->type);
    return t(c, SEMA_UNIT);
  }
  v->type = check_expr(
      c, value, e->as.binding.stage ? check_pipeline_hint(c, e) : v->annotation,
      e->as.binding.declaration ? BOUND
      : e->as.binding.outcome   ? OUTCOME
                                : VALUE);
  expect(c, value, v->annotation,
         "Binding value does not match its annotation.");
  if (!unknown(v->annotation))
    v->type = v->annotation;
  v->outcome_binding = e->as.binding.declaration && sema_has_errors(v->type) &&
                       !special(value->type) &&
                       sema_assignable(value->type, v->type);
  *flow = value->flow;
  return t(c, SEMA_UNIT);
}

const SemaType *check_assign(Checker *c, SemaExpr *e, unsigned *flow) {
  int reachable = e->reachable;
  const SemaType *target = lvalue(c, e);
  if (e->as.binding.target)
    *flow = e->as.binding.target->flow;
  c->reachable = reachable && (*flow & SEMA_NEXT);
  SemaExpr *value = e->as.binding.value;
  check_expr(c, value, target,
             e->as.binding.variable &&
                     e->as.binding.variable->outcome_binding && reachable
                 ? BOUND
                 : VALUE);
  *flow = sequence(*flow, value->flow);
  expect(c, value, target, "Assignment value has the wrong type.");
  if (e->as.binding.op != ASSIGN_SET && !special(target) &&
      !sema_number(target) &&
      !(target->kind == SEMA_STR && e->as.binding.op == ASSIGN_ADD))
    error(c, e->span, "Compound assignment requires numbers (or str for +=).");
  if (e->as.binding.op == ASSIGN_REM && sema_number(target) &&
      !sema_integer(target))
    error(c, e->span, "Remainder requires integers.");
  return t(c, SEMA_UNIT);
}

const SemaType *check_update(Checker *c, SemaExpr *e, unsigned *flow) {
  const SemaType *target = lvalue(c, e);
  if (e->as.binding.target)
    *flow = e->as.binding.target->flow;
  if (!special(target) && !sema_number(target))
    error(c, e->span, "Increment and decrement require a numeric binding.");
  return t(c, SEMA_UNIT);
}
