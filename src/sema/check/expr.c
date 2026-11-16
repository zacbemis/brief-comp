#include "sema/check/internal.h"

#include <errno.h>
#include <math.h>
#include <stdlib.h>

static unsigned char escaped(unsigned char c) {
  return c == 'n' ? '\n' : c == 't' ? '\t' : c == '0' ? 0 : c;
}
static const SemaType *literal(Checker *c, SemaExpr *e,
                               const SemaType *expected) {
  // An already-diagnosed context type gives no range to check against. The
  // literal still takes its default type so independent errors stay checked,
  // but exceeding that default would only be a misleading second diagnostic.
  int diagnosed = expected && expected->kind == SEMA_ERROR;
  const SemaVariant *ok = sema_ok(expected);
  if (ok)
    expected = ok->payload_count <= 1 ? sema_single_payload(ok) : NULL;
  const SemaType *kind = sema_number(expected) ? expected : NULL;
  if (e->kind == SEMA_EXPR_FLOAT && sema_integer(kind))
    kind = NULL;
  if (!kind) {
    if (!c->defaults || (c->defaults == 1 && e->kind == SEMA_EXPR_INT))
      return t(c, SEMA_UNKNOWN);
    kind = t(c, e->kind == SEMA_EXPR_FLOAT ? SEMA_DBL : SEMA_INT);
  }
  Token token = e->as.string;
  if (!sema_integer(kind)) {
    char *text = arena_strndup(c->p->arena, token.start, token.length);
    // Parse at the destination precision: double -> float can round twice.
    // ERANGE also reports underflow, whose finite/zero result is valid here.
    errno = 0;
    double value = kind->kind == SEMA_FLT ? (double)strtof(text, NULL)
                                          : strtod(text, NULL);
    if (errno == ERANGE && isinf(value)) {
      if (!diagnosed)
        error(c, e->span, "Number literal is outside the range of its type.");
      return t(c, SEMA_ERROR);
    }
    e->real = e->negative ? -value : value;
    return kind;
  }
  uint64_t limit = kind->is_signed ? (UINT64_MAX >> (65 - kind->bits)) +
                                         (unsigned)e->negative
                                   : UINT64_MAX >> (64 - kind->bits);
  uint64_t value = 0;
  int valid = 1;
  for (size_t i = 0; i < token.length; i++) {
    unsigned digit = (unsigned)(token.start[i] - '0');
    if (value > limit / 10 || (value == limit / 10 && digit > limit % 10)) {
      valid = 0;
      break;
    }
    value = value * 10 + digit;
  }
  if (!valid || (e->negative && !kind->is_signed && value != 0)) {
    if (!diagnosed)
      error(c, e->span, "Number literal is outside the range of its type.");
    return t(c, SEMA_ERROR);
  }
  e->integer = value;
  return kind;
}

static const SemaType *variable(Checker *c, SemaExpr *e, Use use) {
  const SemaType *result =
      e->as.variable ? e->as.variable->type : t(c, SEMA_ERROR);
  e->handles_outcome = use == TAIL || use == OUTCOME;
  // Flow reports the actual forbidden read after types stabilize. Do not
  // turn it into unrelated argument, field or recursive-inference errors.
  // Dead reads keep their actual type: there is no live flow obligation,
  // but unreachable syntax must still be type checked.
  if (e->as.variable && e->as.variable->outcome_binding &&
      !e->handles_outcome && e->reachable)
    result = t(c, c->probe ? SEMA_UNKNOWN : SEMA_ERROR);
  return result;
}

static const SemaType *field(Checker *c, SemaExpr *e, unsigned *flow) {
  const SemaType *owner = check_expr(c, e->as.field.object, NULL, VALUE);
  *flow = e->as.field.object->flow;
  e->as.field.field = sema_field(owner, e->as.field.name);
  if (!special(owner) && !e->as.field.field)
    error(c, e->span,
          owner->kind == SEMA_STRUCT ? "Unknown struct field."
                                     : "Field access requires a struct.");
  return e->as.field.field ? e->as.field.field->type
         : unknown(owner)  ? t(c, SEMA_UNKNOWN)
                           : t(c, SEMA_ERROR);
}

static const SemaType *symbol(Checker *c, SemaExpr *e, const SemaType *expected,
                              unsigned *flow) {
  const SemaVariant *context = sema_variant(expected, e->as.symbol.tag);
  size_t count = e->as.symbol.count;
  const SemaType *single = NULL;
  const SemaType **types =
      count > 1 ? arena_calloc(c->p->arena, count, sizeof(*types)) : &single;
  const SemaType *incomplete = NULL;
  for (size_t i = 0; i < count; i++) {
    SemaExpr *payload = e->as.symbol.payloads[i];
    const SemaType *wanted =
        context && i < context->payload_count ? context->payloads[i]
        : !context && e->as.symbol.tag == c->p->ok && count == 1 ? expected
                                                                 : NULL;
    c->reachable = e->reachable && (*flow & SEMA_NEXT);
    const SemaType *pt =
        check_expr(c, payload, wanted,
                   payload->kind == SEMA_EXPR_SYMBOL ? OUTCOME : VALUE);
    *flow = sequence(*flow, payload->flow);
    if (special(pt)) {
      if (!incomplete || pt->kind == SEMA_ERROR)
        incomplete = pt;
    } else if (wanted && sema_assignable(pt, wanted)) {
      // Context can widen a nested union independently at each position.
      pt = wanted;
    }
    types[i] = pt;
  }
  const SemaType *result;
  if (!e->as.symbol.tag)
    result = t(c, SEMA_ERROR);
  else if (incomplete)
    result = incomplete;
  else {
    SemaVariant variant = {e->as.symbol.tag, count ? types : NULL, count};
    result = sema_symbol_set(c->p, &variant, 1);
  }
  // Implicit success has one result slot.
  // Positional data checking must not invent a tuple or erase extra values.
  if (e->as.symbol.tag == c->p->ok && count > 1) {
    error(c, e->span, "Multiple ':ok' payloads are not implemented yet.");
    result = t(c, SEMA_ERROR);
  }
  return result;
}

static const SemaType *structure(Checker *c, SemaExpr *e, unsigned *flow) {
  for (size_t i = 0; i < e->as.structure.count; i++) {
    c->reachable = e->reachable && (*flow & SEMA_NEXT);
    SemaField *field = e->as.structure.fields[i];
    SemaExpr *value = e->as.structure.values[i];
    check_expr(c, value, field ? field->type : NULL, VALUE);
    if (field)
      expect(c, value, field->type,
             "Struct field initializer has the wrong type.");
    *flow = sequence(*flow, value->flow);
  }
  return &e->as.structure.type->type;
}

static const SemaType *ret(Checker *c, SemaExpr *e, unsigned *flow) {
  const SemaType *value = t(c, SEMA_UNIT);
  if (e->as.value) {
    value = check_expr(c, e->as.value, c->expected, TAIL);
    *flow = e->as.value->flow;
  } else {
    const SemaType *context = check_none_context(c, c->expected);
    if (context) {
      value = check_none_value(c, context);
      e->none_ending = 1;
    } else if (c->probe && (!c->function->written || c->function->partial)) {
      if (e->reachable)
        check_empty_return(c, e->span);
      value = t(c, SEMA_UNKNOWN);
    }
  }
  if (*flow & SEMA_NEXT) {
    value =
        check_returned(c, value, e->span,
                       "Return value does not match the written return type.");
    if (e->reachable) {
      c->saw_return = 1;
      if (!c->function->written || c->function->partial)
        c->candidate = check_return_join(c, c->candidate, value, e->span,
                                         "Incompatible return types.");
    }
    *flow = sequence(*flow, SEMA_RETURN);
  }
  return t(c, SEMA_NEVER);
}

static const SemaType *block(Checker *c, SemaExpr *e, const SemaType *expected,
                             Use use, unsigned *flow) {
  const SemaType *result = t(c, SEMA_UNIT);
  for (size_t i = 0; i < e->as.block.count; i++) {
    SemaExpr *item = e->as.block.items[i];
    int last = i + 1 == e->as.block.count;
    c->reachable = e->reachable && (*flow & SEMA_NEXT);
    const SemaType *context = expected;
    if (use == TAIL && special(context) && !special(c->candidate))
      context = c->candidate;
    const SemaType *value =
        check_expr(c, item, last ? context : NULL, last ? use : DISCARD);
    if (*flow & SEMA_NEXT)
      result = value;
    *flow = sequence(*flow, item->flow);
  }
  return result;
}

static const SemaType *dispatch(Checker *c, SemaExpr *e,
                                const SemaType *expected, Use use,
                                unsigned *flow) {
  switch (e->kind) {
  case SEMA_EXPR_UNIT:
    return t(c, SEMA_UNIT);
  case SEMA_EXPR_ERROR:
    return t(c, SEMA_ERROR);
  case SEMA_EXPR_STRING:
    return t(c, SEMA_STR);
  case SEMA_EXPR_INT:
  case SEMA_EXPR_FLOAT:
    return literal(c, e, expected);
  case SEMA_EXPR_BOOL:
    e->integer = e->as.string.length == 4;
    return t(c, SEMA_BOOL);
  case SEMA_EXPR_CHAR: {
    Token token = e->as.string;
    e->integer = token.start[1] == '\\' ? escaped((unsigned char)token.start[2])
                                        : (unsigned char)token.start[1];
    return t(c, SEMA_CHR);
  }
  case SEMA_EXPR_VAR:
    return variable(c, e, use);
  case SEMA_EXPR_BIND:
    return check_bind(c, e, flow);
  case SEMA_EXPR_ASSIGN:
    return check_assign(c, e, flow);
  case SEMA_EXPR_UPDATE:
    return check_update(c, e, flow);
  case SEMA_EXPR_FIELD:
    return field(c, e, flow);
  case SEMA_EXPR_SYMBOL:
    return symbol(c, e, expected, flow);
  case SEMA_EXPR_STRUCT:
    return structure(c, e, flow);
  case SEMA_EXPR_RETURN:
    return ret(c, e, flow);
  case SEMA_EXPR_BREAK:
  case SEMA_EXPR_CONTINUE:
    *flow = e->kind == SEMA_EXPR_BREAK ? SEMA_BREAK : SEMA_CONTINUE;
    return t(c, SEMA_NEVER);
  case SEMA_EXPR_BLOCK:
    return block(c, e, expected, use, flow);
  case SEMA_EXPR_CALL:
    return check_call(c, e, use, flow);
  case SEMA_EXPR_TRY:
  case SEMA_EXPR_PANIC:
    return check_try(c, e, flow);
  case SEMA_EXPR_UNARY:
    return check_unary(c, e, expected, flow);
  case SEMA_EXPR_BINARY:
    return check_binary(c, e, expected, flow);
  case SEMA_EXPR_MATCH:
    return check_match(c, e, expected, use, flow);
  case SEMA_EXPR_IF:
    return check_if(c, e, expected, use, flow);
  case SEMA_EXPR_WHILE:
    return check_while(c, e, use, flow);
  case SEMA_EXPR_FOR:
    return check_for(c, e, use, flow);
  }
  return t(c, SEMA_UNIT);
}

// Contextual result adjustments shared by every expression: unit endings that
// mean :none, implicit :ok wrapping, and probe-only deferrals.
static const SemaType *adjust(Checker *c, SemaExpr *e, const SemaType *result,
                              const SemaType *expected, Use use, size_t errors,
                              size_t conflicts) {
  if (use == DISCARD && expected && expected->kind == SEMA_UNIT &&
      sema_has_errors(result)) {
    error(c, e->span, "An outcome cannot be discarded by a unit return.");
    result = t(c, SEMA_ERROR);
  }
  e->raw_type = e->returns ? t(c, SEMA_NEVER) : result;
  int explicit_ok = e->kind == SEMA_EXPR_SYMBOL && e->as.symbol.tag == c->p->ok;
  const SemaType *context =
      use == TAIL ? check_none_context(c, expected) : NULL;
  if (!e->returns && result->kind == SEMA_UNIT && context &&
      !(explicit_ok && sema_ok(context))) {
    result = check_none_value(c, context);
    e->none_ending = 1;
  }
  const SemaType *wrap = explicit_ok && context ? context : expected;
  const SemaVariant *ok = sema_ok(wrap);
  // An outcome returned as-is must fit the outer result, not become a
  // successful nested outcome. Only explicit :ok construction can do that.
  if (!e->returns && !special(result) && ok && ok->payload_count <= 1 &&
      (use == TAIL || explicit_ok) &&
      (explicit_ok || !sema_has_errors(result)) &&
      !(c->probe && !c->function->written && result->kind == SEMA_UNIT) &&
      !sema_assignable(result, wrap) &&
      sema_assignable(result, check_success_type(c, ok))) {
    result = wrap;
    e->adapt = 1;
  }
  // Decide inferred valueless paths only after seeing every reachable return.
  // A later :none makes an earlier empty return absence, not an implicit :ok.
  if (c->probe && !e->returns && use == TAIL && result->kind == SEMA_UNIT &&
      (!c->function->written || c->function->partial)) {
    if (e->reachable)
      check_empty_return(c, e->span);
    result = t(c, SEMA_UNKNOWN);
  }
  // A locally diagnosed error result is not a return-type anchor. Poisoning
  // an SCC with it can erase the types/context needed to report the primary
  // error on the final pass. Defer only errors observed in this expression's
  // probe; inherited errors without a local failure and intrinsic inference
  // conflicts still propagate. Final reports never depend on these counters.
  if (c->probe && result->kind == SEMA_ERROR &&
      c->speculative_errors != errors && c->speculative_conflicts == conflicts)
    result = t(c, SEMA_UNKNOWN);
  return result;
}

const SemaType *check_expr(Checker *c, SemaExpr *e, const SemaType *expected,
                           Use use) {
  size_t errors = c->speculative_errors;
  size_t conflicts = c->speculative_conflicts;
  unsigned flow = SEMA_NEXT;
  int reachable = c->reachable;
  e->reachable = reachable;
  e->adapt = e->none_ending = e->handles_outcome = e->assignment_invalid = 0;
  e->failures = NULL;
  const SemaType *result = dispatch(c, e, expected, use, &flow);
  c->reachable = reachable;
  e->flow = flow;
  e->returns = !(flow & SEMA_NEXT);
  result = adjust(c, e, result, expected, use, errors, conflicts);
  e->type = e->returns ? t(c, SEMA_NEVER) : result;
  return e->type;
}
