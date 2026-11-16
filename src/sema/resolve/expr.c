#include "sema/resolve/internal.h"

#include <string.h>

static SemaExpr *literal(Resolver *r, const Expr *s) {
  SemaExpr *e = node(r,
                     s->kind == EXPR_STRING  ? SEMA_EXPR_STRING
                     : s->kind == EXPR_INT   ? SEMA_EXPR_INT
                     : s->kind == EXPR_FLOAT ? SEMA_EXPR_FLOAT
                     : s->kind == EXPR_BOOL  ? SEMA_EXPR_BOOL
                                             : SEMA_EXPR_CHAR,
                     s->span);
  if (s->kind != EXPR_STRING) {
    e->as.string = s->as.literal;
    return e;
  }
  Token token = s->as.literal;
  unsigned char *bytes = arena_alloc(r->arena, token.length);
  size_t length = 0;
  for (size_t i = 1; i + 1 < token.length; i++) {
    unsigned char c = (unsigned char)token.start[i];
    if (c == '\\') {
      c = (unsigned char)token.start[++i];
      c = c == 'n' ? '\n' : c == 't' ? '\t' : c == '0' ? 0 : c;
    }
    bytes[length++] = c;
  }
  bytes[length] = 0;
  e->as.text.bytes = bytes;
  e->as.text.length = length;
  return e;
}

static SemaExpr *self(Resolver *r, const Expr *s) {
  if (!r->function || r->function->symbol.receiver == RECEIVER_NONE)
    return unsupported(
        r, s->name_span,
        "'self' is only available in struct or trait functions.");
  SemaExpr *e = node(r, SEMA_EXPR_VAR, s->span);
  e->as.variable = r->function->symbol.params[0];
  return e;
}

static SemaExpr *name(Resolver *r, const Expr *s) {
  SemaExpr *e = node(r, SEMA_EXPR_VAR, s->span);
  e->as.variable = resolve_local(r, s->as.name);
  if (!e->as.variable)
    diagnostics_error(r->errors, s->span,
                      resolve_lookup(r->program, s->as.name)
                          ? "Function values are not implemented yet."
                          : "Unknown binding.");
  return e;
}

static SemaExpr *unary(Resolver *r, const Expr *s) {
  // Fold only a directly negated literal, allowing the signed minimum.
  if (s->as.unary.op == UNARY_NEG && (s->as.unary.value->kind == EXPR_INT ||
                                      s->as.unary.value->kind == EXPR_FLOAT)) {
    SemaExpr *e = resolve_expr(r, s->as.unary.value);
    e->negative = 1;
    e->span = s->span;
    return e;
  }
  SemaExpr *e = node(r, SEMA_EXPR_UNARY, s->span);
  e->as.unary.op = s->as.unary.op;
  e->as.unary.value = resolve_expr(r, s->as.unary.value);
  return e;
}

static SemaExpr *binary(Resolver *r, const Expr *s) {
  if (s->as.binary.op == BINARY_RANGE)
    return unsupported(r, s->span, "Ranges are only supported in for loops.");
  SemaExpr *e = node(r, SEMA_EXPR_BINARY, s->span);
  e->as.binary.op = s->as.binary.op;
  e->as.binary.left = resolve_expr(r, s->as.binary.left);
  e->as.binary.right = resolve_expr(r, s->as.binary.right);
  return e;
}

static SemaExpr *postfix(Resolver *r, const Expr *s) {
  SemaExpr *e =
      node(r, s->kind == EXPR_TRY ? SEMA_EXPR_TRY : SEMA_EXPR_PANIC, s->span);
  e->as.value = resolve_expr(r, s->as.postfix);
  resolve_handled(e->as.value);
  return e;
}

static SemaExpr *symbol(Resolver *r, const Expr *s) {
  if (s->as.symbol.module)
    return unsupported(r, s->span,
                       "Module-qualified symbols are not implemented yet.");
  if (s->as.symbol.count > 1 && !strcmp(s->as.symbol.tag, "ok"))
    return unsupported(r, s->span,
                       "Multiple ':ok' payloads are not implemented yet.");
  Span tag_span = s->as.symbol.tag_span.file ? s->as.symbol.tag_span : s->span;
  if (resolve_reject_none_payload(r, s->as.symbol.tag, s->as.symbol.kind,
                                  s->as.symbol.count, tag_span))
    return node(r, SEMA_EXPR_ERROR, s->span);
  SemaExpr *e = node(r, SEMA_EXPR_SYMBOL, s->span);
  e->as.symbol.tag =
      resolve_tag(r, s->as.symbol.tag, s->as.symbol.kind, tag_span);
  e->as.symbol.count = s->as.symbol.count;
  e->as.symbol.payloads = arena_calloc(r->arena, e->as.symbol.count,
                                       sizeof(*e->as.symbol.payloads));
  for (size_t i = 0; i < e->as.symbol.count; i++)
    e->as.symbol.payloads[i] = resolve_expr(r, s->as.symbol.payloads[i]);
  return e;
}

static SemaExpr *field(Resolver *r, const Expr *s) {
  SemaExpr *e = node(r, SEMA_EXPR_FIELD, s->span);
  e->as.field.object = resolve_expr(r, s->as.field.object);
  e->as.field.name = s->as.field.name;
  return e;
}

static SemaExpr *struct_literal(Resolver *r, const Expr *s) {
  SemaStruct *owner = s->as.structure.type->kind == EXPR_NAME
                          ? resolve_named_structure(r, s->as.structure.type)
                          : NULL;
  if (!owner)
    return unsupported(r, s->span, "Unknown struct type.");
  SemaExpr *e = node(r, SEMA_EXPR_STRUCT, s->span);
  e->as.structure.type = owner;
  e->as.structure.count = s->as.structure.count;
  e->as.structure.values =
      arena_calloc(r->arena, s->as.structure.count, sizeof(SemaExpr *));
  e->as.structure.fields =
      arena_calloc(r->arena, s->as.structure.count, sizeof(SemaField *));
  unsigned char *seen = arena_calloc(r->arena, owner->count, 1);
  for (size_t i = 0; i < s->as.structure.count; i++) {
    FieldValue f = s->as.structure.fields[i];
    SemaField *field = sema_field(&owner->type, f.name);
    if (!field)
      diagnostics_error(r->errors, f.span, "Unknown struct field.");
    else if (seen[field->id])
      diagnostics_error(r->errors, f.span,
                        "Duplicate struct field initializer.");
    else
      seen[field->id] = 1;
    e->as.structure.fields[i] = field;
    e->as.structure.values[i] = resolve_expr(r, f.value);
  }
  for (size_t i = 0; i < owner->count; i++)
    if (!seen[i])
      diagnostics_error(r->errors, s->span,
                        "Missing struct field initializer.");
  return e;
}

static SemaExpr *conditional(Resolver *r, const Expr *s) {
  SemaExpr *e = node(r, SEMA_EXPR_IF, s->span);
  e->as.conditional.condition = resolve_expr(r, s->as.conditional.condition);
  e->as.conditional.yes = resolve_expr(r, s->as.conditional.then_branch);
  if (s->as.conditional.else_branch)
    e->as.conditional.no = resolve_expr(r, s->as.conditional.else_branch);
  return e;
}

static SemaExpr *while_loop(Resolver *r, const Expr *s) {
  SemaExpr *e = node(r, SEMA_EXPR_WHILE, s->span);
  e->as.while_loop.condition = resolve_expr(r, s->as.while_loop.condition);
  r->loops++;
  e->as.while_loop.body = resolve_expr(r, s->as.while_loop.body);
  r->loops--;
  return e;
}

static SemaExpr *for_loop(Resolver *r, const Expr *s) {
  const Expr *range = s->as.for_loop.iterable;
  if (range->kind != EXPR_BINARY || range->as.binary.op != BINARY_RANGE)
    return unsupported(r, range->span,
                       "Only range for-loops are implemented yet.");
  SemaExpr *e = node(r, SEMA_EXPR_FOR, s->span);
  e->as.for_loop.lower = resolve_expr(r, range->as.binary.left);
  e->as.for_loop.upper = resolve_expr(r, range->as.binary.right);
  e->as.for_loop.inclusive = range->as.binary.inclusive;
  Local *saved = r->locals;
  r->depth++;
  const Type *annotation = s->as.for_loop.annotation;
  if (annotation)
    e->as.for_loop.annotation_span = annotation->span;
  e->as.for_loop.variable = resolve_variable(r, s->as.for_loop.name, s->span,
                                             resolve_type(r, annotation), 0);
  r->loops++;
  e->as.for_loop.body = resolve_expr(r, s->as.for_loop.body);
  r->loops--;
  resolve_restore(r, saved);
  r->depth--;
  return e;
}

SemaExpr *resolve_expr(Resolver *r, const Expr *s) {
  switch (s->kind) {
  case EXPR_UNIT:
    return node(r, SEMA_EXPR_UNIT, s->span);
  case EXPR_STRING:
  case EXPR_INT:
  case EXPR_FLOAT:
  case EXPR_BOOL:
  case EXPR_CHAR:
    return literal(r, s);
  case EXPR_SELF:
    return self(r, s);
  case EXPR_NAME:
    return name(r, s);
  case EXPR_UNARY:
    return unary(r, s);
  case EXPR_BINARY:
    return binary(r, s);
  case EXPR_CALL:
    return resolve_call(r, s);
  case EXPR_TRY:
  case EXPR_PANIC:
    return postfix(r, s);
  case EXPR_PIPE:
    return resolve_pipe(r, s);
  case EXPR_SYMBOL:
    return symbol(r, s);
  case EXPR_FIELD:
    return field(r, s);
  case EXPR_COLLECTION:
    return unsupported(r, s->span,
                       "Collection literals are not implemented yet.");
  case EXPR_STRUCT_LIT:
    return struct_literal(r, s);
  case EXPR_BLOCK:
    return resolve_block(r, s);
  case EXPR_IF:
    return conditional(r, s);
  case EXPR_WHILE:
    return while_loop(r, s);
  case EXPR_FOR:
    return for_loop(r, s);
  default:
    return unsupported(r, s->span, "This expression is not implemented yet.");
  }
}
