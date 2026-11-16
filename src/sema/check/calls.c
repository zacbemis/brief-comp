#include "sema/check/internal.h"

#include <stdio.h>
#include <string.h>

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

// Picks the callee once the receiver type is known. Sets *wrong_form when a
// method is called on its type or an associated function on a value.
static SemaSymbol *callee(Checker *c, SemaExpr *e, int *wrong_form,
                          int *on_type) {
  SemaSymbol *s = e->as.call.target;
  if (!e->as.call.method) {
    if (s && s->owner && s->receiver != RECEIVER_NONE)
      *wrong_form = *on_type = 1;
    return s;
  }
  const SemaType *receiver = check_expr(c, e->as.call.args[0], NULL, VALUE);
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
      *wrong_form = 1;
  }
  return s;
}

static void arguments(Checker *c, SemaExpr *e, SemaSymbol *s, int wrong_form,
                      unsigned *flow) {
  for (size_t i = 0; i < e->as.call.count; i++) {
    c->reachable = e->reachable && (*flow & SEMA_NEXT);
    SemaExpr *arg = e->as.call.args[i];
    const SemaType *wanted =
        !wrong_form && s && i < s->arity ? s->params[i]->type : NULL;
    if (!(e->as.call.method && i == 0))
      check_expr(c, arg, check_conversion_context(s, arg, wanted), VALUE);
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
    *flow = sequence(*flow, arg->flow);
  }
  if (wrong_form || !s || e->as.call.count == s->arity)
    return;
  error(c, e->span,
        s->builtin == SEMA_BUILTIN_PRINT
            ? "print expects exactly one str argument."
        : s->builtin == SEMA_BUILTIN_STR
            ? "str expects exactly one number or bool argument."
        : s->builtin == SEMA_BUILTIN_CONVERT
            ? "Numeric conversion expects exactly one number, chr or str "
              "argument."
            : "Wrong number of function arguments.");
}

static const SemaType *conversion(Checker *c, SemaExpr *e, SemaSymbol *s) {
  const SemaType *outcome =
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
                : text   ? "Numeric conversions use ':!invalid', not neutral "
                           "':invalid'."
                         : "Numeric conversions use ':!overflow', not "
                           "neutral ':overflow'.");
      if (d && failure && failure->kind_span.file)
        diagnostic_note(c->errors, d, failure->kind_span,
                        "The neutral symbol is declared here.");
    }
  }
  return outcome;
}

const SemaType *check_call(Checker *c, SemaExpr *e, Use use, unsigned *flow) {
  int wrong_form = 0, on_type = 0;
  SemaSymbol *s = callee(c, e, &wrong_form, &on_type);
  e->as.call.wrong_form = wrong_form;
  arguments(c, e, s, wrong_form, flow);
  if (wrong_form)
    wrong_call(c, e, s, on_type);
  const SemaType *result =
      s ? s->return_type
      : e->as.call.method && unknown(e->as.call.args[0]->type)
          ? t(c, SEMA_UNKNOWN)
          : t(c, SEMA_ERROR);
  if (s && s->builtin == SEMA_BUILTIN_CONVERT)
    result = conversion(c, e, s);
  // Returning a call handles its complete result, not only its errors.
  // TAIL reaches ret values and value-producing branch/block endings; it
  // never reaches arguments, payloads or other discarded statements.
  // Payload/field errors do not make an enclosing data tag fail.
  if (sema_has_errors(result) && !e->as.call.handled && use != TAIL &&
      use != BOUND) {
    error(c, e->span,
          "A call that can fail must be handled with ?, !, or a match.");
    // A forbidden outcome cannot provide a usable value or return candidate.
    // Defer during SCC probes rather than poisoning a component: an error
    // outcome there could erase the very fallibility we need to diagnose.
    result = t(c, c->probe ? SEMA_UNKNOWN : SEMA_ERROR);
  }
  return result;
}
