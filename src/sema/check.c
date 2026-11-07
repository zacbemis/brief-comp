#include "sema/sema.h"
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum { DISCARD, VALUE, TAIL, OUTCOME } Use;
typedef struct {
  SemaProgram *p;
  SemaFunction *function;
  Diagnostics *errors;
  int probe, defaults, reachable, saw_return;
  const SemaType *candidate, *expected, *failures;
  size_t speculative_errors, speculative_conflicts;
} Checker;
static const SemaType *t(Checker *c, SemaTypeKind k) { return &c->p->types[k]; }
static int unknown(const SemaType *a) { return !a || a->kind == SEMA_UNKNOWN; }
static int special(const SemaType *a) {
  return unknown(a) || a->kind == SEMA_ERROR || a->kind == SEMA_NEVER;
}
static Diagnostic *error(Checker *c, Span span, const char *message) {
  if (c->probe) {
    c->speculative_errors++;
    return NULL;
  }
  return diagnostics_error(c->errors, span, message);
}
static void expect(Checker *c, SemaExpr *e, const SemaType *wanted,
                   const char *message) {
  if (!special(wanted) && !special(e->type) &&
      !sema_assignable(e->type, wanted))
    error(c, e->span, message);
}
static void unknown_method(Checker *c, SemaExpr *call,
                           const SemaType *receiver) {
  Diagnostic *d = error(c, call->span, "Unknown method for this type.");
  if (!d)
    return;
  SemaSymbol *candidate = call->as.call.fallback;
  if (!candidate || candidate->builtin || candidate->owner ||
      !candidate->arity || candidate->arity != call->as.call.count ||
      special(candidate->params[0]->type) ||
      !sema_assignable(receiver, candidate->params[0]->type))
    return;
  // This advice identifies the call form, not permission, argument, outcome
  // handling or return-type validity. It must not change lookup or add edges.
  const char *format =
      candidate->arity == 1
          ? "This is a top-level function, not a method; use %s(value) "
            "or value |> %s()."
          : "This is a top-level function, not a method; use "
            "%s(value, ...) or value |> %s(...).";
  int length = snprintf(NULL, 0, format, candidate->name, candidate->name);
  if (length < 0)
    return;
  char *message = arena_alloc(c->p->arena, (size_t)length + 1);
  snprintf(message, (size_t)length + 1, format, candidate->name,
           candidate->name);
  diagnostic_note(c->errors, d, candidate->span, message);
}
static char *source_text(Checker *c, Span span, const char *before,
                         const char *after) {
  if (!span.file || span.start > span.end || span.end > span.file->length)
    return NULL;
  size_t a = strlen(before), n = span.end - span.start, b = strlen(after);
  if (memchr(span.file->text + span.start, 0, n))
    return NULL; // Diagnostic replacement strings cannot contain source NULs.
  char *text = arena_alloc(c->p->arena, a + n + b + 1);
  memcpy(text, before, a);
  memcpy(text + a, span.file->text + span.start, n);
  memcpy(text + a + n, after, b + 1);
  return text;
}
// Move exact source chunks, not pretty-printed expressions. Delimiter metadata
// is immutable and belongs to the original call, including in pipeline copies.
static void wrong_call(Checker *c, SemaExpr *call, SemaSymbol *s, int on_type) {
  Diagnostic *d = error(
      c, call->span,
      on_type ? "Methods are called on a value, not the type."
              : "Associated functions are called on the type, not a value.");
  if (!d)
    return;
  diagnostic_note(c->errors, d, s->span,
                  on_type ? "This function has a 'self' receiver."
                          : "This function has no 'self' receiver.");
  const Expr *syntax = call->as.call.syntax;
  if (!syntax || syntax->kind != EXPR_CALL)
    return;
  const Expr *callee = syntax->as.call.callee;
  const SemaType *owner = &s->owner->type;
  if (on_type) {
    if (callee->kind != EXPR_FIELD)
      return;
    const Expr *qualifier = callee->as.field.object;
    if (qualifier->kind != EXPR_NAME || !qualifier->name_span.file)
      return;
    if (call->as.call.source_pipeline) {
      Span dot = callee->as.field.dot_span;
      if (dot.file != qualifier->name_span.file || dot.end != dot.start + 1 ||
          dot.end > dot.file->length || dot.file->text[dot.start] != '.' ||
          !call->as.call.count || call->as.call.args[0]->type != owner ||
          qualifier->span.start != qualifier->name_span.start ||
          qualifier->span.end != qualifier->name_span.end)
        return;
      diagnostic_fix(c->errors, d, qualifier->name_span, "");
      diagnostic_fix(c->errors, d, callee->as.field.dot_span, "");
      return;
    }
    if (!syntax->as.call.count || !call->as.call.count ||
        call->as.call.args[0]->type != owner)
      return;
    const Expr *arg = syntax->as.call.args[0];
    Span open = syntax->as.call.open_span,
         comma = syntax->as.call.first_comma_span;
    if (!open.file || open.file != arg->span.file ||
        open.file != qualifier->name_span.file || open.end > arg->span.start)
      return;
    if (open.end != open.start + 1 || open.end > open.file->length ||
        open.file->text[open.start] != '(' ||
        (!comma.file && syntax->as.call.count > 1))
      return;
    if (comma.file &&
        (comma.file != open.file || comma.start < arg->span.end ||
         comma.end != comma.start + 1 || comma.end > open.file->length ||
         open.file->text[comma.start] != ','))
      return;
    size_t end = comma.file ? comma.start : arg->span.end;
    if (end < open.end || end > open.file->length)
      return;
    Span chunk = span_make(open.file, open.end, end);
    int simple = (arg->kind == EXPR_NAME || arg->kind == EXPR_SELF) &&
                 arg->span.start == arg->name_span.start &&
                 arg->span.end == arg->name_span.end;
    for (size_t i = chunk.start; simple && i < chunk.end; i++)
      if (chunk.file->text[i] == '\n' || chunk.file->text[i] == '\r' ||
          chunk.file->text[i] == '/')
        simple = 0;
    char *text = source_text(c, chunk, simple ? "" : "(", simple ? "" : ")");
    if (!text)
      return;
    // Drop the separator's following spaces too, so `(d, 2)` becomes `(2)`.
    size_t cut = comma.file ? comma.end : end;
    while (comma.file && cut < open.file->length &&
           (open.file->text[cut] == ' ' || open.file->text[cut] == '\t'))
      cut++;
    diagnostic_fix(c->errors, d, qualifier->name_span, text);
    diagnostic_fix(c->errors, d, span_make(open.file, open.end, cut), "");
    return;
  }
  int keep = s->arity && call->as.call.count == s->arity &&
             s->params[0]->type == owner;
  if (call->as.call.source_pipeline) {
    if (keep && callee->kind == EXPR_NAME && callee->name_span.file) {
      size_t n = strlen(owner->name);
      char *prefix = arena_alloc(c->p->arena, n + 2);
      memcpy(prefix, owner->name, n);
      prefix[n] = '.';
      prefix[n + 1] = 0;
      diagnostic_fix(c->errors, d,
                     span_make(callee->name_span.file, callee->name_span.start,
                               callee->name_span.start),
                     prefix);
    }
    return;
  }
  if (callee->kind != EXPR_FIELD)
    return;
  const Expr *value = callee->as.field.object;
  Span open = syntax->as.call.open_span;
  if (!open.file || open.file != value->span.file)
    return;
  if (keep) {
    char *text =
        source_text(c, value->span, "", syntax->as.call.count ? ", " : "");
    if (!text)
      return;
    diagnostic_fix(c->errors, d, value->span, owner->name);
    diagnostic_fix(c->errors, d, span_make(open.file, open.end, open.end),
                   text);
  } else if (call->as.call.count == s->arity + 1 &&
             (value->kind == EXPR_NAME || value->kind == EXPR_SELF) &&
             value->name_span.file) {
    // Removing a simple binding read has no effects. Do not drop evaluation of
    // calls, indexing, operators or other potentially effectful receivers.
    diagnostic_fix(c->errors, d, value->name_span, owner->name);
  }
}
static const SemaType *join(Checker *c, const SemaType *a, const SemaType *b,
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
static const SemaType *full(Checker *c, const SemaType *type) {
  const SemaVariant *ok = sema_ok(c->expected);
  if (type->kind == SEMA_UNIT && ok && ok->payload_count <= 1 &&
      sema_single_payload(ok) == type)
    return sema_symbol_set(c->p, ok, 1);
  return special(type) || type->kind == SEMA_SYM ? type
                                                 : sema_success(c->p, type);
}
static const SemaType *return_join(Checker *c, const SemaType *a,
                                   const SemaType *b, Span span,
                                   const char *message) {
  if ((a->kind == SEMA_SYM && !special(b)) ||
      (b->kind == SEMA_SYM && !special(a))) {
    a = full(c, a);
    b = full(c, b);
  }
  return join(c, a, b, span, message);
}
static int return_fits(Checker *c, const SemaType *actual) {
  const SemaType *wanted = c->function->written_type;
  if (special(actual) || special(wanted))
    return 1;
  actual = full(c, actual);
  wanted = full(c, wanted);
  if (!c->function->partial)
    return sema_assignable(actual, wanted);
  const SemaVariant *ok = sema_ok(actual), *wok = sema_ok(wanted);
  return !ok || (wok && sema_payloads_equal(ok, wok));
}
static const SemaType *checked_return(Checker *c, const SemaType *actual,
                                      Span span, const char *message) {
  if (c->function->written && !return_fits(c, actual)) {
    error(c, span, message);
    // A value rejected by the written contract contributes nothing: it must
    // not collide with a partial annotation's :ok payload, and the function
    // keeps its written contract so callers are still checked against it.
    return t(c, SEMA_NEVER);
  }
  return actual;
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
  case SEMA_EXPR_TRY:
  case SEMA_EXPR_PANIC: {
    const SemaType *input = hint(e->as.value);
    const SemaVariant *success =
        sema_has_errors(input) ? sema_single_success(input) : sema_ok(input);
    const SemaType *payload = success && success->payload_count <= 1
                                  ? sema_single_payload(success)
                                  : NULL;
    return payload ? payload : e->type;
  }
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
  case SEMA_EXPR_MATCH:
    for (size_t i = 0; i < e->as.match.count; i++) {
      const SemaType *h = hint(e->as.match.arms[i].body);
      if (!special(h))
        return h;
    }
    return e->type;
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
// Only direct numeric literals adopt a converter's target. Typed expressions
// keep their source domain; decimal literals still require flt/dbl context.
static const SemaType *conversion_context(SemaSymbol *s, SemaExpr *arg,
                                          const SemaType *fallback) {
  return s && s->builtin == SEMA_BUILTIN_CONVERT &&
                 (arg->kind == SEMA_EXPR_INT || arg->kind == SEMA_EXPR_FLOAT)
             ? s->return_type
             : fallback;
}
// Pipeline input evaluation is sequenced through a hidden binding, but its
// literals still receive the same parameter context as an ordinary call.
static const SemaType *pipeline_hint(Checker *c, SemaExpr *binding) {
  SemaExpr *call = binding->as.binding.stage;
  if (!call)
    return NULL;
  SemaSymbol *s = call->as.call.target;
  size_t arg = 0;
  if (call->as.call.method) {
    const SemaType *receiver =
        hint(call->as.call.pipeline ? binding->as.binding.value
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
  return conversion_context(s, binding->as.binding.value, wanted);
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
  } else if (e->kind == SEMA_EXPR_MATCH) {
    const SemaType *h = return_hint(e->as.match.input);
    if (!special(h))
      return h;
    for (size_t i = 0; i < e->as.match.count; i++) {
      if (!e->as.match.arms[i].useful)
        continue;
      h = return_hint(e->as.match.arms[i].body);
      if (!special(h))
        return h;
    }
  } else if (e->kind == SEMA_EXPR_SYMBOL) {
    for (size_t i = 0; i < e->as.symbol.count; i++) {
      SemaExpr *payload = e->as.symbol.payloads[i];
      const SemaType *h = return_hint(payload);
      if (!special(h))
        return h;
      if (!(payload->flow & SEMA_NEXT))
        break;
    }
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
  const SemaVariant *ok = sema_ok(expected);
  if (ok)
    expected = ok->payload_count <= 1 ? sema_single_payload(ok) : NULL;
  if (sema_number(expected))
    return expected;
  const SemaType *h = hint(a);
  if (!special(h))
    return h;
  h = hint(b);
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
static const SemaType *check(Checker *c, SemaExpr *e, const SemaType *expected,
                             Use use) {
  const SemaType *result = t(c, SEMA_UNIT);
  size_t before = c->speculative_errors;
  size_t conflicts = c->speculative_conflicts;
  unsigned flow = SEMA_NEXT;
  int reachable = c->reachable;
  e->reachable = reachable;
  e->adapt = e->outcome = 0;
  e->failures = NULL;
  const SemaType *raw = NULL;
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
    v->type = check(c, value,
                    e->as.binding.stage ? pipeline_hint(c, e) : v->annotation,
                    e->as.binding.outcome ? OUTCOME : VALUE);
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
  case SEMA_EXPR_SYMBOL: {
    const SemaType *context_type = expected;
    if (e->as.symbol.tag == c->p->ok && !special(expected) &&
        expected->kind != SEMA_SYM)
      context_type = sema_success(c->p, expected);
    const SemaVariant *context = sema_variant(context_type, e->as.symbol.tag);
    size_t count = e->as.symbol.count;
    const SemaType *single = NULL;
    const SemaType **types =
        count > 1 ? arena_calloc(c->p->arena, count, sizeof(*types)) : &single;
    const SemaType *incomplete = NULL;
    for (size_t i = 0; i < count; i++) {
      SemaExpr *payload = e->as.symbol.payloads[i];
      const SemaType *wanted =
          context && i < context->payload_count ? context->payloads[i] : NULL;
      c->reachable = reachable && (flow & SEMA_NEXT);
      const SemaType *pt =
          check(c, payload, wanted,
                payload->kind == SEMA_EXPR_SYMBOL ? OUTCOME : VALUE);
      flow = sequence(flow, payload->flow);
      if (special(pt)) {
        if (!incomplete || pt->kind == SEMA_ERROR)
          incomplete = pt;
      } else if (wanted && sema_assignable(pt, wanted)) {
        // Context can widen a nested union independently at each position.
        pt = wanted;
      }
      types[i] = pt;
    }
    if (!e->as.symbol.tag)
      result = t(c, SEMA_ERROR);
    else if (incomplete)
      result = incomplete;
    else {
      SemaVariant variant = {e->as.symbol.tag, count ? types : NULL, count};
      result = sema_symbol_set(c->p, &variant, 1);
    }
    // The first-version implicit success ABI still has one result slot.
    // Positional data checking must not invent a tuple or erase extra values.
    if (e->as.symbol.tag == c->p->ok && count > 1) {
      error(c, e->span, "Multiple ':ok' payloads are not implemented yet.");
      result = t(c, SEMA_ERROR);
    }
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
      value = check(c, e->as.value, c->expected, TAIL);
      flow = e->as.value->flow;
    }
    if (flow & SEMA_NEXT) {
      value = checked_return(
          c, value, e->span,
          "Return value does not match the written return type.");
      if (reachable) {
        c->saw_return = 1;
        if (!c->function->written || c->function->partial)
          c->candidate = return_join(c, c->candidate, value, e->span,
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
    if (e->as.block.pipeline && e->as.block.count)
      e->outcome = e->as.block.items[e->as.block.count - 1]->outcome;
    break;
  case SEMA_EXPR_CALL: {
    SemaSymbol *s = e->as.call.target;
    int wrong_form = 0, on_type = 0;
    if (e->as.call.method) {
      const SemaType *receiver = check(c, e->as.call.args[0], NULL, VALUE);
      SemaSymbol *method = sema_method(c->p, receiver, e->as.call.method);
      s = method;
      if (e->as.call.pipeline && !special(receiver) &&
          (!method || method->receiver == RECEIVER_NONE)) {
        SemaSymbol *fallback = e->as.call.fallback;
        s = e->as.call.shadowed ? NULL : fallback ? fallback : method;
      }
      e->as.call.target = s;
      if (!special(receiver)) {
        if (!s) {
          if (e->as.call.pipeline)
            error(c, e->span,
                  e->as.call.shadowed ? "A local binding is not callable."
                                      : "Unknown function.");
          else
            unknown_method(c, e, receiver);
        } else if (s == method && s->receiver == RECEIVER_NONE)
          wrong_form = 1;
      }
    } else if (s && s->owner && s->receiver != RECEIVER_NONE) {
      wrong_form = on_type = 1;
    }
    e->as.call.wrong_form = wrong_form;
    for (size_t i = 0; i < e->as.call.count; i++) {
      c->reachable = reachable && (flow & SEMA_NEXT);
      SemaExpr *arg = e->as.call.args[i];
      const SemaType *wanted =
          !wrong_form && s && i < s->arity ? s->params[i]->type : NULL;
      if (!(e->as.call.method && i == 0))
        check(c, arg, conversion_context(s, arg, wanted), VALUE);
      expect(c, arg, wanted,
             s && s->builtin == SEMA_BUILTIN_PRINT
                 ? "print expects a str argument."
                 : "Function argument has the wrong type.");
      if (s && s->builtin == SEMA_BUILTIN_STR && !special(arg->type) &&
          !sema_number(arg->type) && arg->type->kind != SEMA_BOOL)
        error(c, arg->span, "str expects a number or bool argument.");
      if (s && s->builtin == SEMA_BUILTIN_CONVERT && !special(arg->type) &&
          !sema_number(arg->type) && arg->type->kind != SEMA_STR &&
          arg->type->kind != SEMA_CHR)
        error(c, arg->span,
              "Numeric conversion expects a number, chr or str argument.");
      flow = sequence(flow, arg->flow);
    }
    if (wrong_form)
      wrong_call(c, e, s, on_type);
    if (!wrong_form && s && e->as.call.count != s->arity)
      error(c, e->span,
            s->builtin == SEMA_BUILTIN_PRINT
                ? "print expects exactly one str argument."
            : s->builtin == SEMA_BUILTIN_STR
                ? "str expects exactly one number or bool argument."
            : s->builtin == SEMA_BUILTIN_CONVERT
                ? "Numeric conversion expects exactly one number, chr or str "
                  "argument."
                : "Wrong number of function arguments.");
    result = s ? s->return_type
             : e->as.call.method && unknown(e->as.call.args[0]->type)
                 ? t(c, SEMA_UNKNOWN)
                 : t(c, SEMA_ERROR);
    e->outcome = 1;
    const SemaType *outcome = s ? s->outcome_type : NULL;
    if (s && s->builtin == SEMA_BUILTIN_CONVERT) {
      outcome =
          e->as.call.count == 1
              ? sema_conversion(c->p, e->as.call.args[0]->type, s->return_type)
              : t(c, SEMA_ERROR);
      if (outcome->kind == SEMA_ERROR && e->as.call.count == 1 &&
          (sema_number(e->as.call.args[0]->type) ||
           e->as.call.args[0]->type->kind == SEMA_STR)) {
        int text = e->as.call.args[0]->type->kind == SEMA_STR;
        SemaTag *failure = name_map_get(&c->p->tag_names, c->p->module,
                                        text ? "invalid" : "overflow");
        // Source/source kind conflicts already have a resolver diagnostic.
        // Otherwise report on every final check, not via persistent flags
        // that could swallow the error on a recheck. error() also defers a
        // locally invalid conversion during SCC probes.
        if (!failure || !failure->kind_conflict) {
          Diagnostic *d =
              error(c, e->span,
                    !failure ? "Too many symbol tags."
                    : text ? "Numeric conversions use ':!invalid', not neutral "
                             "':invalid'."
                           : "Numeric conversions use ':!overflow', not "
                             "neutral ':overflow'.");
          if (d && failure && failure->kind_span.file)
            diagnostic_note(c->errors, d, failure->kind_span,
                            "The neutral symbol is declared here.");
        }
      }
      result = sema_result(c->p, outcome);
    }
    const SemaVariant *ok = sema_ok(outcome);
    // Use the actual ABI result, including an already-erased only-:ok
    // envelope. Payload/field errors do not make an enclosing data tag fail.
    if (sema_has_errors(result) && !e->as.call.handled) {
      error(c, e->span,
            "A call that can fail must be handled with ?, !, or a match.");
      // A forbidden outcome cannot provide a usable value or return candidate.
      // Defer during SCC probes rather than poisoning a component: an error
      // outcome there could erase the very fallibility we need to diagnose.
      result = t(c, c->probe ? SEMA_UNKNOWN : SEMA_ERROR);
    }
    if (ok && outcome->symbols->count == 1 &&
        (use == OUTCOME ||
         (use == VALUE && expected && expected->kind == SEMA_SYM))) {
      raw = result;
      result = outcome;
      e->adapt = 1;
    }
    break;
  }
  case SEMA_EXPR_TRY:
  case SEMA_EXPR_PANIC: {
    SemaExpr *value = e->as.value;
    const SemaType *input = check(c, value, NULL, OUTCOME);
    flow = value->flow;
    int errors = sema_has_errors(input);
    const SemaVariant *success = errors ? sema_single_success(input) : NULL;
    int terminal = sema_only_errors(input);
    if (!errors) {
      if (!special(input))
        error(c, e->span,
              "? and ! cannot handle this value: it has no error tags.");
      // A provisional SCC symbol set can acquire error tags on a later probe.
      // Do not permanently poison callers while that set is still growing.
      result = c->probe && input->kind == SEMA_SYM ? t(c, SEMA_UNKNOWN)
               : special(input)                    ? input
                                                   : t(c, SEMA_ERROR);
    } else if (success && success->payload_count > 1) {
      error(
          c, e->span,
          success->tag == c->p->ok
              ? "Multiple ':ok' payloads are not implemented yet."
              : "Extracting several success payloads is not implemented yet.");
      result = t(c, SEMA_ERROR);
    } else {
      const SemaType *payload = sema_single_payload(success);
      result = terminal   ? t(c, SEMA_NEVER)
               : !success ? sema_kind_part(c->p, input, SEMA_TAG_NEUTRAL)
               : payload  ? payload
                          : t(c, SEMA_UNIT);
      e->failures = sema_kind_part(c->p, input, SEMA_TAG_ERROR);
      if (e->kind == SEMA_EXPR_TRY && e->failures) {
        flow = sequence(flow, terminal ? SEMA_RETURN : SEMA_NEXT | SEMA_RETURN);
        if (c->function->written && !return_fits(c, e->failures))
          error(c, e->span,
                "Propagated variants are not in the written return type.");
        if (reachable && (value->flow & SEMA_NEXT)) {
          // An always-propagating expression is a real return, not a
          // valueless ending that can invent an implicit :ok alternative.
          if (terminal)
            c->saw_return = 1;
          c->failures = c->failures
                            ? join(c, c->failures, e->failures, e->span,
                                   "Symbol tag has conflicting payload types.")
                            : e->failures;
        }
      } else if (terminal)
        flow = sequence(flow, 0);
    }
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
    e->as.binary.common = NULL;
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
      result = t(c, SEMA_BOOL);
    } else if (comparison) {
      int equality = op == BINARY_EQ || op == BINARY_NE;
      int shaped = result->kind == SEMA_STRUCT || result->kind == SEMA_SYM;
      if (!special(result) &&
          !(sema_number(result) || result->kind == SEMA_CHR ||
            result->kind == SEMA_STR ||
            (equality &&
             (result->kind == SEMA_BOOL || result->kind == SEMA_UNIT ||
              (shaped && sema_equatable(result))))))
        // Only recursive shapes make a struct or symbol non-equatable. That is
        // a language rule, unlike ordering, which is not implemented yet.
        error(c, e->span,
              equality && shaped
                  ? "Types that can contain a reference cycle have no == or !=."
                  : "Comparison of this type is not implemented yet.");
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
  case SEMA_EXPR_MATCH: {
    SemaExpr *input = e->as.match.input;
    const SemaType *it = check(c, input, NULL, OUTCOME);
    int scalar =
        sema_number(it) || it->kind == SEMA_BOOL || it->kind == SEMA_CHR;
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
        check(c, a->lower, it, VALUE);
        if (scalar || (string_literal && a->kind == PAT_LITERAL))
          expect(c, a->lower, it, "Pattern value has the wrong type.");
      }
      if (a->upper) {
        check(c, a->upper, it, VALUE);
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
    const SemaType *context = expected;
    if (special(context))
      context = hint(e);
    if (special(context) && sema_number(e->type))
      context = e->type;
    result = t(c, SEMA_NEVER);
    unsigned arms_flow = 0;
    for (size_t i = 0; i < e->as.match.count; i++) {
      SemaArm *a = &e->as.match.arms[i];
      c->reachable = reachable && (input->flow & SEMA_NEXT) && a->useful;
      const SemaType *at = check(c, a->body, context, use);
      if (a->useful) {
        arms_flow |= a->body->flow;
        if (use != DISCARD)
          result = use == TAIL ? return_join(c, result, at, a->span,
                                             "Value-producing match arms must "
                                             "have compatible types.")
                               : join(c, result, at, a->span,
                                      "Value-producing match arms must have "
                                      "compatible types.");
      }
    }
    flow = sequence(input->flow, arms_flow);
    if (use == DISCARD)
      result = t(c, SEMA_UNIT);
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
      result =
          use == TAIL
              ? return_join(
                    c, yt, nt, e->span,
                    "Value-producing if branches must have compatible types.")
              : join(c, yt, nt, e->span,
                     "Value-producing if branches must have compatible types.");
    if (!no && (use == VALUE || use == OUTCOME))
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
    if (use == VALUE || use == OUTCOME)
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
    if (use == VALUE || use == OUTCOME)
      error(c, e->span, "Loops cannot be used as values.");
    break;
  }
  }
  c->reachable = reachable;
  e->flow = flow;
  e->returns = !(flow & SEMA_NEXT);
  e->raw_type = e->returns ? t(c, SEMA_NEVER) : (raw ? raw : result);
  if (!e->returns && !e->adapt) {
    const SemaVariant *ok = sema_ok(result);
    if (ok && ok->payload_count <= 1 && result->symbols->count == 1 &&
        use != OUTCOME && use != TAIL &&
        (!expected || expected->kind != SEMA_SYM)) {
      const SemaType *payload = sema_single_payload(ok);
      result = payload ? payload : t(c, SEMA_UNIT);
      e->adapt = 2;
    } else if (e->outcome && e->kind != SEMA_EXPR_CALL && !special(result) &&
               result->kind != SEMA_SYM && use == OUTCOME) {
      result = sema_success(c->p, result);
      e->adapt = 1;
    }
  }
  // A locally diagnosed error result is not a return-type anchor. Poisoning
  // an SCC with it can erase the types/context needed to report the primary
  // error on the final pass. Defer only errors observed in this expression's
  // probe; inherited errors without a local failure and intrinsic inference
  // conflicts still propagate. Final reports never depend on these counters.
  if (c->probe && result->kind == SEMA_ERROR &&
      c->speculative_errors != before && c->speculative_conflicts == conflicts)
    result = t(c, SEMA_UNKNOWN);
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
               .expected =
                   f->written ? f->written_type : f->symbol.outcome_type};
  if (unknown(c.expected)) {
    c.expected = return_hint(f->body);
    if (special(c.expected))
      c.expected = hint(f->body);
  }
  int discard = f->written && f->written_type->kind == SEMA_UNIT;
  const SemaType *body =
      check(&c, f->body, c.expected, discard ? DISCARD : TAIL);
  if (f->body->flow & SEMA_NEXT) {
    if (discard)
      body = &p->types[SEMA_UNIT];
    body =
        checked_return(&c, body, f->body->span,
                       "Function body does not match the written return type.");
    if (!f->written || f->partial)
      c.candidate = return_join(&c, c.candidate, body, f->body->span,
                                "Incompatible return types.");
  } else if (!c.saw_return)
    c.candidate = &p->types[SEMA_UNIT];
  const SemaType *result = full(&c, c.candidate);
  if (c.failures)
    result = join(&c, result, c.failures, f->body->span,
                  "Symbol tag has conflicting payload types.");
  if (f->written && !f->partial)
    return full(&c, f->written_type);
  if (f->partial)
    result = join(&c, result, full(&c, f->written_type), f->body->span,
                  "Symbol tag has conflicting payload types.");
  return result;
}
void sema_check(SemaProgram *p, Diagnostics *errors) {
  size_t count = sema_components(p);
  for (size_t i = 0; i < p->count; i++) {
    p->functions[i].inference_conflict = 0;
    p->functions[i].inference_reported = 0;
    SemaFunction *f = &p->functions[i];
    if (!f->written) {
      f->symbol.outcome_type = &p->types[SEMA_UNKNOWN];
      f->symbol.return_type = &p->types[SEMA_UNKNOWN];
    } else {
      f->symbol.outcome_type =
          f->written_type->kind == SEMA_SYM || special(f->written_type)
              ? f->written_type
              : sema_success(p, f->written_type);
      f->symbol.return_type = sema_result(p, f->symbol.outcome_type);
    }
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
          // Symbol sets grow monotonically; one tag cannot change payload
          // type. Other concrete types still have the M3 finite lattice.
          const SemaType *old = f->symbol.outcome_type;
          if ((!f->written || f->partial) && old->kind != SEMA_ERROR &&
              !unknown(inferred) && inferred != old) {
            const SemaType *merged = unknown(old) ? inferred : NULL;
            if (old->kind == SEMA_SYM && inferred->kind == SEMA_SYM)
              merged = sema_symbol_union(p, old, inferred);
            if (!merged) {
              if (inferred->kind != SEMA_ERROR)
                f->inference_conflict = 1;
              merged = &p->types[SEMA_ERROR];
            }
            f->symbol.outcome_type = merged;
            f->symbol.return_type = sema_result(p, merged);
            changed |= merged != old;
          }
        }
      } while (changed);
    }
    for (size_t i = head[component]; i != SIZE_MAX; i = next[i]) {
      SemaFunction *f = &p->functions[i];
      int unresolved = unknown(f->symbol.return_type);
      const SemaType *final = check_function(p, f, errors, 0, 2);
      if (unresolved) {
        // A malformed handler may have stayed unknown while SCC outcomes
        // grew. Its final, specific error is not an unanchored recursion.
        if (final->kind != SEMA_ERROR) {
          diagnostics_error(
              errors, f->symbol.span,
              "Cannot infer recursive return type; write a return type.");
          f->inference_reported = 1;
        }
        f->symbol.return_type = f->symbol.outcome_type = &p->types[SEMA_ERROR];
      }
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
