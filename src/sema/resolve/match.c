#include "sema/resolve/internal.h"

#include <string.h>

// Annotation matching needs merged-symbol type-directed coverage. Find the
// first unsupported binder without resolving its types or inventing locals.
static const Pattern *typed_payload(const Pattern *p) {
  if (p->kind == PAT_TYPED_BIND)
    return p;
  if (p->kind == PAT_SYMBOL)
    for (size_t i = 0; i < p->as.symbol.count; i++) {
      const Pattern *typed = typed_payload(p->as.symbol.payloads[i]);
      if (typed)
        return typed;
    }
  return NULL;
}
static const Pattern *refutable_positions(const Pattern *p) {
  if (p->kind == PAT_SYMBOL && p->as.symbol.count > 1)
    for (size_t i = 0; i < p->as.symbol.count; i++) {
      const Pattern *item = p->as.symbol.payloads[i];
      if (item->kind != PAT_BIND && item->kind != PAT_WILDCARD)
        return item;
    }
  return NULL;
}
static const Pattern *qualified_payload(const Pattern *p) {
  if (p->kind == PAT_SYMBOL) {
    if (p->as.symbol.module)
      return p;
    for (size_t i = 0; i < p->as.symbol.count; i++) {
      const Pattern *qualified = qualified_payload(p->as.symbol.payloads[i]);
      if (qualified)
        return qualified;
    }
  }
  return NULL;
}

static void symbol_pattern(Resolver *r, SemaArm *a, const Pattern *p) {
  a->tag =
      resolve_tag(r, p->as.symbol.tag, p->as.symbol.kind,
                  p->as.symbol.tag_span.file ? p->as.symbol.tag_span : p->span);
  a->payload_count = p->as.symbol.count;
  a->payload_bindings =
      arena_calloc(r->arena, a->payload_count, sizeof(*a->payload_bindings));
  if (!a->tag)
    a->invalid = 1;
  for (size_t j = 0; j < a->payload_count; j++) {
    const Pattern *payload = p->as.symbol.payloads[j];
    if (payload->kind == PAT_BIND)
      a->payload_bindings[j] =
          resolve_variable(r, payload->as.name, payload->span,
                           &r->program->types[SEMA_UNKNOWN], 0);
    else if (payload->kind != PAT_WILDCARD) {
      a->invalid = 1;
      diagnostics_error(
          r->errors, payload->span,
          "Refutable symbol payload patterns are not implemented yet.");
    }
  }
}

static void range_pattern(Resolver *r, SemaArm *a, const Pattern *p) {
  a->lower = resolve_expr(r, p->as.range.lower);
  a->upper = resolve_expr(r, p->as.range.upper);
  a->inclusive = p->as.range.inclusive;
  // Coverage ranks are compile-time literal values, not runtime
  // variable slots. Do not silently normalize a named bound to zero.
  SemaExpr *bound =
      a->lower->kind == SEMA_EXPR_VAR && a->lower->as.variable   ? a->lower
      : a->upper->kind == SEMA_EXPR_VAR && a->upper->as.variable ? a->upper
                                                                 : NULL;
  if (bound) {
    a->invalid = 1;
    diagnostics_error(
        r->errors, bound->span,
        "Non-literal range pattern bounds are not implemented yet.");
  }
}

static void pattern(Resolver *r, SemaArm *a, const Pattern *p,
                    const Pattern *typed, const Pattern *qualified) {
  SemaAlias *named = p->kind == PAT_SET ? alias(r->program, p->as.name) : NULL;
  if (qualified) {
    a->invalid = 1;
    a->body = unsupported(r, qualified->span,
                          "Module-qualified symbols are not implemented yet.");
  } else if (named) {
    a->set = named->type;
    if (a->set && a->set->kind != SEMA_SYM && a->set->kind != SEMA_ERROR) {
      a->invalid = 1;
      a->body = unsupported(r, p->span,
                            "A named symbol pattern requires a symbol set.");
    }
  } else if (p->kind == PAT_BIND)
    a->binding = resolve_variable(r, p->as.name, p->span,
                                  &r->program->types[SEMA_UNKNOWN], 0);
  else if (p->kind == PAT_SYMBOL && p->as.symbol.count > 1 &&
           !strcmp(p->as.symbol.tag, "ok")) {
    a->invalid = 1;
    a->body = unsupported(r, p->span,
                          "Multiple ':ok' payloads are not implemented yet.");
  } else if (p->kind == PAT_SYMBOL &&
             resolve_reject_none_payload(
                 r, p->as.symbol.tag, p->as.symbol.kind, p->as.symbol.count,
                 p->as.symbol.tag_span.file ? p->as.symbol.tag_span
                                            : p->span)) {
    a->invalid = 1;
    a->body = node(r, SEMA_EXPR_ERROR, p->span);
  } else if (typed) {
    a->invalid = 1;
    a->body =
        unsupported(r, typed->span,
                    "Typed symbol payload patterns are not implemented yet.");
  } else if (refutable_positions(p)) {
    a->invalid = 1;
    a->body = unsupported(
        r, refutable_positions(p)->span,
        "Refutable symbol payload patterns are not implemented yet.");
  } else if (p->kind == PAT_SYMBOL)
    symbol_pattern(r, a, p);
  else if (p->kind == PAT_LITERAL)
    a->lower = resolve_expr(r, p->as.literal);
  else if (p->kind == PAT_RANGE)
    range_pattern(r, a, p);
  else if (p->kind == PAT_COMPARE) {
    a->comparison = p->as.compare.op;
    a->lower = resolve_expr(r, p->as.compare.value);
  } else if (p->kind != PAT_WILDCARD) {
    a->invalid = 1;
    diagnostics_error(r->errors, p->span, "Unknown named symbol set.");
  }
}

void resolve_arm(Resolver *r, SemaArm *a, MatchArm source) {
  const Pattern *p = source.pattern;
  const Pattern *typed = typed_payload(p);
  const Pattern *qualified = qualified_payload(p);
  a->kind = p->kind;
  a->span = p->span;
  Local *arm_scope = r->locals;
  r->depth++;
  pattern(r, a, p, typed, qualified);
  if (!a->body)
    a->body = resolve_expr(r, source.value);
  resolve_restore(r, arm_scope);
  r->depth--;
}
