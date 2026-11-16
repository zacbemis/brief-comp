#include "sema/check/internal.h"

const SemaType *check_hint(SemaExpr *e) {
  if (!e)
    return NULL;
  switch (e->kind) {
  case SEMA_EXPR_VAR:
    return e->as.variable ? e->as.variable->type : NULL;
  case SEMA_EXPR_CALL:
    return e->as.call.target ? e->as.call.target->return_type : NULL;
  case SEMA_EXPR_STRUCT:
    return &e->as.structure.type->type;
  case SEMA_EXPR_TRY:
  case SEMA_EXPR_PANIC: {
    const SemaType *input = check_hint(e->as.value);
    const SemaVariant *success =
        sema_has_errors(input) ? sema_single_success(input) : sema_ok(input);
    const SemaType *payload = success && success->payload_count <= 1
                                  ? sema_single_payload(success)
                                  : NULL;
    return payload ? payload : e->type;
  }
  case SEMA_EXPR_FIELD: {
    SemaField *f = sema_field(check_hint(e->as.field.object), e->as.field.name);
    return f ? f->type : e->type;
  }
  case SEMA_EXPR_UNARY: {
    const SemaType *h = check_hint(e->as.unary.value);
    return e->as.unary.op == UNARY_NOT || special(h) ? e->type : h;
  }
  case SEMA_EXPR_BINARY:
    if (e->as.binary.op >= BINARY_EQ && e->as.binary.op <= BINARY_LOGICAL_OR)
      return e->type;
    {
      const SemaType *a = check_hint(e->as.binary.left);
      if (special(a) && e->as.binary.op != BINARY_LSHIFT &&
          e->as.binary.op != BINARY_RSHIFT)
        a = check_hint(e->as.binary.right);
      return special(a) ? e->type : a;
    }
  case SEMA_EXPR_MATCH:
    for (size_t i = 0; i < e->as.match.count; i++) {
      const SemaType *h = check_hint(e->as.match.arms[i].body);
      if (!special(h))
        return h;
    }
    return e->type;
  case SEMA_EXPR_IF: {
    const SemaType *a = check_hint(e->as.conditional.yes);
    if (special(a))
      a = check_hint(e->as.conditional.no);
    return special(a) ? e->type : a;
  }
  case SEMA_EXPR_BLOCK:
    if (!e->flow || !e->as.block.count)
      return NULL;
    for (size_t i = 0; i < e->as.block.count; i++) {
      SemaExpr *item = e->as.block.items[i];
      if (!(item->flow & SEMA_NEXT))
        return NULL;
      if (i + 1 == e->as.block.count)
        return check_hint(item);
    }
    return NULL;
  case SEMA_EXPR_INT:
  case SEMA_EXPR_FLOAT:
    return NULL; // Contextual, never an anchor.
  default:
    return e->type;
  }
}
// Only direct numeric literals adopt a converter's target. Typed expressions
// keep their source domain; decimal literals still require flt/dbl context.
const SemaType *check_conversion_context(SemaSymbol *s, SemaExpr *arg,
                                         const SemaType *fallback) {
  return s && s->builtin == SEMA_BUILTIN_CONVERT &&
                 (arg->kind == SEMA_EXPR_INT || arg->kind == SEMA_EXPR_FLOAT)
             ? s->return_type
             : fallback;
}
// Pipeline input evaluation is sequenced through a hidden binding, but its
// literals still receive the same parameter context as an ordinary call.
const SemaType *check_pipeline_hint(Checker *c, SemaExpr *binding) {
  SemaExpr *call = binding->as.binding.stage;
  if (!call)
    return NULL;
  SemaSymbol *s = call->as.call.target;
  size_t arg = 0;
  if (call->as.call.method) {
    const SemaType *receiver =
        check_hint(call->as.call.pipeline ? binding->as.binding.value
                                          : call->as.call.args[0]);
    s = sema_method(c->p, receiver, call->as.call.method);
    if (call->as.call.pipeline) {
      if (!s || s->receiver == RECEIVER_NONE)
        s = call->as.call.shadowed   ? NULL
            : call->as.call.fallback ? call->as.call.fallback
                                     : s;
    } else
      arg = 1;
  }
  const SemaType *wanted = s && arg < s->arity ? s->params[arg]->type : NULL;
  return check_conversion_context(s, binding->as.binding.value, wanted);
}
// Only reachable returns contribute anchors. Flow is established in a first
// non-defaulting pass, before using these hints to contextualize literals.
const SemaType *check_return_hint(SemaExpr *e) {
  if (!e || !e->flow || !e->reachable)
    return NULL;
  if (e->kind == SEMA_EXPR_RETURN)
    return e->as.value ? check_hint(e->as.value) : e->type;
  if (e->kind == SEMA_EXPR_BLOCK) {
    for (size_t i = 0; i < e->as.block.count; i++) {
      SemaExpr *item = e->as.block.items[i];
      const SemaType *h = check_return_hint(item);
      if (!special(h))
        return h;
      if (!(item->flow & SEMA_NEXT))
        break;
    }
  } else if (e->kind == SEMA_EXPR_IF) {
    const SemaType *h = check_return_hint(e->as.conditional.condition);
    if (!special(h))
      return h;
    h = check_return_hint(e->as.conditional.yes);
    return special(h) ? check_return_hint(e->as.conditional.no) : h;
  } else if (e->kind == SEMA_EXPR_MATCH) {
    const SemaType *h = check_return_hint(e->as.match.input);
    if (!special(h))
      return h;
    for (size_t i = 0; i < e->as.match.count; i++) {
      if (!e->as.match.arms[i].useful)
        continue;
      h = check_return_hint(e->as.match.arms[i].body);
      if (!special(h))
        return h;
    }
  } else if (e->kind == SEMA_EXPR_SYMBOL) {
    for (size_t i = 0; i < e->as.symbol.count; i++) {
      SemaExpr *payload = e->as.symbol.payloads[i];
      const SemaType *h = check_return_hint(payload);
      if (!special(h))
        return h;
      if (!(payload->flow & SEMA_NEXT))
        break;
    }
  } else if (e->kind == SEMA_EXPR_WHILE)
    return check_return_hint(e->as.while_loop.body);
  else if (e->kind == SEMA_EXPR_FOR)
    return check_return_hint(e->as.for_loop.body);
  return NULL;
}
const SemaType *check_operand_type(Checker *c, SemaExpr *a, SemaExpr *b,
                                   const SemaType *expected) {
  const SemaVariant *ok = sema_ok(expected);
  if (ok)
    expected = ok->payload_count <= 1 ? sema_single_payload(ok) : NULL;
  if (sema_number(expected))
    return expected;
  const SemaType *h = check_hint(a);
  if (!special(h))
    return h;
  h = check_hint(b);
  if (!special(h))
    return h;
  // Let the literal on either side establish the fallback; 1 + 2.0 is dbl.
  if (c->defaults) {
    if ((a && a->kind == SEMA_EXPR_FLOAT) || (b && b->kind == SEMA_EXPR_FLOAT))
      return t(c, SEMA_DUB);
    if (c->defaults >= 2)
      return t(c, SEMA_INT);
  }
  return t(c, SEMA_UNKNOWN);
}
