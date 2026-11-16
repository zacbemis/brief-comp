#include "sema/check/internal.h"

// Let-else is an ordinary checked match after its input type stabilizes.
// Keep original variants for tag tests even when a singleton :ok normalizes.
static void let_else(Checker *c, SemaExpr *e, const SemaType *input) {
  SemaArm *yes = &e->as.match.arms[0], *no = &e->as.match.arms[1];
  SemaVar *value = e->as.match.remaining;
  yes->kind = PAT_SET;
  yes->binding = value;
  yes->tag = NULL;
  yes->payload_count = 0;
  yes->payload_bindings[0] = NULL;
  yes->any = 0;
  yes->useful = 1;
  yes->cases = yes->set = NULL;
  yes->case_count = no->case_count = 0;
  yes->case_variants = no->case_variants = NULL;
  no->any = 1;
  no->useful = 0;
  no->cases = NULL;
  value->type = special(input) ? input : t(c, SEMA_ERROR);
  if (special(input))
    return;
  const SemaVariant *none = sema_none(input);
  if (!none && !sema_has_errors(input)) {
    error(c, e->as.match.input->span,
          "A let-else value must contain :none or an error tag.");
    value->type = t(c, c->probe ? SEMA_UNKNOWN : SEMA_ERROR);
    return;
  }
  size_t count = input->symbols->count;
  yes->case_variants = arena_calloc(c->p->arena, count, sizeof(SemaVariant));
  no->case_variants = arena_calloc(c->p->arena, count, sizeof(SemaVariant));
  for (size_t i = 0; i < count; i++) {
    const SemaVariant *v = &input->symbols->variants[i];
    SemaArm *arm = v == none || v->tag->kind == SEMA_TAG_ERROR ? no : yes;
    arm->case_variants[arm->case_count++] = *v;
  }
  no->useful = 1;
  no->cases = sema_symbol_set(c->p, no->case_variants, no->case_count);
  yes->useful = yes->case_count != 0;
  if (!yes->useful) {
    value->type = t(c, SEMA_NEVER);
    return;
  }
  yes->cases = sema_symbol_set(c->p, yes->case_variants, yes->case_count);
  if (yes->case_count == 1) {
    const SemaVariant *v = yes->case_variants;
    if (v->payload_count > 1) {
      error(c, e->as.match.input->span,
            "Extracting several success payloads is not implemented yet.");
      value->type = t(c, c->probe ? SEMA_UNKNOWN : SEMA_ERROR);
      return;
    }
    yes->kind = PAT_SYMBOL;
    yes->tag = v->tag;
    yes->binding = NULL;
    yes->payload_count = v->payload_count;
    if (v->payload_count)
      yes->payload_bindings[0] = value;
    value->type = v->payload_count ? v->payloads[0] : t(c, SEMA_UNIT);
  } else {
    yes->set = yes->cases;
    value->type = yes->cases;
  }
}

const SemaType *check_try(Checker *c, SemaExpr *e, unsigned *flow) {
  SemaExpr *value = e->as.value;
  const SemaType *input = check_expr(c, value, NULL, OUTCOME);
  *flow = value->flow;
  int errors = sema_has_errors(input);
  const SemaVariant *success = errors ? sema_single_success(input) : NULL;
  int terminal = sema_only_errors(input);
  if (!errors) {
    if (!special(input))
      error(c, e->span,
            "? and ! cannot handle this value: it has no error tags.");
    // A provisional SCC symbol set can acquire error tags on a later probe.
    // Do not permanently poison callers while that set is still growing.
    return c->probe && input->kind == SEMA_SYM ? t(c, SEMA_UNKNOWN)
           : special(input)                    ? input
                                               : t(c, SEMA_ERROR);
  }
  if (success && success->payload_count > 1) {
    error(c, e->span,
          success->tag == c->p->ok
              ? "Multiple ':ok' payloads are not implemented yet."
              : "Extracting several success payloads is not implemented yet.");
    return t(c, SEMA_ERROR);
  }
  const SemaType *payload = sema_single_payload(success);
  const SemaType *result = terminal ? t(c, SEMA_NEVER)
                           : !success
                               ? sema_kind_part(c->p, input, SEMA_TAG_NEUTRAL)
                           : payload ? payload
                                     : t(c, SEMA_UNIT);
  e->failures = sema_kind_part(c->p, input, SEMA_TAG_ERROR);
  if (e->kind == SEMA_EXPR_TRY && e->failures) {
    *flow = sequence(*flow, terminal ? SEMA_RETURN : SEMA_NEXT | SEMA_RETURN);
    if (c->function->written && !check_return_fits(c, e->failures))
      error(c, e->span,
            "Propagated variants are not in the written return type.");
    if (e->reachable && (value->flow & SEMA_NEXT)) {
      // An always-propagating expression is a real return, not a
      // valueless ending that can invent an implicit :ok alternative.
      if (terminal)
        c->saw_return = 1;
      c->failures =
          c->failures ? check_join(c, c->failures, e->failures, e->span,
                                   "Symbol tag has conflicting payload types.")
                      : e->failures;
    }
  } else if (terminal)
    *flow = sequence(*flow, 0);
  return result;
}

// Types scalar and symbol arm patterns against the match input.
static void arm_patterns(Checker *c, SemaExpr *e, const SemaType *it) {
  int scalar = sema_number(it) || it->kind == SEMA_BOOL || it->kind == SEMA_CHR;
  int string_literal = it->kind == SEMA_STR;
  for (size_t i = 0; i < e->as.match.count; i++) {
    SemaArm *a = &e->as.match.arms[i];
    if (a->binding)
      a->binding->type = it;
    for (size_t j = 0; a->payload_bindings && j < a->payload_count; j++)
      if (a->payload_bindings[j])
        a->payload_bindings[j]->type = it;
    if (it->kind == SEMA_SYM || a->invalid)
      continue;
    if (!special(it) && (a->kind == PAT_SYMBOL || a->kind == PAT_SET))
      error(c, a->span, "Symbol patterns require a symbol input.");
    if (a->lower) {
      check_expr(c, a->lower, it, VALUE);
      if (scalar || (string_literal && a->kind == PAT_LITERAL))
        expect(c, a->lower, it, "Pattern value has the wrong type.");
    }
    if (a->upper) {
      check_expr(c, a->upper, it, VALUE);
      if (scalar)
        expect(c, a->upper, it, "Pattern value has the wrong type.");
    }
    if (!special(it) && !scalar && !string_literal && !a->invalid && a->lower)
      error(c, a->span,
            "Literal, range, and comparison patterns require a scalar type.");
    if ((it->kind == SEMA_BOOL || string_literal) &&
        (a->kind == PAT_RANGE || a->kind == PAT_COMPARE)) {
      error(c, a->span,
            "Range and comparison patterns require numbers or chr.");
    }
  }
  sema_match_coverage(c->p, e, c->probe ? NULL : c->errors);
}

const SemaType *check_match(Checker *c, SemaExpr *e, const SemaType *expected,
                            Use use, unsigned *flow) {
  SemaExpr *input = e->as.match.input;
  const SemaType *it = check_expr(c, input, NULL, OUTCOME);
  if (e->as.match.remaining)
    let_else(c, e, it);
  else
    arm_patterns(c, e, it);
  const SemaType *context = expected;
  if (special(context))
    context = check_hint(e);
  if (special(context) && sema_number(e->type))
    context = e->type;
  const SemaType *result = t(c, SEMA_NEVER);
  unsigned arms_flow = 0;
  for (size_t i = 0; i < e->as.match.count; i++) {
    SemaArm *a = &e->as.match.arms[i];
    c->reachable = e->reachable && (input->flow & SEMA_NEXT) && a->useful;
    int fallback = e->as.match.remaining && i == 1;
    const SemaType *at = check_expr(c, a->body, fallback ? NULL : context,
                                    fallback ? DISCARD : use);
    if (fallback && !a->body->returns)
      error(c, a->body->span,
            "The else block of a let binding must not fall through.");
    if (a->useful) {
      arms_flow |= a->body->flow;
      if (use != DISCARD && !fallback)
        result = use == TAIL ? check_return_join(c, result, at, a->span,
                                                 "Value-producing match arms "
                                                 "must have compatible types.")
                             : check_join(c, result, at, a->span,
                                          "Value-producing match arms must "
                                          "have compatible types.");
    }
  }
  *flow = sequence(input->flow, arms_flow);
  return use == DISCARD ? t(c, SEMA_UNIT) : result;
}

const SemaType *check_if(Checker *c, SemaExpr *e, const SemaType *expected,
                         Use use, unsigned *flow) {
  SemaExpr *condition = e->as.conditional.condition,
           *yes = e->as.conditional.yes, *no = e->as.conditional.no;
  check_expr(c, condition, t(c, SEMA_BOOL), VALUE);
  expect(c, condition, t(c, SEMA_BOOL), "An if condition must be bool.");
  const SemaType *context = expected;
  if (special(context)) {
    context = check_hint(yes);
    if (special(context))
      context = check_hint(no);
    if (special(context) && sema_number(e->type))
      context = e->type;
  }
  c->reachable = e->reachable && (condition->flow & SEMA_NEXT);
  const SemaType *yt =
      check_expr(c, yes, no ? context : NULL, no ? use : DISCARD);
  c->reachable = e->reachable && (condition->flow & SEMA_NEXT);
  const SemaType *nt =
      no ? check_expr(c, no, special(context) ? yt : context, use)
         : t(c, SEMA_UNIT);
  *flow = sequence(condition->flow, yes->flow | (no ? no->flow : SEMA_NEXT));
  if (!no && (use == VALUE || use == OUTCOME || use == BOUND))
    error(c, e->span, "An if without else cannot be used as a value.");
  if (!no || use == DISCARD)
    return t(c, SEMA_UNIT);
  return use == TAIL
             ? check_return_join(
                   c, yt, nt, e->span,
                   "Value-producing if branches must have compatible types.")
             : check_join(
                   c, yt, nt, e->span,
                   "Value-producing if branches must have compatible types.");
}

const SemaType *check_while(Checker *c, SemaExpr *e, Use use, unsigned *flow) {
  SemaExpr *condition = e->as.while_loop.condition,
           *body = e->as.while_loop.body;
  check_expr(c, condition, t(c, SEMA_BOOL), VALUE);
  expect(c, condition, t(c, SEMA_BOOL), "A while condition must be bool.");
  c->reachable = e->reachable && (condition->flow & SEMA_NEXT);
  check_expr(c, body, NULL, DISCARD);
  unsigned exit = SEMA_NEXT | (body->flow & SEMA_RETURN);
  if (condition->kind == SEMA_EXPR_BOOL && condition->integer &&
      !(body->flow & SEMA_BREAK))
    exit &= ~SEMA_NEXT;
  *flow = sequence(condition->flow, exit);
  if (use == VALUE || use == OUTCOME || use == BOUND)
    error(c, e->span, "Loops cannot be used as values.");
  return t(c, SEMA_UNIT);
}

const SemaType *check_for(Checker *c, SemaExpr *e, Use use, unsigned *flow) {
  SemaExpr *a = e->as.for_loop.lower, *b = e->as.for_loop.upper,
           *body = e->as.for_loop.body;
  SemaVar *v = e->as.for_loop.variable;
  const SemaType *annotation =
      e->as.for_loop.annotation_span.file ? v->annotation : NULL;
  int typed = sema_integer(annotation);
  if (annotation && !special(annotation) && !typed)
    error(c, e->as.for_loop.annotation_span,
          "A loop variable must have an integer type.");
  const SemaType *context = annotation ? (typed ? annotation : t(c, SEMA_ERROR))
                                       : check_operand_type(c, a, b, NULL);
  const SemaType *at = check_expr(c, a, context, VALUE);
  c->reachable = e->reachable && (a->flow & SEMA_NEXT);
  // A mismatching lower bound must not change the upper bound's context.
  const SemaType *bt = check_expr(
      c, b, annotation ? context : (special(at) ? context : at), VALUE);
  if (annotation) {
    if (typed) {
      expect(c, a, annotation,
             "Range bound does not match the loop variable type.");
      expect(c, b, annotation,
             "Range bound does not match the loop variable type.");
    }
    v->type = typed ? annotation : t(c, SEMA_ERROR);
  } else {
    if (!special(at) && !sema_integer(at))
      error(c, a->span, "Range bounds must be integers.");
    if (!special(bt) && !sema_integer(bt))
      error(c, b->span, "Range bounds must be integers.");
    if (!special(at) && !special(bt) && at != bt)
      error(c, e->span, "Range bounds must have the same type.");
    v->type = special(at) ? bt : at;
  }
  c->reachable = e->reachable && (a->flow & b->flow & SEMA_NEXT);
  check_expr(c, body, NULL, DISCARD);
  *flow = sequence(sequence(a->flow, b->flow),
                   SEMA_NEXT | (body->flow & SEMA_RETURN));
  if (use == VALUE || use == OUTCOME || use == BOUND)
    error(c, e->span, "Loops cannot be used as values.");
  return t(c, SEMA_UNIT);
}
