#include "sema/sema.h"
#include <math.h>
#include <string.h>

typedef struct {
  uint64_t first, last;
} Interval;
static int floating(const SemaType *t) {
  return t->kind == SEMA_FLT || t->kind == SEMA_DUB;
}
static uint64_t mask(const SemaType *t) { return UINT64_MAX >> (64 - t->bits); }
// Monotone ranks avoid signed overflow, including the complete ulng domain.
// IEEE ranks put -0 immediately before +0; normalization treats them as equal.
static uint64_t rank(const SemaType *t, const SemaExpr *e) {
  uint64_t raw;
  if (floating(t)) {
    if (t->kind == SEMA_FLT) {
      float f = (float)e->real;
      uint32_t bits;
      memcpy(&bits, &f, sizeof(bits));
      raw = bits;
    } else
      memcpy(&raw, &e->real, sizeof(raw));
    uint64_t sign = UINT64_C(1) << (t->bits - 1);
    return raw & sign ? (~raw & mask(t)) : raw ^ sign;
  }
  raw = (e->negative ? UINT64_C(0) - e->integer : e->integer) & mask(t);
  return t->is_signed ? raw ^ (UINT64_C(1) << (t->bits - 1)) : raw;
}
static uint64_t low_rank(const SemaType *t, const SemaExpr *e) {
  SemaExpr zero = {.real = -0.0};
  return floating(t) && e->real == 0 ? rank(t, &zero) : rank(t, e);
}
static uint64_t high_rank(const SemaType *t, const SemaExpr *e) {
  SemaExpr zero = {.real = 0.0};
  return floating(t) && e->real == 0 ? rank(t, &zero) : rank(t, e);
}
static SemaExpr *bound(Arena *arena, const SemaType *t, uint64_t r, Span span) {
  SemaExpr *e = arena_calloc(arena, 1, sizeof(*e));
  *e = (SemaExpr){.kind = SEMA_EXPR_INT, .type = t, .span = span};
  uint64_t sign = UINT64_C(1) << (t->bits - 1), raw;
  if (floating(t)) {
    raw = r & sign ? r ^ sign : ~r & mask(t);
    e->kind = SEMA_EXPR_FLOAT;
    if (t->kind == SEMA_FLT) {
      uint32_t bits = (uint32_t)raw;
      float f;
      memcpy(&f, &bits, sizeof(f));
      e->real = f;
    } else
      memcpy(&e->real, &raw, sizeof(raw));
  } else {
    raw = t->is_signed ? r ^ sign : r;
    e->negative = t->is_signed && (raw & sign);
    e->integer = e->negative ? (UINT64_C(0) - raw) & mask(t) : raw;
  }
  return e;
}
static int normalize(SemaArm *a, const SemaType *t, uint64_t min,
                     uint64_t max) {
  a->first = min;
  a->last = max;
  if (a->any)
    return 1;
  if (!a->lower || a->lower->type != t || (a->upper && a->upper->type != t))
    return 0;
  uint64_t lo = low_rank(t, a->lower), hi = high_rank(t, a->lower);
  if (a->kind == PAT_LITERAL) {
    a->first = lo;
    a->last = hi;
  } else if (a->kind == PAT_RANGE) {
    a->first = lo;
    uint64_t end = low_rank(t, a->upper);
    if (!end || end <= lo)
      return 2; // Empty, without end - 1 underflow.
    a->last = end - 1;
  } else if (a->kind == PAT_COMPARE) {
    switch (a->comparison) {
    case BINARY_LT:
      if (lo == min)
        return 2;
      a->last = lo - 1;
      break;
    case BINARY_LE:
      a->last = hi;
      break;
    case BINARY_GT:
      if (hi == max)
        return 2;
      a->first = hi + 1;
      break;
    case BINARY_GE:
      a->first = lo;
      break;
    default:
      return 0;
    }
  } else
    return 0;
  return a->first <= a->last ? 1 : 2;
}
static int contains(const Interval *covered, size_t count, Interval x) {
  for (size_t i = 0; i < count; i++)
    if (covered[i].first <= x.first && covered[i].last >= x.last)
      return 1;
  return 0;
}
static void add(Interval *covered, size_t *count, Interval x) {
  size_t at = (*count)++;
  while (at && covered[at - 1].first > x.first) {
    covered[at] = covered[at - 1];
    at--;
  }
  covered[at] = x;
  size_t n = 0;
  for (size_t i = 0; i < *count; i++) {
    Interval y = covered[i];
    if (n && (y.first <= covered[n - 1].last ||
              (covered[n - 1].last != UINT64_MAX &&
               y.first == covered[n - 1].last + 1))) {
      if (y.last > covered[n - 1].last)
        covered[n - 1].last = y.last;
    } else
      covered[n++] = y;
  }
  *count = n;
}
void sema_match_coverage(SemaProgram *p, SemaExpr *match, Diagnostics *errors) {
  const SemaType *t = match->as.match.input->type;
  size_t count = match->as.match.count;
  for (size_t i = 0; i < count; i++) {
    SemaArm *a = &match->as.match.arms[i];
    a->any = a->kind == PAT_BIND || a->kind == PAT_WILDCARD;
    a->useful = 1;
    a->first_value = a->last_value = NULL;
  }
  if (!t || t->kind <= SEMA_ERROR)
    return;
  int numeric = sema_number(t) || t->kind == SEMA_BOOL || t->kind == SEMA_CHR;
  uint64_t min = 0, max = numeric ? mask(t) : 0;
  if (floating(t)) {
    SemaExpr lo = {.real = -INFINITY}, hi = {.real = INFINITY};
    min = rank(t, &lo);
    max = rank(t, &hi);
  }
  Interval *covered = arena_calloc(p->arena, count, sizeof(*covered));
  size_t used = 0;
  int all = 0, valid = 1;
  for (size_t i = 0; i < count; i++) {
    SemaArm *a = &match->as.match.arms[i];
    // Pattern typing already diagnosed unsupported shapes; do not add
    // misleading missing/unreachable-case diagnostics for them.
    int status = a->invalid || (!numeric && !a->any) ||
                         (t->kind == SEMA_BOOL &&
                          (a->kind == PAT_RANGE || a->kind == PAT_COMPARE))
                     ? 0
                     : normalize(a, t, min, max);
    if (!status) {
      valid = 0;
      continue;
    }
    Interval x = {a->first, a->last};
    a->useful = status != 2 && !all &&
                (a->any && floating(t) ? 1 : !contains(covered, used, x));
    if (!a->useful) {
      if (errors)
        diagnostics_error(errors, a->span, "Unreachable match arm.");
      continue;
    }
    add(covered, &used, x);
    if (a->any)
      all = 1;
    if (!a->any) {
      a->first_value = bound(p->arena, t, a->first, a->span);
      a->last_value = bound(p->arena, t, a->last, a->span);
    }
  }
  if (valid && !all &&
      (!numeric || floating(t) ||
       !contains(covered, used, (Interval){min, max}))) {
    if (errors)
      diagnostics_error(
          errors, match->span,
          floating(t) ? "Match is not exhaustive; add a catch-all for NaN."
                      : "Match is not exhaustive.");
  }
}
