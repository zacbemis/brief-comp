#include "sema/sema.h"
#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef enum { DISCARD, VALUE, TAIL } Use;
typedef struct {
  SemaProgram *p;
  SemaFunction *function;
  Diagnostics *errors;
  int probe, defaults, reachable, saw_return;
  const SemaType *candidate, *expected;
} Checker;
static const SemaType *t(Checker *c, SemaTypeKind k) { return &c->p->types[k]; }
static int unknown(const SemaType *a) { return !a || a->kind == SEMA_UNKNOWN; }
static int special(const SemaType *a) {
  return unknown(a) || a->kind == SEMA_ERROR || a->kind == SEMA_NEVER;
}
static void error(Checker *c, Span span, const char *message) {
  if (!c->probe)
    diagnostics_error(c->errors, span, message);
}
static void expect(Checker *c, SemaExpr *e, const SemaType *wanted,
                   const char *message) {
  if (!special(wanted) && !special(e->type) && e->type != wanted)
    error(c, e->span, message);
}
static const SemaType *join(Checker *c, const SemaType *a, const SemaType *b,
                            Span span, const char *message) {
  if (a->kind == SEMA_NEVER || unknown(a))
    return b;
  if (b->kind == SEMA_NEVER || unknown(b))
    return a;
  if (a == b)
    return a;
  if (a->kind != SEMA_ERROR && b->kind != SEMA_ERROR) {
    c->function->inference_conflict = 1;
    if (!c->probe)
      c->function->inference_reported = 1;
    error(c, span, message);
  }
  return t(c, SEMA_ERROR);
}
static const SemaType *hint(SemaExpr *e) {
  if (!e)
    return NULL;
  switch (e->kind) {
  case SEMA_EXPR_VAR:
    return e->as.variable ? e->as.variable->type : NULL;
  case SEMA_EXPR_CALL:
    return e->as.call.target ? e->as.call.target->return_type : NULL;
  case SEMA_EXPR_STRUCT:
    return &e->as.structure.type->type;
  case SEMA_EXPR_FIELD: {
    SemaField *f = sema_field(hint(e->as.field.object), e->as.field.name);
    return f ? f->type : e->type;
  }
  case SEMA_EXPR_UNARY: {
    const SemaType *h = hint(e->as.unary.value);
    return e->as.unary.op == UNARY_NOT || special(h) ? e->type : h;
  }
  case SEMA_EXPR_BINARY:
    if (e->as.binary.op >= BINARY_EQ && e->as.binary.op <= BINARY_LOGICAL_OR)
      return e->type;
    {
      const SemaType *a = hint(e->as.binary.left);
      if (special(a))
        a = hint(e->as.binary.right);
      return special(a) ? e->type : a;
    }
  case SEMA_EXPR_IF: {
    const SemaType *a = hint(e->as.conditional.yes);
    if (special(a))
      a = hint(e->as.conditional.no);
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
        return hint(item);
    }
    return NULL;
  case SEMA_EXPR_INT:
  case SEMA_EXPR_FLOAT:
    return NULL; // Contextual, never an anchor.
  default:
    return e->type;
  }
}
// Only reachable returns contribute anchors. Flow is established in a first
// non-defaulting pass, before using these hints to contextualize literals.
static const SemaType *return_hint(SemaExpr *e) {
  if (!e || !e->flow || !e->reachable)
    return NULL;
  if (e->kind == SEMA_EXPR_RETURN)
    return e->as.value ? hint(e->as.value) : e->type;
  if (e->kind == SEMA_EXPR_BLOCK) {
    for (size_t i = 0; i < e->as.block.count; i++) {
      SemaExpr *item = e->as.block.items[i];
      const SemaType *h = return_hint(item);
      if (!special(h))
        return h;
      if (!(item->flow & SEMA_NEXT))
        break;
    }
  } else if (e->kind == SEMA_EXPR_IF) {
    const SemaType *h = return_hint(e->as.conditional.condition);
    if (!special(h))
      return h;
    h = return_hint(e->as.conditional.yes);
    return special(h) ? return_hint(e->as.conditional.no) : h;
  } else if (e->kind == SEMA_EXPR_WHILE)
    return return_hint(e->as.while_loop.body);
  else if (e->kind == SEMA_EXPR_FOR)
    return return_hint(e->as.for_loop.body);
  return NULL;
}
static unsigned sequence(unsigned a, unsigned b) {
  return (a & ~SEMA_NEXT) | ((a & SEMA_NEXT) ? b : 0);
}
static unsigned char escaped(unsigned char c) {
  return c == 'n' ? '\n' : c == 't' ? '\t' : c == '0' ? 0 : c;
}
static const SemaType *literal(Checker *c, SemaExpr *e,
                               const SemaType *expected) {
  const SemaType *kind = sema_number(expected) ? expected : NULL;
  if (e->kind == SEMA_EXPR_FLOAT && sema_integer(kind))
    kind = NULL;
  if (!kind) {
    if (!c->defaults || (c->defaults == 1 && e->kind == SEMA_EXPR_INT))
      return t(c, SEMA_UNKNOWN);
    kind = t(c, e->kind == SEMA_EXPR_FLOAT ? SEMA_DUB : SEMA_INT);
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
    error(c, e->span, "Number literal is outside the range of its type.");
    return t(c, SEMA_ERROR);
  }
  e->integer = value;
  return kind;
}
static const SemaType *check(Checker *c, SemaExpr *e, const SemaType *expected,
                             Use use);
static void mutable(Checker *c, SemaVar *v, Span span) {
  if (v && !v->mutable)
    error(c, span, "Cannot reassign an immutable binding.");
}
static const SemaType *lvalue(Checker *c, SemaExpr *e) {
  if (e->as.binding.target) {
    SemaExpr *target = e->as.binding.target;
    return check(c, target, NULL, VALUE);
  }
  mutable(c, e->as.binding.variable, e->span);
  return e->as.binding.variable ? e->as.binding.variable->type
                                : t(c, SEMA_ERROR);
}
static const SemaType *operand_type(Checker *c, SemaExpr *a, SemaExpr *b,
                                    const SemaType *expected) {
  if (sema_number(expected))
    return expected;
  const SemaType *h = hint(a);
  if (!special(h))
    return h;
  h = hint(b);
  if (!special(h))
    return h;
  // Let the literal on either side establish the fallback; 1 + 2.0 is dub.
  if (c->defaults) {
    if ((a && a->kind == SEMA_EXPR_FLOAT) || (b && b->kind == SEMA_EXPR_FLOAT))
      return t(c, SEMA_DUB);
    if (c->defaults >= 2)
      return t(c, SEMA_INT);
  }
  return t(c, SEMA_UNKNOWN);
}
static const SemaType *check(Checker *c, SemaExpr *e, const SemaType *expected,
                             Use use) {
  const SemaType *result = t(c, SEMA_UNIT);
  unsigned flow = SEMA_NEXT;
  int reachable = c->reachable;
  e->reachable = reachable;
  switch (e->kind) {
  case SEMA_EXPR_UNIT:
    break;
  case SEMA_EXPR_ERROR:
    result = t(c, SEMA_ERROR);
    break;
  case SEMA_EXPR_STRING:
    result = t(c, SEMA_STR);
    break;
  case SEMA_EXPR_INT:
  case SEMA_EXPR_FLOAT:
    result = literal(c, e, expected);
    break;
  case SEMA_EXPR_BOOL:
    result = t(c, SEMA_BOOL);
    e->integer = e->as.string.length == 4;
    break;
  case SEMA_EXPR_CHAR: {
    Token token = e->as.string;
    result = t(c, SEMA_CHR);
    e->integer = token.start[1] == '\\' ? escaped((unsigned char)token.start[2])
                                        : (unsigned char)token.start[1];
    break;
  }
  case SEMA_EXPR_VAR:
    result = e->as.variable ? e->as.variable->type : t(c, SEMA_ERROR);
    break;
  case SEMA_EXPR_BIND: {
    SemaVar *v = e->as.binding.variable;
    SemaExpr *value = e->as.binding.value;
    v->type = check(c, value, v->annotation, VALUE);
    expect(c, value, v->annotation,
           "Binding value does not match its annotation.");
    if (!unknown(v->annotation))
      v->type = v->annotation;
    flow = value->flow;
    break;
  }
  case SEMA_EXPR_ASSIGN: {
    const SemaType *target = lvalue(c, e);
    if (e->as.binding.target)
      flow = e->as.binding.target->flow;
    c->reachable = reachable && (flow & SEMA_NEXT);
    SemaExpr *value = e->as.binding.value;
    check(c, value, target, VALUE);
    flow = sequence(flow, value->flow);
    expect(c, value, target, "Assignment value has the wrong type.");
    if (e->as.binding.op != ASSIGN_SET && !special(target) &&
        !sema_number(target) &&
        !(target->kind == SEMA_STR && e->as.binding.op == ASSIGN_ADD))
      error(c, e->span,
            "Compound assignment requires numbers (or str for +=).");
    if (e->as.binding.op == ASSIGN_REM && sema_number(target) &&
        !sema_integer(target))
      error(c, e->span, "Remainder requires integers.");
    break;
  }
  case SEMA_EXPR_UPDATE: {
    const SemaType *target = lvalue(c, e);
    if (e->as.binding.target)
      flow = e->as.binding.target->flow;
    if (!special(target) && !sema_number(target))
      error(c, e->span, "Increment and decrement require a numeric binding.");
    break;
  }
  case SEMA_EXPR_FIELD: {
    const SemaType *owner = check(c, e->as.field.object, NULL, VALUE);
    flow = e->as.field.object->flow;
    e->as.field.field = sema_field(owner, e->as.field.name);
    result = e->as.field.field ? e->as.field.field->type
             : unknown(owner)  ? t(c, SEMA_UNKNOWN)
                               : t(c, SEMA_ERROR);
    if (!special(owner) && !e->as.field.field)
      error(c, e->span,
            owner->kind == SEMA_STRUCT ? "Unknown struct field."
                                       : "Field access requires a struct.");
    break;
  }
  case SEMA_EXPR_STRUCT:
    for (size_t i = 0; i < e->as.structure.count; i++) {
      c->reachable = reachable && (flow & SEMA_NEXT);
      SemaField *field = e->as.structure.fields[i];
      SemaExpr *value = e->as.structure.values[i];
      check(c, value, field ? field->type : NULL, VALUE);
      if (field)
        expect(c, value, field->type,
               "Struct field initializer has the wrong type.");
      flow = sequence(flow, value->flow);
    }
    result = &e->as.structure.type->type;
    break;
  case SEMA_EXPR_RETURN: {
    const SemaType *value = t(c, SEMA_UNIT);
    if (e->as.value) {
      value = check(c, e->as.value, c->expected, VALUE);
      flow = e->as.value->flow;
    }
    if (flow & SEMA_NEXT) {
      if (c->function->written && !special(value) && value != c->expected)
        error(c, e->span,
              "Return value does not match the written return type.");
      if (reachable) {
        c->saw_return = 1;
        if (!c->function->written)
          c->candidate = join(c, c->candidate, value, e->span,
                              "Incompatible return types.");
      }
      flow = sequence(flow, SEMA_RETURN);
    }
    result = t(c, SEMA_NEVER);
    break;
  }
  case SEMA_EXPR_BREAK:
  case SEMA_EXPR_CONTINUE:
    result = t(c, SEMA_NEVER);
    flow = e->kind == SEMA_EXPR_BREAK ? SEMA_BREAK : SEMA_CONTINUE;
    break;
  case SEMA_EXPR_BLOCK:
    for (size_t i = 0; i < e->as.block.count; i++) {
      SemaExpr *item = e->as.block.items[i];
      c->reachable = reachable && (flow & SEMA_NEXT);
      const SemaType *context = expected;
      if (use == TAIL && special(context) && !special(c->candidate))
        context = c->candidate;
      const SemaType *value =
          check(c, item, i + 1 == e->as.block.count ? context : NULL,
                i + 1 == e->as.block.count ? use : DISCARD);
      if (flow & SEMA_NEXT)
        result = value;
      flow = sequence(flow, item->flow);
    }
    break;
  case SEMA_EXPR_CALL: {
    SemaSymbol *s = e->as.call.target;
    if (e->as.call.method) {
      const SemaType *receiver = check(c, e->as.call.args[0], NULL, VALUE);
      s = e->as.call.target = sema_method(c->p, receiver, e->as.call.method);
      if (!special(receiver)) {
        if (!s)
          error(c, e->span, "Unknown method for this type.");
        else if (!s->arity || s->params[0]->type != receiver)
          error(c, e->span,
                "A method requires a first parameter of the receiver type.");
      }
    }
    for (size_t i = 0; i < e->as.call.count; i++) {
      c->reachable = reachable && (flow & SEMA_NEXT);
      SemaExpr *arg = e->as.call.args[i];
      const SemaType *wanted = s && i < s->arity ? s->params[i]->type : NULL;
      if (!(e->as.call.method && i == 0))
        check(c, arg, wanted, VALUE);
      expect(c, arg, wanted,
             s && s->builtin == SEMA_BUILTIN_PRINT
                 ? "print expects a str argument."
                 : "Function argument has the wrong type.");
      if (s && s->builtin == SEMA_BUILTIN_STR && !special(arg->type) &&
          !sema_number(arg->type) && arg->type->kind != SEMA_BOOL)
        error(c, arg->span, "str expects a number or bool argument.");
      flow = sequence(flow, arg->flow);
    }
    if (s && e->as.call.count != s->arity)
      error(c, e->span,
            s->builtin == SEMA_BUILTIN_PRINT
                ? "print expects exactly one str argument."
            : s->builtin == SEMA_BUILTIN_STR
                ? "str expects exactly one number or bool argument."
                : "Wrong number of function arguments.");
    result = s ? s->return_type
             : e->as.call.method && unknown(e->as.call.args[0]->type)
                 ? t(c, SEMA_UNKNOWN)
                 : t(c, SEMA_ERROR);
    break;
  }
  case SEMA_EXPR_UNARY: {
    SemaExpr *value = e->as.unary.value;
    UnaryOp op = e->as.unary.op;
    result =
        check(c, value, op == UNARY_NOT ? t(c, SEMA_BOOL) : expected, VALUE);
    flow = value->flow;
    if (op == UNARY_NOT) {
      expect(c, value, t(c, SEMA_BOOL), "Logical not requires bool.");
      result = t(c, SEMA_BOOL);
    } else {
      if (!special(result) &&
          !(op == UNARY_BIT_NOT ? sema_integer(result) : sema_number(result)))
        error(c, e->span,
              "This unary operator requires numbers (bitwise not requires "
              "integers).");
    }
    break;
  }
  case SEMA_EXPR_BINARY: {
    SemaExpr *a = e->as.binary.left, *b = e->as.binary.right;
    BinaryOp op = e->as.binary.op;
    int logical = op == BINARY_LOGICAL_AND || op == BINARY_LOGICAL_OR;
    int comparison = op >= BINARY_EQ && op <= BINARY_GE;
    const SemaType *context =
        logical ? t(c, SEMA_BOOL)
                : operand_type(c, a, b, comparison ? NULL : expected);
    const SemaType *at = check(c, a, context, VALUE);
    c->reachable = reachable && (a->flow & SEMA_NEXT);
    const SemaType *bt = check(c, b, special(at) ? context : at, VALUE);
    flow = sequence(a->flow, logical ? (b->flow | SEMA_NEXT) : b->flow);
    if (!special(at) && !special(bt) && at != bt)
      error(c, e->span,
            "Operator operands must have the same type; numeric conversions "
            "are explicit.");
    result = special(at) ? bt : at;
    if (logical) {
      expect(c, a, t(c, SEMA_BOOL), "Logical operators require bool.");
      expect(c, b, t(c, SEMA_BOOL), "Logical operators require bool.");
      result = t(c, SEMA_BOOL);
    } else if (comparison) {
      if (!special(result) &&
          !(sema_number(result) || result->kind == SEMA_CHR ||
            result->kind == SEMA_STR ||
            ((op == BINARY_EQ || op == BINARY_NE) &&
             (result->kind == SEMA_BOOL || result->kind == SEMA_UNIT ||
              (result->kind == SEMA_STRUCT && result->structure->equatable)))))
        error(c, e->span, "Comparison of this type is not implemented yet.");
      result = t(c, SEMA_BOOL);
    } else if (!special(result)) {
      int integer = op == BINARY_REM || op >= BINARY_BIT_AND;
      if (!(integer ? sema_integer(result) : sema_number(result)) &&
          !(op == BINARY_ADD && result->kind == SEMA_STR))
        error(c, e->span,
              integer ? "This operator requires integers."
                      : "Arithmetic requires numbers.");
    }
    break;
  }
  case SEMA_EXPR_IF: {
    SemaExpr *condition = e->as.conditional.condition,
             *yes = e->as.conditional.yes, *no = e->as.conditional.no;
    check(c, condition, t(c, SEMA_BOOL), VALUE);
    expect(c, condition, t(c, SEMA_BOOL), "An if condition must be bool.");
    const SemaType *context = expected;
    if (special(context)) {
      context = hint(yes);
      if (special(context))
        context = hint(no);
      if (special(context) && sema_number(e->type))
        context = e->type;
    }
    c->reachable = reachable && (condition->flow & SEMA_NEXT);
    const SemaType *yt = check(c, yes, no ? context : NULL, no ? use : DISCARD);
    c->reachable = reachable && (condition->flow & SEMA_NEXT);
    const SemaType *nt = no ? check(c, no, special(context) ? yt : context, use)
                            : t(c, SEMA_UNIT);
    flow = sequence(condition->flow, yes->flow | (no ? no->flow : SEMA_NEXT));
    if (!no || use == DISCARD)
      result = t(c, SEMA_UNIT);
    else
      result = join(c, yt, nt, e->span,
                    "Value-producing if branches must have compatible types.");
    if (!no && use == VALUE)
      error(c, e->span, "An if without else cannot be used as a value.");
    break;
  }
  case SEMA_EXPR_WHILE: {
    SemaExpr *condition = e->as.while_loop.condition,
             *body = e->as.while_loop.body;
    check(c, condition, t(c, SEMA_BOOL), VALUE);
    expect(c, condition, t(c, SEMA_BOOL), "A while condition must be bool.");
    c->reachable = reachable && (condition->flow & SEMA_NEXT);
    check(c, body, NULL, DISCARD);
    unsigned exit = SEMA_NEXT | (body->flow & SEMA_RETURN);
    if (condition->kind == SEMA_EXPR_BOOL && condition->integer &&
        !(body->flow & SEMA_BREAK))
      exit &= ~SEMA_NEXT;
    flow = sequence(condition->flow, exit);
    if (use == VALUE)
      error(c, e->span, "Loops cannot be used as values.");
    break;
  }
  case SEMA_EXPR_FOR: {
    SemaExpr *a = e->as.for_loop.lower, *b = e->as.for_loop.upper,
             *body = e->as.for_loop.body;
    const SemaType *context = operand_type(c, a, b, NULL);
    const SemaType *at = check(c, a, context, VALUE);
    c->reachable = reachable && (a->flow & SEMA_NEXT);
    const SemaType *bt = check(c, b, special(at) ? context : at, VALUE);
    if (!special(at) && !sema_integer(at))
      error(c, a->span, "Range bounds must be integers.");
    if (!special(bt) && !sema_integer(bt))
      error(c, b->span, "Range bounds must be integers.");
    if (!special(at) && !special(bt) && at != bt)
      error(c, e->span, "Range bounds must have the same type.");
    e->as.for_loop.variable->type = special(at) ? bt : at;
    c->reachable = reachable && (a->flow & b->flow & SEMA_NEXT);
    check(c, body, NULL, DISCARD);
    flow = sequence(sequence(a->flow, b->flow),
                    SEMA_NEXT | (body->flow & SEMA_RETURN));
    if (use == VALUE)
      error(c, e->span, "Loops cannot be used as values.");
    break;
  }
  }
  c->reachable = reachable;
  e->flow = flow;
  e->returns = !(flow & SEMA_NEXT);
  e->type = e->returns ? t(c, SEMA_NEVER) : result;
  return e->type;
}
static const SemaType *check_function(SemaProgram *p, SemaFunction *f,
                                      Diagnostics *errors, int probe,
                                      int defaults) {
  Checker c = {.p = p,
               .function = f,
               .errors = errors,
               .probe = probe,
               .defaults = defaults,
               .reachable = 1,
               .candidate = &p->types[SEMA_UNKNOWN],
               .expected = f->symbol.return_type};
  if (unknown(c.expected)) {
    c.expected = return_hint(f->body);
    if (special(c.expected))
      c.expected = hint(f->body);
  }
  const SemaType *body = check(
      &c, f->body, c.expected,
      f->written && f->symbol.return_type->kind == SEMA_UNIT ? DISCARD : TAIL);
  if (!f->written && (f->body->flow & SEMA_NEXT))
    c.candidate = join(&c, c.candidate, body, f->body->span,
                       "Incompatible return types.");
  else if (!c.saw_return)
    c.candidate = &p->types[SEMA_UNIT];
  if (f->written) {
    if ((f->body->flow & SEMA_NEXT) && f->symbol.return_type->kind != SEMA_UNIT)
      expect(&c, f->body, f->symbol.return_type,
             "Function body does not match the written return type.");
    return f->symbol.return_type;
  }
  return c.candidate;
}
void sema_check(SemaProgram *p, Diagnostics *errors) {
  size_t count = sema_components(p);
  for (size_t i = 0; i < p->count; i++) {
    p->functions[i].inference_conflict = 0;
    p->functions[i].inference_reported = 0;
    if (!p->functions[i].written)
      p->functions[i].symbol.return_type = &p->types[SEMA_UNKNOWN];
  }
  // Bucket members once: long acyclic call chains must not require quadratic
  // scans of the entire program, nor use the C stack.
  size_t *head = arena_alloc(p->arena, count * sizeof(*head));
  size_t *next = arena_alloc(p->arena, p->count * sizeof(*next));
  for (size_t i = 0; i < count; i++)
    head[i] = SIZE_MAX;
  for (size_t i = 0; i < p->count; i++) {
    next[i] = head[p->functions[i].component];
    head[p->functions[i].component] = i;
  }
  for (size_t component = 0; component < count; component++) {
    for (size_t i = head[component]; i != SIZE_MAX; i = next[i])
      (void)check_function(p, &p->functions[i], errors, 1, 0);
    // Resolve non-literal anchors first, then decimal defaults, then whole
    // defaults. A whole literal must not force int before a decimal anchor
    // elsewhere in the same recursive component has had a chance to propagate.
    for (int defaults = 0; defaults <= 2; defaults++) {
      int changed;
      do {
        changed = 0;
        for (size_t i = head[component]; i != SIZE_MAX; i = next[i]) {
          SemaFunction *f = &p->functions[i];
          const SemaType *inferred = check_function(p, f, errors, 1, defaults);
          // A finite lattice: unknown -> one concrete type -> error. Never
          // let a contradictory cycle oscillate between guessed types.
          const SemaType *old = f->symbol.return_type;
          if (!f->written && old->kind != SEMA_ERROR && !unknown(inferred) &&
              inferred != old) {
            if (!unknown(old) && inferred->kind != SEMA_ERROR)
              f->inference_conflict = 1;
            f->symbol.return_type =
                unknown(old) ? inferred : &p->types[SEMA_ERROR];
            changed = 1;
          }
        }
      } while (changed);
    }
    for (size_t i = head[component]; i != SIZE_MAX; i = next[i]) {
      SemaFunction *f = &p->functions[i];
      if (unknown(f->symbol.return_type)) {
        diagnostics_error(
            errors, f->symbol.span,
            "Cannot infer recursive return type; write a return type.");
        f->symbol.return_type = &p->types[SEMA_ERROR];
        f->inference_reported = 1;
      }
      (void)check_function(p, f, errors, 0, 2);
      // Unrelated errors, even in this function, must not hide a conflict.
      // Track its provenance rather than the total diagnostic count: avoid
      // cascades from frontend/callee error types and duplicate join errors.
      if (f->symbol.return_type->kind == SEMA_ERROR && f->inference_conflict &&
          !f->inference_reported) {
        diagnostics_error(errors, f->symbol.span,
                          "Cannot infer a consistent return type.");
        f->inference_reported = 1;
      }
    }
  }
  sema_check_permissions(p, errors);
  p->checked = p->resolved && errors->count == 0;
}
