#ifndef BRIEF_SEMA_CHECK_INTERNAL_H
#define BRIEF_SEMA_CHECK_INTERNAL_H

#include "sema/internal.h"

// How the enclosing expression consumes a value. TAIL reaches ret values and
// value-producing branch/block endings; BOUND is a let/var initializer.
typedef enum { DISCARD, VALUE, TAIL, OUTCOME, BOUND } Use;
typedef struct {
  SemaProgram *p;
  SemaFunction *function;
  Diagnostics *errors;
  int probe, defaults, reachable, saw_return;
  const SemaType *candidate, *expected, *failures;
  size_t speculative_errors, speculative_conflicts;
  int empty_return;
  Span empty_span;
} Checker;

static inline const SemaType *t(Checker *c, SemaTypeKind k) {
  return &c->p->types[k];
}
static inline int unknown(const SemaType *a) {
  return !a || a->kind == SEMA_UNKNOWN;
}
static inline int special(const SemaType *a) {
  return unknown(a) || a->kind == SEMA_ERROR || a->kind == SEMA_NEVER;
}
static inline unsigned sequence(unsigned a, unsigned b) {
  return (a & ~SEMA_NEXT) | ((a & SEMA_NEXT) ? b : 0);
}
// Probes count errors instead of reporting them.
static inline Diagnostic *error(Checker *c, Span span, const char *message) {
  if (c->probe) {
    c->speculative_errors++;
    return NULL;
  }
  return diagnostics_error(c->errors, span, message);
}
static inline void expect(Checker *c, SemaExpr *e, const SemaType *wanted,
                          const char *message) {
  if (!special(wanted) && !special(e->type) &&
      !sema_assignable(e->type, wanted))
    error(c, e->span, message);
}

// returns.c: joining branch and return types.

const SemaType *check_join(Checker *c, const SemaType *a, const SemaType *b,
                           Span span, const char *message);
const SemaType *check_return_join(Checker *c, const SemaType *a,
                                  const SemaType *b, Span span,
                                  const char *message);
const SemaType *check_success_type(Checker *c, const SemaVariant *ok);
const SemaType *check_none_context(Checker *c, const SemaType *expected);
const SemaType *check_none_value(Checker *c, const SemaType *context);
void check_empty_return(Checker *c, Span span);
int check_return_fits(Checker *c, const SemaType *actual);
// A value rejected by the written return type becomes never.
const SemaType *check_returned(Checker *c, const SemaType *actual, Span span,
                               const char *message);

// hints.c: context types inferred from already-typed neighbours.

const SemaType *check_hint(SemaExpr *e);
const SemaType *check_return_hint(SemaExpr *e);
const SemaType *check_conversion_context(SemaSymbol *s, SemaExpr *arg,
                                         const SemaType *fallback);
const SemaType *check_pipeline_hint(Checker *c, SemaExpr *binding);
const SemaType *check_operand_type(Checker *c, SemaExpr *a, SemaExpr *b,
                                   const SemaType *expected);

// Expression checking. Each handler returns the expression's result type and
// stores its control flow in *flow, which starts as SEMA_NEXT.

const SemaType *check_expr(Checker *c, SemaExpr *e, const SemaType *expected,
                           Use use);
const SemaType *check_bind(Checker *c, SemaExpr *e, unsigned *flow);
const SemaType *check_assign(Checker *c, SemaExpr *e, unsigned *flow);
const SemaType *check_update(Checker *c, SemaExpr *e, unsigned *flow);
const SemaType *check_call(Checker *c, SemaExpr *e, Use use, unsigned *flow);
const SemaType *check_unary(Checker *c, SemaExpr *e, const SemaType *expected,
                            unsigned *flow);
const SemaType *check_binary(Checker *c, SemaExpr *e, const SemaType *expected,
                             unsigned *flow);
const SemaType *check_try(Checker *c, SemaExpr *e, unsigned *flow);
const SemaType *check_match(Checker *c, SemaExpr *e, const SemaType *expected,
                            Use use, unsigned *flow);
const SemaType *check_if(Checker *c, SemaExpr *e, const SemaType *expected,
                         Use use, unsigned *flow);
const SemaType *check_while(Checker *c, SemaExpr *e, Use use, unsigned *flow);
const SemaType *check_for(Checker *c, SemaExpr *e, Use use, unsigned *flow);

#endif
