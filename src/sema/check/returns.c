#include "sema/check/internal.h"

#include <string.h>

static int ambiguous_pair(const SemaType *a, const SemaType *b) {
  const SemaVariant *aok = sema_ok(a), *bok = sema_ok(b);
  return (aok && !aok->payload_count && sema_none(b)) ||
         (bok && !bok->payload_count && sema_none(a));
}
const SemaType *check_join(Checker *c, const SemaType *a, const SemaType *b,
                           Span span, const char *message) {
  if (a->kind == SEMA_NEVER || unknown(a))
    return b;
  if (b->kind == SEMA_NEVER || unknown(b))
    return a;
  if (a == b)
    return a;
  if (a->kind == SEMA_SYM && b->kind == SEMA_SYM) {
    const SemaType *set = sema_symbol_union(c->p, a, b);
    if (set)
      return set;
    if (ambiguous_pair(a, b)) {
      error(c, span,
            "A union cannot contain both payload-less ':ok' and ':none'.");
      return t(c, SEMA_ERROR);
    }
    message = "Symbol tag has conflicting payload types.";
  }
  if (a->kind != SEMA_ERROR && b->kind != SEMA_ERROR) {
    c->function->inference_conflict = 1;
    if (c->probe)
      c->speculative_conflicts++;
    else
      c->function->inference_reported = 1;
    error(c, span, message);
  }
  return t(c, SEMA_ERROR);
}
const SemaType *check_success_type(Checker *c, const SemaVariant *ok) {
  return ok && ok->payload_count ? sema_single_payload(ok) : t(c, SEMA_UNIT);
}
const SemaType *check_none_context(Checker *c, const SemaType *expected) {
  if (sema_none(expected))
    return expected;
  if (sema_none(c->expected))
    return c->expected;
  if (sema_none(c->candidate))
    return c->candidate;
  return sema_none(c->function->symbol.outcome_type)
             ? c->function->symbol.outcome_type
             : NULL;
}
const SemaType *check_none_value(Checker *c, const SemaType *context) {
  const SemaVariant *none = sema_none(context);
  return sema_symbol_set(c->p, none, 1);
}
void check_empty_return(Checker *c, Span span) {
  if (!c->empty_return)
    c->empty_span = span;
  c->empty_return = 1;
}
const SemaType *check_return_join(Checker *c, const SemaType *a,
                                  const SemaType *b, Span span,
                                  const char *message) {
  if (c->function->partial && ambiguous_pair(a, b)) {
    error(c, c->function->syntax->as.function.return_type->span,
          "A union cannot contain both payload-less ':ok' and ':none'.");
    return t(c, SEMA_ERROR);
  }
  if (!special(a) && !special(b) &&
      ((a->kind == SEMA_SYM) != (b->kind == SEMA_SYM))) {
    const SemaType *symbols = a->kind == SEMA_SYM ? a : b;
    const SemaType *value = a->kind == SEMA_SYM ? b : a;
    if (value->kind == SEMA_UNIT && sema_none(symbols) && !c->function->partial)
      return symbols;
    size_t count = symbols->symbols->count;
    SemaVariant *variants =
        arena_calloc(c->p->arena, count + 1, sizeof(*variants));
    memcpy(variants, symbols->symbols->variants, count * sizeof(*variants));
    variants[count] =
        c->function->partial && c->function->written_ok
            ? *c->function->written_ok
            : (SemaVariant){c->p->ok, value->kind == SEMA_UNIT ? NULL : &value,
                            value->kind == SEMA_UNIT ? 0 : 1};
    if (sema_assignable(value, check_success_type(c, &variants[count]))) {
      const SemaType *result = sema_symbol_set(c->p, variants, count + 1);
      if (result)
        return result;
      if (sema_ambiguous_endings(variants, count + 1)) {
        error(c, c->function->syntax->as.function.return_type->span,
              "A union cannot contain both payload-less ':ok' and ':none'.");
        return t(c, SEMA_ERROR);
      }
    }
    message = "Symbol tag has conflicting payload types.";
  }
  return check_join(c, a, b, span, message);
}
int check_return_fits(Checker *c, const SemaType *actual) {
  const SemaType *wanted = c->function->written_type;
  if (special(actual) || special(wanted))
    return 1;
  const SemaVariant *wok = c->function->written_ok;
  if (!c->function->partial) {
    wok = sema_ok(wanted);
    return sema_assignable(actual, wanted) ||
           (wok && !sema_has_errors(actual) &&
            sema_assignable(actual, check_success_type(c, wok)));
  }
  if (actual->kind != SEMA_SYM)
    return wok && sema_assignable(actual, check_success_type(c, wok));
  const SemaVariant *ok = sema_ok(actual);
  return !ok || (wok && sema_payloads_equal(ok, wok));
}
const SemaType *check_returned(Checker *c, const SemaType *actual, Span span,
                               const char *message) {
  if (c->function->written && !check_return_fits(c, actual)) {
    error(c, span, message);
    // A value rejected by the written contract contributes nothing: it must
    // not collide with a partial annotation's :ok payload, and the function
    // keeps its written contract so callers are still checked against it.
    return t(c, SEMA_NEVER);
  }
  return actual;
}
