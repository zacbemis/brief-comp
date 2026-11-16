#include "sema/check/internal.h"

const SemaType *check_unary(Checker *c, SemaExpr *e, const SemaType *expected,
                            unsigned *flow) {
  SemaExpr *value = e->as.unary.value;
  UnaryOp op = e->as.unary.op;
  const SemaVariant *ok = sema_ok(expected);
  const SemaType *context = ok ? check_success_type(c, ok) : expected;
  const SemaType *result =
      check_expr(c, value, op == UNARY_NOT ? t(c, SEMA_BOOL) : context, VALUE);
  *flow = value->flow;
  if (op == UNARY_NOT) {
    expect(c, value, t(c, SEMA_BOOL), "Logical not requires bool.");
    return t(c, SEMA_BOOL);
  }
  if (!special(result) &&
      !(op == UNARY_BIT_NOT ? sema_integer(result) : sema_number(result)))
    error(c, e->span,
          "This unary operator requires numbers (bitwise not requires "
          "integers).");
  return result;
}

static const SemaType *shift(Checker *c, SemaExpr *e, const SemaType *at,
                             const SemaType *bt) {
  if (!special(at) && !sema_integer(at))
    error(c, e->as.binary.left->span, "The shifted value must be an integer.");
  if (!special(bt) && !sema_integer(bt))
    error(c, e->as.binary.right->span, "A shift count must be an integer.");
  return at; // The count never anchors or widens the shifted value.
}

static const SemaType *comparison(Checker *c, SemaExpr *e,
                                  const SemaType *result) {
  BinaryOp op = e->as.binary.op;
  int equality = op == BINARY_EQ || op == BINARY_NE;
  int shaped = result->kind == SEMA_STRUCT || result->kind == SEMA_SYM;
  if (!special(result) &&
      !(sema_number(result) || result->kind == SEMA_CHR ||
        result->kind == SEMA_STR ||
        (equality && (result->kind == SEMA_BOOL || result->kind == SEMA_UNIT ||
                      (shaped && sema_equatable(result))))))
    // Only recursive shapes make a struct or symbol non-equatable. That is
    // a language rule, unlike ordering, which is not implemented yet.
    error(c, e->span,
          equality && shaped
              ? "Types that can contain a reference cycle have no == or !=."
              : "Comparison of this type is not implemented yet.");
  return t(c, SEMA_BOOL);
}

const SemaType *check_binary(Checker *c, SemaExpr *e, const SemaType *expected,
                             unsigned *flow) {
  SemaExpr *a = e->as.binary.left, *b = e->as.binary.right;
  BinaryOp op = e->as.binary.op;
  int logical = op == BINARY_LOGICAL_AND || op == BINARY_LOGICAL_OR;
  int compare = op >= BINARY_EQ && op <= BINARY_GE;
  int shifting = op == BINARY_LSHIFT || op == BINARY_RSHIFT;
  const SemaType *context = logical
                                ? t(c, SEMA_BOOL)
                                : check_operand_type(c, a, shifting ? NULL : b,
                                                     compare ? NULL : expected);
  const SemaType *at = check_expr(c, a, context, VALUE);
  c->reachable = e->reachable && (a->flow & SEMA_NEXT);
  const SemaType *bt =
      check_expr(c, b, shifting ? NULL : (special(at) ? context : at), VALUE);
  *flow = sequence(a->flow, logical ? (b->flow | SEMA_NEXT) : b->flow);
  e->as.binary.common = NULL;
  if (shifting)
    return shift(c, e, at, bt);
  const SemaType *result;
  if (at->kind == SEMA_SYM && bt->kind == SEMA_SYM) {
    result = sema_symbol_union(c->p, at, bt);
    if (!result) {
      error(c, e->span, "Symbol tag has conflicting payload types.");
      result = t(c, SEMA_ERROR);
    } else
      e->as.binary.common = result;
  } else {
    if (!special(at) && !special(bt) && at != bt)
      error(c, e->span,
            "Operator operands must have the same type; numeric conversions "
            "are explicit.");
    result = special(at) ? bt : at;
  }
  if (logical) {
    expect(c, a, t(c, SEMA_BOOL), "Logical operators require bool.");
    expect(c, b, t(c, SEMA_BOOL), "Logical operators require bool.");
    return t(c, SEMA_BOOL);
  }
  if (compare)
    return comparison(c, e, result);
  if (!special(result)) {
    int integer = op == BINARY_REM || op >= BINARY_BIT_AND;
    if (!(integer ? sema_integer(result) : sema_number(result)) &&
        !(op == BINARY_ADD && result->kind == SEMA_STR))
      error(c, e->span,
            integer ? "This operator requires integers."
                    : "Arithmetic requires numbers.");
  }
  return result;
}
