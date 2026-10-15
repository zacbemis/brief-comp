#include "sema/sema.h"

#include <string.h>

typedef struct {
  Arena *arena;
  SemaProgram *program;
  Diagnostics *errors;
} Resolver;
static SemaSymbol *lookup(SemaProgram *p, const char *name) {
  if (strcmp(name, "print") == 0)
    return &p->print;
  for (size_t i = 0; i < p->count; i++)
    if (strcmp(name, p->functions[i].symbol.name) == 0)
      return &p->functions[i].symbol;
  return NULL;
}
static SemaExpr *node(Resolver *r, SemaExprKind kind, Span span) {
  SemaExpr *e = arena_calloc(r->arena, 1, sizeof(*e));
  e->kind = kind;
  e->span = span;
  return e;
}
static SemaExpr *unsupported(Resolver *r, Span span, const char *message) {
  diagnostics_error(r->errors, span, message);
  return node(r, SEMA_EXPR_ERROR, span);
}
static SemaExpr *resolve_expr(Resolver *r, const Expr *syntax);
static SemaExpr *resolve_stmt(Resolver *r, const Stmt *s) {
  if (s->kind == STMT_EXPR)
    return resolve_expr(r, s->as.value);
  if (s->kind != STMT_RET)
    return unsupported(r, s->span, "This statement is not implemented yet.");
  SemaExpr *e = node(r, SEMA_EXPR_RETURN, s->span);
  if (s->as.value)
    e->as.value = resolve_expr(r, s->as.value);
  return e;
}
static SemaExpr *resolve_expr(Resolver *r, const Expr *syntax) {
  SemaExpr *e;
  switch (syntax->kind) {
  case EXPR_UNIT:
    return node(r, SEMA_EXPR_UNIT, syntax->span);
  case EXPR_STRING:
    e = node(r, SEMA_EXPR_STRING, syntax->span);
    e->as.string = syntax->as.literal;
    return e;
  case EXPR_CALL:
    e = node(r, SEMA_EXPR_CALL, syntax->span);
    if (syntax->as.call.type_count)
      diagnostics_error(r->errors, syntax->span,
                        "Generic calls are not implemented yet.");
    if (syntax->as.call.callee->kind != EXPR_NAME)
      diagnostics_error(
          r->errors, syntax->as.call.callee->span,
          "Only calls to plain function names are currently implemented.");
    else {
      e->as.call.target = lookup(r->program, syntax->as.call.callee->as.name);
      if (!e->as.call.target)
        diagnostics_error(r->errors, syntax->as.call.callee->span,
                          "Unknown function.");
    }
    e->as.call.count = syntax->as.call.count;
    e->as.call.args =
        arena_calloc(r->arena, e->as.call.count, sizeof(SemaExpr *));
    for (size_t i = 0; i < e->as.call.count; i++)
      e->as.call.args[i] = resolve_expr(r, syntax->as.call.args[i]);
    return e;
  case EXPR_BLOCK:
    e = node(r, SEMA_EXPR_BLOCK, syntax->span);
    e->as.block.count = syntax->as.block.count;
    e->as.block.items =
        arena_calloc(r->arena, e->as.block.count, sizeof(SemaExpr *));
    for (size_t i = 0; i < e->as.block.count; i++) {
      BlockItem item = syntax->as.block.items[i];
      e->as.block.items[i] =
          item.kind == BLOCK_ITEM_EXPR ? resolve_expr(r, item.as.expr)
          : item.kind == BLOCK_ITEM_STMT
              ? resolve_stmt(r, item.as.stmt)
              : unsupported(r, item.span,
                            "Local declarations are not implemented yet.");
    }
    return e;
  case EXPR_NAME:
    return unsupported(r, syntax->span,
                       "Function names can only be used in calls; values and "
                       "bindings are not implemented yet.");
  default:
    return unsupported(r, syntax->span,
                       "This expression is not implemented yet.");
  }
}
SemaProgram *sema_resolve(Arena *arena, const Program *syntax,
                          Diagnostics *errors) {
  SemaProgram *p = arena_calloc(arena, 1, sizeof(*p));
  p->unit_type.kind = SEMA_UNIT;
  p->str_type.kind = SEMA_STR;
  p->error_type.kind = SEMA_ERROR;
  p->print = (SemaSymbol){
      .name = "print", .arity = 1, .builtin = 1, .return_type = &p->unit_type};
  p->functions = arena_calloc(arena, syntax->count, sizeof(SemaFunction));
  Resolver r = {arena, p, errors};
  for (size_t i = 0; i < syntax->count; i++) {
    const Decl *d = syntax->items[i];
    if (d->kind != DECL_FN) {
      diagnostics_error(
          errors, d->span,
          "Only function declarations are currently implemented.");
      continue;
    }
    SemaSymbol *previous = lookup(p, d->name);
    if (previous) {
      Diagnostic *error = diagnostics_error(
          errors, d->span,
          previous->builtin ? "The name 'print' is reserved for the builtin."
                            : "Duplicate function name.");
      if (!previous->builtin)
        diagnostic_note(errors, error, previous->span, "First declared here.");
      continue;
    }
    SemaFunction *f = &p->functions[p->count];
    f->syntax = d;
    f->symbol = (SemaSymbol){.name = d->name,
                             .span = d->span,
                             .id = p->count,
                             .arity = d->as.function.count,
                             .return_type = &p->unit_type};
    p->count++;
    if (strcmp(d->name, "main") == 0)
      p->entry = &f->symbol;
    if (d->as.function.count)
      diagnostics_error(errors, d->span,
                        "Function parameters are not implemented yet.");
    if (d->as.function.return_type &&
        d->as.function.return_type->kind != TYPE_UNIT)
      diagnostics_error(
          errors, d->as.function.return_type->span,
          "Only the written return type () is currently implemented.");
  }
  if (!p->entry)
    diagnostics_error(errors, syntax->span, "A program must define main().");
  for (size_t i = 0; i < p->count; i++)
    p->functions[i].body =
        resolve_expr(&r, p->functions[i].syntax->as.function.body);
  p->resolved = errors->count == 0;
  return p;
}
static const SemaType *check_expr(SemaProgram *p, SemaExpr *e,
                                  Diagnostics *errors) {
  const SemaType *type = &p->unit_type;
  e->returns = 0;
  switch (e->kind) {
  case SEMA_EXPR_UNIT:
    break;
  case SEMA_EXPR_STRING:
    type = &p->str_type;
    break;
  case SEMA_EXPR_ERROR:
    type = &p->error_type;
    break;
  case SEMA_EXPR_RETURN:
    if (e->as.value) {
      const SemaType *value = check_expr(p, e->as.value, errors);
      if (value != &p->unit_type && value != &p->error_type)
        diagnostics_error(errors, e->span,
                          "Only unit return values are currently implemented.");
    }
    e->returns = 1;
    break;
  case SEMA_EXPR_BLOCK:
    for (size_t i = 0; i < e->as.block.count; i++) {
      SemaExpr *item = e->as.block.items[i];
      const SemaType *value = check_expr(p, item, errors);
      if (!e->returns) {
        type = value;
        if (item->returns) {
          e->returns = 1;
          type = &p->unit_type;
        }
      }
    }
    break;
  case SEMA_EXPR_CALL:
    for (size_t i = 0; i < e->as.call.count; i++) {
      check_expr(p, e->as.call.args[i], errors);
      if (e->as.call.args[i]->returns)
        e->returns = 1;
    }
    if (!e->as.call.target) {
      type = &p->error_type;
      break;
    }
    if (e->as.call.count != e->as.call.target->arity)
      diagnostics_error(errors, e->span,
                        e->as.call.target->builtin
                            ? "print expects exactly one str argument."
                            : "Wrong number of function arguments.");
    if (e->as.call.target->builtin && e->as.call.count == 1) {
      SemaExpr *arg = e->as.call.args[0];
      if (!arg->returns && arg->type != &p->str_type &&
          arg->type != &p->error_type)
        diagnostics_error(errors, arg->span, "print expects a str argument.");
    }
    type = e->as.call.target->return_type;
    break;
  }
  e->type = type;
  return type;
}
static void visit_function(SemaProgram *p, SemaFunction *f, Diagnostics *errors,
                           unsigned depth);
static void visit_calls(SemaProgram *p, SemaExpr *e, Diagnostics *errors,
                        unsigned depth) {
  switch (e->kind) {
  case SEMA_EXPR_CALL:
    for (size_t i = 0; i < e->as.call.count; i++)
      visit_calls(p, e->as.call.args[i], errors, depth);
    if (e->as.call.target && !e->as.call.target->builtin) {
      SemaFunction *f = &p->functions[e->as.call.target->id];
      if (f->visit == 1)
        diagnostics_error(errors, e->span,
                          "Recursive functions are not implemented yet.");
      else if (!f->visit) {
        if (depth == 128)
          diagnostics_error(errors, e->span,
                            "Function call graph depth limit exceeded.");
        else
          visit_function(p, f, errors, depth + 1);
      }
    }
    break;
  case SEMA_EXPR_BLOCK:
    for (size_t i = 0; i < e->as.block.count; i++)
      visit_calls(p, e->as.block.items[i], errors, depth);
    break;
  case SEMA_EXPR_RETURN:
    if (e->as.value)
      visit_calls(p, e->as.value, errors, depth);
    break;
  default:
    break;
  }
}
static void visit_function(SemaProgram *p, SemaFunction *f, Diagnostics *errors,
                           unsigned depth) {
  f->visit = 1;
  visit_calls(p, f->body, errors, depth);
  f->visit = 2;
}
void sema_check(SemaProgram *p, Diagnostics *errors) {
  for (size_t i = 0; i < p->count; i++)
    p->functions[i].visit = 0;
  for (size_t i = 0; i < p->count; i++)
    if (!p->functions[i].visit)
      visit_function(p, &p->functions[i], errors, 1);
  for (size_t i = 0; i < p->count; i++) {
    SemaFunction *f = &p->functions[i];
    const SemaType *type = check_expr(p, f->body, errors);
    if (!f->syntax->as.function.return_type && type == &p->str_type)
      diagnostics_error(errors, f->body->span,
                        "Value-returning functions are not implemented yet; "
                        "write ': ()' to discard the final value.");
  }
  p->checked = p->resolved && errors->count == 0;
}
